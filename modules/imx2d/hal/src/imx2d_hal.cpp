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
#ifdef DEBUG
#define CV_LOG_STRIP_LEVEL (CV_LOG_LEVEL_VERBOSE + 1)
#endif

#include "opencv2/core/base.hpp"
#include "opencv2/core/hal/interface.h"
#include "opencv2/core/utils/logger.hpp"
#include "opencv2/imgproc/hal/interface.h"

#include "opencv2/imgproc.hpp" // cv::cvtColor()

#include "imx2d_hal.hpp"
#include "imx2d_common.hpp"

//#define PF_ENABLED
#include "profiler.hpp"

#include "g2d.h"

using namespace cv::imx2d;

PF_ENTRY(resize_prepro);
PF_ENTRY(resize_cache);
PF_ENTRY(resize_g2d);
PF_ENTRY(resize_postpro);


static inline bool IMX2D_HW_SUPPORT_3CH()
{
    Imx2dHal& hal = Imx2dHal::getInstance();
    Imx2dHal::HardwareFeatures& hwFeatures = hal.getHardwareFeatures();
    return hwFeatures.threeChannels;
}


static bool is_g2d_buffer(const uchar* vaddr, struct g2d_buf*& g2dBuf, bool& cacheable)
{
    bool ret;
    g2dBuf = nullptr;
    cacheable = false;
    void* _vaddr = const_cast<void*>(static_cast<const void*>(vaddr));

    void *handle;
    Imx2dGAllocator& gAlloc = Imx2dGAllocator::getInstance();
    ret = gAlloc.isGraphicBuffer(_vaddr, handle, cacheable);
    g2dBuf = static_cast<struct g2d_buf*>(handle);

    CV_LOG_DEBUG(NULL, "vaddr:" << vaddr << " ret:" << ret <<
                       " cacheable:" << cacheable);

    return ret;
}

static struct g2d_buf* galloc(size_t size, bool cacheable)
{
    void *handle;
    Imx2dGAllocator& gAlloc = Imx2dGAllocator::getInstance();

    // gbuf allocation goes through buffers cache
    (void)gAlloc.alloc(size, cacheable, handle);
    return static_cast<struct g2d_buf*>(handle);
}


static void gfree(struct g2d_buf *buf)
{
    void *handle = static_cast<void*>(buf);
    Imx2dGAllocator& gAlloc = Imx2dGAllocator::getInstance();

    // gbuf deallocation goes through buffers cache
    gAlloc.free(handle);
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
    CV_LOG_DEBUG(NULL, "depth:" << depth << " cn:" << cn <<
                       " interpolation:" << interpolation);

    if (interpolation != CV_HAL_INTER_LINEAR)
        return false;

    // integer matrices only
    if (depth != CV_8U)
        return false;

    // 3 and 4 channels matrices only
    if (cn < 3 || cn > 4)
        return false;

    return true;
}

static void g2d_cache_clean(struct g2d_buf *buf)
{
    int ret = g2d_cache_op(buf, G2D_CACHE_CLEAN);
    CV_Assert(ret == 0 || ret == G2D_STATUS_NOT_SUPPORTED);
}

static void g2d_cache_invalidate(struct g2d_buf *buf)
{
    int ret = g2d_cache_op(buf, G2D_CACHE_INVALIDATE);
    CV_Assert(ret == 0 || ret == G2D_STATUS_NOT_SUPPORTED);
}

static void g2d_surface_init(g2d_surface& s, int cn,
                             int width, int height, int step,
                             struct g2d_buf* buf, void* vaddr)
{
    CV_UNUSED(vaddr);

    CV_LOG_DEBUG(NULL, "width:" << width << " height:" << height <<
                       " step:" << step);

    s.planes[0] = s.planes[1] = s.planes[2] = buf->buf_paddr;

    // reference: i.MX Graphics User Guide Figure 2
    // width and height from parameters refer to rectangle dimensions, not
    // the surface itself
    int top_bytes = (static_cast<uchar *>(vaddr) -
                     static_cast<uchar *>(buf->buf_vaddr));
    int top = top_bytes / step;
    int left_bytes = top_bytes - top * step;
    CV_Assert(left_bytes % cn == 0);
    int left = left_bytes / cn;
    int right = left + width;
    int bottom = top + height;
    CV_Assert(step % cn == 0);
    int stride = step / cn;

    CV_LOG_DEBUG(NULL, "top:" << top << " left:" << left <<
                       " right:" << right << " bottom:" << bottom <<
                       " height:" << buf->buf_size / step <<
                       " width/stride:" << stride);

    s.top = top;
    s.bottom = bottom;

    s.left = left;
    s.right = right;

    s.stride = stride;
    s.width = stride; // XXX: could be any value from [right..stride] ?
    s.height = buf->buf_size / step; // size rounded up to page size multiple

    if (cn == 4)
        s.format = G2D_BGRA8888; // 4 chans config functional for GPU2D, PXP and DPU
    else if (cn == 3)
        s.format = G2D_RGB888;

    s.rot = G2D_ROTATION_0;

    // unused for now
    s.blendfunc = G2D_ZERO;
    s.global_alpha = 0;
    s.clrcolor = 0;
}

struct io_buffer {
    struct g2d_buf* g2d_buf;
    uchar* data;
    size_t step;
    int width;
    int height;
    bool cacheable;
};

static void io_preprocess(const struct io_buffer& src,
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
    CV_Assert((cn >= 3) || (cn <=4 ));

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

    if (in_copy || csc)
    {
        msrc = cv::Mat(src.height, src.width, src_type, src.data, src.step); // no alloc

        size_t in_stride = src.width * inout_cn;
        struct g2d_buf *in_buf = galloc(src.height * in_stride, inout_cacheable);
        CV_Assert(in_buf);

        in = { .g2d_buf = in_buf, .data = (uchar*)in_buf->buf_vaddr, .step = in_stride,
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
        CV_Assert(out_buf);

        out = { .g2d_buf = out_buf, .data = (uchar*)out_buf->buf_vaddr, .step = out_stride,
               .width = dst.width, .height = dst.height, .cacheable = inout_cacheable };
    }
    else
    {
        out = dst; // struct copy
    }
}


static void io_postprocess(const struct io_buffer& src,
                          const struct io_buffer& dst,
                          int src_type, int inout_type,
                          const struct io_buffer& in,
                          const struct io_buffer& out)
{
    bool in_copy = (src.g2d_buf == nullptr);
    bool out_copy = (dst.g2d_buf == nullptr);
    bool csc;
    int csc_type;

    // 3 chans support may have been emulated via software CSC
    if (src_type != inout_type)
    {
        CV_Assert((src_type == CV_8UC3) && (inout_type == CV_8UC4));
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
    else if (out_copy)// copy only
        mout.copyTo(mdst);

    // free intermediate g2d buffers if any
    if (in_copy || csc)
        gfree(in.g2d_buf);
    if (out_copy || csc)
        gfree(out.g2d_buf);
}



int imx2d_resize(int src_type, const uchar* src_data, size_t src_step,
                 int src_width, int src_height,
                 uchar* dst_data, size_t dst_step, int dst_width, int dst_height,
                 double inv_scale_x, double inv_scale_y, int interpolation)
{
    int ret;
    struct g2d_surface in_surface, out_surface;
    struct io_buffer src, dst, in, out;
    int inout_type;

    Imx2dHal& imx2dHal = Imx2dHal::getInstance();

    CV_Assert(dst_width > 0 && dst_height > 0);

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

    src = { .g2d_buf = src_g2d_buf, .data = const_cast<uchar *>(src_data), .step = src_step,
            .width = src_width, .height = src_height, .cacheable = src_cacheable };

    dst = { .g2d_buf = dst_g2d_buf, .data = dst_data, .step = dst_step,
            .width = dst_width, .height = dst_height, .cacheable = dst_cacheable };

    PF_ENTER(resize_prepro);
    io_preprocess(src, dst, src_type, inout_type, in, out);
    PF_EXIT(resize_prepro);

    PF_ENTER(resize_cache);
    if (in.cacheable)
        g2d_cache_clean(in.g2d_buf);

    if (out.cacheable)
        g2d_cache_invalidate(out.g2d_buf);
    PF_EXIT(resize_cache);

    g2d_surface_init(in_surface, CV_MAT_CN(inout_type),
                     in.width, in.height, in.step,
                     in.g2d_buf, in.data);

    g2d_surface_init(out_surface, CV_MAT_CN(inout_type),
                     out.width, out.height, out.step,
                     out.g2d_buf, out.data);

    void* handle = imx2dHal.getG2dHandle();

    PF_ENTER(resize_g2d);
    ret = g2d_blit(handle, &in_surface, &out_surface);
    CV_Assert(ret == 0);
    ret = g2d_finish(handle);
    CV_Assert(ret == 0);
    PF_EXIT(resize_g2d);

    PF_ENTER(resize_postpro);
    io_postprocess(src, dst, src_type, inout_type, in, out);
    PF_EXIT(resize_postpro);

    imx2dHal.counters.incrementCount(Imx2dHalCounters::RESIZE);

    return CV_HAL_ERROR_OK;
}
