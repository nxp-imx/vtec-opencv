/*
   Copyright 2023 NXP

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */

//#define DEBUG

#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core.hpp>

#include "imx2d_hal.hpp"
#include "imx2d_hal_utils.hpp"
#include "g2d.h"

#include "profiler.hpp"


namespace cv {
namespace imx2d {

// profiling points
//#define PF_ENABLED
PF_ENTRY(transform_prepro);
PF_ENTRY(transform_cache);
PF_ENTRY(transform_g2d);
PF_ENTRY(transform_postpro);


static void io_release_intermediate(const struct io_buffer& src,
                    const struct io_buffer& dst,
                    int src_type, int inout_type,
                    const struct io_buffer& in,
                    const struct io_buffer& out)
{
    CV_UNUSED(src_type);
    CV_UNUSED(inout_type);
    bool in_copy = (src.g2d_buf == nullptr);
    bool out_copy = (dst.g2d_buf == nullptr);

    // free intermediate g2d buffers if any
    if (in_copy && (in.g2d_buf != nullptr))
        gfree(in.g2d_buf);
    if (out_copy && (out.g2d_buf != nullptr))
        gfree(out.g2d_buf);
}

static int io_preprocess(const struct io_buffer& src,
                   const struct io_buffer& dst,
                   int src_type, int &inout_type,
                   struct io_buffer& in,
                   struct io_buffer& out)
{
    // Mat buffers are not g2d allocated - need copy
    bool in_copy = (src.g2d_buf == nullptr);
    bool out_copy = (dst.g2d_buf == nullptr);

    int cn = CV_MAT_CN(src_type);
    IMX2D_Assert((cn >= 3) || (cn <=4));
    IMX2D_Assert((cn != 3) || IMX2D_HW_SUPPORT_3CH());

    inout_type = src_type;

    int inout_cn = CV_MAT_CN(inout_type);

    cv::Mat msrc, min, mout;
    bool inout_cacheable = true;

    in.g2d_buf = nullptr;
    out.g2d_buf = nullptr;

    if (in_copy)
    {
        msrc = cv::Mat(src.height, src.width, src_type, src.data, src.step); // no alloc

        size_t in_stride = src.width * inout_cn;
        struct g2d_buf *in_buf = galloc(src.height * in_stride, inout_cacheable);
        if (in_buf == nullptr)
            return CV_HAL_ERROR_UNKNOWN;

        in = { .g2d_buf = in_buf, .data = static_cast<uchar *>(in_buf->buf_vaddr), .step = in_stride,
               .width = src.width, .height = src.height, .cacheable = inout_cacheable };

        min = cv::Mat(in.height, in.width, inout_type, in.data, in.step); // no alloc
        msrc.copyTo(min);
    }
    else
    {
        in = src; // struct copy
    }

    if (out_copy)
    {
        size_t out_stride = dst.width * inout_cn;
        struct g2d_buf *out_buf = galloc(dst.height * out_stride, inout_cacheable);
        if (out_buf == nullptr)
            return CV_HAL_ERROR_UNKNOWN;


        out = { .g2d_buf = out_buf, .data = static_cast<uchar *>(out_buf->buf_vaddr), .step = out_stride,
               .width = dst.width, .height = dst.height, .cacheable = inout_cacheable };
    }
    else
    {
        out = dst; // struct copy
    }

    return CV_HAL_ERROR_OK;
}

static int io_postprocess(const struct io_buffer& src,
                    const struct io_buffer& dst,
                    int src_type, int inout_type,
                    const struct io_buffer& in,
                    const struct io_buffer& out)
{
    bool out_copy = (dst.g2d_buf == nullptr);
    CV_UNUSED(src);
    CV_UNUSED(in);

    cv::Mat mdst, mout;
    mout = cv::Mat(out.height, out.width, inout_type, out.data, out.step); // no alloc
    mdst = cv::Mat(dst.height, dst.width, src_type, dst.data, dst.step); // no alloc

    if (out_copy)
        mout.copyTo(mdst);

    return CV_HAL_ERROR_OK;
}

static int do_blit(struct io_buffer src,
                   struct io_buffer dst,
                   int src_type,
                   int flip_type, int rotate_type)
{
    int ret;
    struct g2d_surface in_surface, out_surface;
    struct io_buffer in, out;
    int inout_type;
    enum g2d_rotation rotation;
    void* handle;

    Imx2dHal& imx2dHal = Imx2dHal::getInstance();

    // Flip V+H is to be submitted as 180 degrees rotation
    IMX2D_Assert(flip_type != IMX2D_FLIP_BOTH);

    PF_ENTER(transform_prepro);
    ret = io_preprocess(src, dst, src_type, inout_type, in, out);
    PF_EXIT(transform_prepro);

    if (ret != CV_HAL_ERROR_OK)
        goto error;

    PF_ENTER(transform_cache);
    if (in.cacheable)
        ret = g2d_cache_clean(in.g2d_buf);
    else
        ret = 0;

    if ((ret == 0) && (out.cacheable))
        g2d_cache_invalidate(out.g2d_buf);
    PF_EXIT(transform_cache);

    if (ret != 0) {
        ret = CV_HAL_ERROR_UNKNOWN;
        goto error;
    }

    switch (flip_type)
    {
    case IMX2D_FLIP_VERTICAL:
        rotation = G2D_FLIP_V;
        break;
    case IMX2D_FLIP_HORIZONTAL:
        rotation = G2D_FLIP_H;
        break;
    default:
        rotation = G2D_ROTATION_0;
        break;
    }

    g2d_surface_init(in_surface, CV_MAT_CN(inout_type),
                     in.width, in.height, in.step,
                     in.g2d_buf, in.data,
                     rotation);

    switch (rotate_type)
    {
    case IMX2D_ROTATE_90:
        rotation = G2D_ROTATION_90;
        break;
    case IMX2D_ROTATE_180:
        rotation = G2D_ROTATION_180;
        break;
    case IMX2D_ROTATE_270:
        rotation = G2D_ROTATION_270;
        break;
    default:
        rotation = G2D_ROTATION_0;
        break;
    }

    g2d_surface_init(out_surface, CV_MAT_CN(inout_type),
                     out.width, out.height, out.step,
                     out.g2d_buf, out.data,
                     rotation);

    handle = imx2dHal.getG2dHandle();

    PF_ENTER(transform_g2d);
    ret = g2d_blit(handle, &in_surface, &out_surface);
    if (ret == 0)
        ret = g2d_finish(handle);
    PF_EXIT(transform_g2d);

    if (ret != 0) {
        ret = CV_HAL_ERROR_UNKNOWN;
        goto error;
    }

    PF_ENTER(transform_postpro);
    ret = io_postprocess(src, dst, src_type, inout_type, in, out);
    PF_EXIT(transform_postpro);

error:
    io_release_intermediate(src, dst, src_type, inout_type, in, out);

    return ret;
}

static int transform_impl(int src_type, const uchar* src_data, size_t src_step,
                          int src_width, int src_height,
                          uchar* dst_data, size_t dst_step,
                          int flip_type, int rotate_type)
{
    int ret;
    struct io_buffer src, dst;
    struct g2d_buf * src_g2d_buf, * dst_g2d_buf;
    bool src_cacheable, dst_cacheable;
    (void) is_g2d_buffer(src_data, src_g2d_buf, src_cacheable);
    (void) is_g2d_buffer(dst_data, dst_g2d_buf, dst_cacheable);

    int dst_width, dst_height;
    switch(rotate_type)
    {
    case IMX2D_ROTATE_90:
    case IMX2D_ROTATE_270:
        dst_width = src_height;
        dst_height = src_width;
        break;
    default:
        dst_width = src_width;
        dst_height = src_height;
        break;
    }

    src = { .g2d_buf = src_g2d_buf, .data = const_cast<void *>(static_cast<const void *>(src_data)), .step = src_step,
            .width = src_width, .height = src_height, .cacheable = src_cacheable };

    dst = { .g2d_buf = dst_g2d_buf, .data = dst_data, .step = dst_step,
            .width = dst_width, .height = dst_height, .cacheable = dst_cacheable };

    ret = do_blit(src, dst, src_type, flip_type, rotate_type);

    return ret;
}

static bool is_transform_supported(
                  int src_type, const uchar *src_data, size_t src_step,
                  int src_width, int src_height,
                  uchar *dst_data, size_t dst_step,
                  int flip_type, int rotate_type)
{
    int depth = CV_MAT_DEPTH(src_type);
    int cn = CV_MAT_CN(src_type);

    // integer matrixes only
    if (depth != CV_8U)
        return false;

    // flip on both axis not supported
    if (flip_type == IMX2D_FLIP_BOTH)
        return false;

    // in place operation not supported
    size_t dst_height;
    switch(rotate_type)
    {
    case IMX2D_ROTATE_90:
    case IMX2D_ROTATE_270:
        dst_height = src_width;
        break;
    default:
        dst_height = src_height;
        break;
    }
    size_t src_sz = src_height * src_step;
    size_t dst_sz = dst_height * dst_step;
    if (!((dst_data + dst_sz <= src_data) || (dst_data >= src_data + src_sz)))
        return false;

    // 3 and 4 channels matrixes
    if (((cn == 3) && (IMX2D_HW_SUPPORT_3CH())) || cn == 4)
        return true;

    return false;
}

} // imx2d::
} // cv::


using namespace cv::imx2d;

int imx2d_flip(int src_type, const uchar* src_data, size_t src_step,
                 int src_width, int src_height,
                 uchar* dst_data, size_t dst_step, int flip_type)
{
    int ret;
    Imx2dHal& imx2dHal = Imx2dHal::getInstance();
    int rotate_type;

    IMX2D_Assert(src_width > 0 && src_height > 0);

    if (!imx2dHal.isEnabled())
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    // flip both handled as single step 180 degrees rotation
    rotate_type = IMX2D_ROTATE_NONE;
    if (flip_type == IMX2D_FLIP_BOTH)
    {
        flip_type = IMX2D_FLIP_NONE;
        rotate_type = IMX2D_ROTATE_180;
    }

    if (!is_transform_supported(src_type, src_data, src_step,
                                src_width, src_height,
                                dst_data, dst_step,
                                flip_type, rotate_type))
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    ret = transform_impl(src_type, src_data, src_step,
                         src_width, src_height,
                         dst_data, dst_step,
                         flip_type, rotate_type);

    if (ret == CV_HAL_ERROR_OK)
        imx2dHal.counters.incrementCount(Imx2dHalCounters::FLIP);

    return ret;
}

int imx2d_rotate(int src_type, const uchar* src_data, size_t src_step,
                 int src_width, int src_height,
                 uchar* dst_data, size_t dst_step, int rotate_type)
{
    int ret;
    Imx2dHal& imx2dHal = Imx2dHal::getInstance();
    int flip_type = IMX2D_FLIP_NONE;

    IMX2D_Assert(src_width > 0 && src_height > 0);

    if (!imx2dHal.isEnabled())
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    if (!is_transform_supported(src_type, src_data, src_step,
                                src_width, src_height,
                                dst_data, dst_step,
                                flip_type, rotate_type))
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    ret = transform_impl(src_type, src_data, src_step,
                         src_width, src_height,
                         dst_data, dst_step,
                         flip_type, rotate_type);

    if (ret == CV_HAL_ERROR_OK)
        imx2dHal.counters.incrementCount(Imx2dHalCounters::ROTATE);

    return ret;
}
