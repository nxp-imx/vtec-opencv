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
#include <opencv2/imgproc/hal/interface.h>
#include <opencv2/imgproc.hpp> // cv::cvtColor()

#include "imx2d_hal.hpp"
#include "imx2d_hal_utils.hpp"
#include "g2d.h"

#include "profiler.hpp"


namespace cv {
namespace imx2d {

// profiling points
//#define PF_ENABLED
PF_ENTRY(resize_prepro);
PF_ENTRY(resize_cache);
PF_ENTRY(resize_g2d);
PF_ENTRY(resize_postpro);


static void io_release_intermediate_csc(
                   const struct io_buffer& src,
                   const struct io_buffer& dst,
                   int src_type, int inout_type,
                   const struct io_buffer& in,
                   const struct io_buffer& out)
{
    // Mat buffers are not g2d allocated - need copy
    bool in_copy = (src.g2d_buf == nullptr);
    bool out_copy = (dst.g2d_buf == nullptr);

    // 3 chans support may have been emulated via software CSC
    bool csc = (src_type != inout_type);

    // free intermediate g2d buffers if any
    if ((in_copy || csc) && (in.g2d_buf != nullptr))
        gfree(in.g2d_buf);
    if ((out_copy || csc) && (out.g2d_buf != nullptr))
        gfree(out.g2d_buf);
}


static int io_preprocess_csc(
                   const struct io_buffer& src,
                   const struct io_buffer& dst,
                   int src_type, int &inout_type,
                   struct io_buffer& in,
                   struct io_buffer& out)
{
    // Mat buffers are not g2d allocated - need copy
    bool in_copy = (src.g2d_buf == nullptr);
    bool out_copy = (dst.g2d_buf == nullptr);
    bool csc;
    int csc_type;

    int cn = CV_MAT_CN(src_type);
    IMX2D_Assert((cn >= 3) || (cn <=4));

    // 3 chans support via 4 chans software CSC if not supported by hardware
    if ((cn == 3) && (!IMX2D_HW_SUPPORT_3CH()))
    {
        inout_type = CV_8UC4;
        csc = true;
        csc_type = cv::COLOR_BGR2BGRA;
    } else {
        inout_type = src_type;
        csc = false;
        csc_type = cv::COLOR_COLORCVT_MAX;
    }

    int inout_cn = CV_MAT_CN(inout_type);

    cv::Mat msrc, min, mout;
    bool inout_cacheable = true;

    in.g2d_buf = nullptr;
    out.g2d_buf = nullptr;

    if (in_copy || csc)
    {
        msrc = cv::Mat(src.height, src.width, src_type, src.data, src.step); // no alloc

        size_t in_stride = src.width * inout_cn;
        struct g2d_buf *in_buf = galloc(src.height * in_stride, inout_cacheable);
        if (in_buf == nullptr)
            return CV_HAL_ERROR_UNKNOWN;

        in = { .g2d_buf = in_buf, .data = static_cast<uchar *>(in_buf->buf_vaddr), .step = in_stride,
               .width = src.width, .height = src.height, .cacheable = inout_cacheable };

        min = cv::Mat(in.height, in.width, inout_type, in.data, in.step); // no alloc
        if (csc) // implies copy
            cv::cvtColor(msrc, min, csc_type);
        else // copy only
            msrc.copyTo(min);
    }
    else
    {
        in = src; // struct copy
    }

    if (out_copy || csc)
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

static int io_postprocess_csc(
                    const struct io_buffer& src,
                    const struct io_buffer& dst,
                    int src_type, int inout_type,
                    const struct io_buffer& in,
                    const struct io_buffer& out)
{
    bool out_copy = (dst.g2d_buf == nullptr);
    bool csc;
    int csc_type;
    CV_UNUSED(src);
    CV_UNUSED(in);

    // 3 chans support may have been emulated via software CSC
    if (src_type != inout_type)
    {
        IMX2D_Assert((src_type == CV_8UC3) && (inout_type == CV_8UC4));
        csc = true;
        csc_type = cv::COLOR_BGRA2BGR;
    } else {
        csc = false;
        csc_type = cv::COLOR_COLORCVT_MAX;
    }

    cv::Mat mdst, mout;
    mout = cv::Mat(out.height, out.width, inout_type, out.data, out.step); // no alloc
    mdst = cv::Mat(dst.height, dst.width, src_type, dst.data, dst.step); // no alloc

    if (csc) // implies copy
        cv::cvtColor(mout, mdst, csc_type);
    else if (out_copy) // copy only
        mout.copyTo(mdst);

    return CV_HAL_ERROR_OK;
}

static bool is_resize_supported(
                  int src_type, const uchar *src_data, size_t src_step,
                  int src_width, int src_height,
                  uchar *dst_data, size_t dst_step, int dst_width, int dst_height,
                  double inv_scale_x, double inv_scale_y, int interpolation)
{
    CV_UNUSED(src_data);
    CV_UNUSED(src_step);
    CV_UNUSED(src_width);
    CV_UNUSED(src_height);
    CV_UNUSED(dst_data);
    CV_UNUSED(dst_step);
    CV_UNUSED(dst_width);
    CV_UNUSED(dst_height);
    CV_UNUSED(inv_scale_x);
    CV_UNUSED(inv_scale_y);

    int depth = CV_MAT_DEPTH(src_type);
    int cn = CV_MAT_CN(src_type);
    IMX2D_LOG("depth:%d cn:%d interpolation:%d", depth, cn, interpolation);

    if (interpolation != CV_HAL_INTER_LINEAR)
        return false;

    // integer matrixes only
    if (depth != CV_8U)
        return false;

    // 3 and 4 channels matrixes only
    if (cn < 3 || cn > 4)
        return false;

    return true;
}

} // imx2d::
} // cv::


using namespace cv::imx2d;

int imx2d_resize(int src_type, const uchar* src_data, size_t src_step,
                 int src_width, int src_height,
                 uchar* dst_data, size_t dst_step, int dst_width, int dst_height,
                 double inv_scale_x, double inv_scale_y, int interpolation)
{
    int ret;
    struct g2d_surface in_surface, out_surface;
    struct io_buffer src, dst, in, out;
    int inout_type;
    void* handle;

    Imx2dHal& imx2dHal = Imx2dHal::getInstance();

    IMX2D_Assert(dst_width > 0 && dst_height > 0);

    if (!imx2dHal.isEnabled())
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    if (!is_resize_supported(src_type, src_data, src_step,
                             src_width, src_height,
                             dst_data, dst_step, dst_width, dst_height,
                             inv_scale_x, inv_scale_y, interpolation))
        return CV_HAL_ERROR_NOT_IMPLEMENTED;

    struct g2d_buf * src_g2d_buf, * dst_g2d_buf;
    bool src_cacheable, dst_cacheable;
    (void) is_g2d_buffer(src_data, src_g2d_buf, src_cacheable);
    (void) is_g2d_buffer(dst_data, dst_g2d_buf, dst_cacheable);

    src = { .g2d_buf = src_g2d_buf, .data = const_cast<void *>(static_cast<const void *>(src_data)), .step = src_step,
            .width = src_width, .height = src_height, .cacheable = src_cacheable };

    dst = { .g2d_buf = dst_g2d_buf, .data = dst_data, .step = dst_step,
            .width = dst_width, .height = dst_height, .cacheable = dst_cacheable };

    PF_ENTER(resize_prepro);
    ret = io_preprocess_csc(src, dst, src_type, inout_type, in, out);
    PF_EXIT(resize_prepro);

    if (ret != CV_HAL_ERROR_OK)
        goto error;

    PF_ENTER(resize_cache);
    if (in.cacheable)
        ret = g2d_cache_clean(in.g2d_buf);
    else
        ret = 0;

    if ((ret == 0) && (out.cacheable))
        ret = g2d_cache_invalidate(out.g2d_buf);
    PF_EXIT(resize_cache);

    if (ret != 0) {
        ret = CV_HAL_ERROR_UNKNOWN;
        goto error;
    }

    g2d_surface_init(in_surface, CV_MAT_CN(inout_type),
                     in.width, in.height, in.step,
                     in.g2d_buf, in.data);

    g2d_surface_init(out_surface, CV_MAT_CN(inout_type),
                     out.width, out.height, out.step,
                     out.g2d_buf, out.data);

    handle = imx2dHal.getG2dHandle();

    PF_ENTER(resize_g2d);
    ret = g2d_blit(handle, &in_surface, &out_surface);
    if (ret == 0)
        ret = g2d_finish(handle);
    PF_EXIT(resize_g2d);

    if (ret != 0) {
        ret = CV_HAL_ERROR_UNKNOWN;
        goto error;
    }

    PF_ENTER(resize_postpro);
    ret = io_postprocess_csc(src, dst, src_type, inout_type, in, out);
    PF_EXIT(resize_postpro);

    if (ret == CV_HAL_ERROR_OK)
        imx2dHal.counters.incrementCount(Imx2dHalCounters::RESIZE);

error:
    io_release_intermediate_csc(src, dst, src_type, inout_type, in, out);

    return ret;
}
