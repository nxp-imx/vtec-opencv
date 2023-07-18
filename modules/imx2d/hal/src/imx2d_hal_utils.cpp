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

#include <opencv2/core/base.hpp>
#include <opencv2/imgproc.hpp> // cv::cvtColor()

#include "g2d.h"
#include "imx2d_hal_utils.hpp"

namespace cv {
namespace imx2d {

bool is_g2d_buffer(const void* vaddr, struct g2d_buf*& g2dBuf, bool& cacheable)
{
    bool ret;
    g2dBuf = nullptr;
    cacheable = false;
    //void* _vaddr = const_cast<void*>(static_cast<const void*>(vaddr));
    void* _vaddr = const_cast<void*>(vaddr);

    void *handle;
    Imx2dGAllocator& gAlloc = Imx2dGAllocator::getInstance();
    ret = gAlloc.isGraphicBuffer(_vaddr, handle, cacheable);
    g2dBuf = static_cast<struct g2d_buf*>(handle);

    IMX2D_LOG("vaddr:%p ret:%d cacheable:%d", vaddr, ret, cacheable);

    return ret;
}

struct g2d_buf* galloc(size_t size, bool cacheable)
{
    void *handle;
    Imx2dGAllocator& gAlloc = Imx2dGAllocator::getInstance();

    // gbuf allocation goes through buffers cache
    (void)gAlloc.alloc(size, cacheable, handle);
    return static_cast<struct g2d_buf*>(handle);
}

void gfree(struct g2d_buf *buf)
{
    void *handle = static_cast<void*>(buf);
    Imx2dGAllocator& gAlloc = Imx2dGAllocator::getInstance();

    // gbuf deallocation goes through buffers cache
    gAlloc.free(handle);
}

int g2d_cache_clean(struct g2d_buf *buf)
{
    int ret = g2d_cache_op(buf, G2D_CACHE_CLEAN);
    if (ret == 0 || ret == G2D_STATUS_NOT_SUPPORTED) {
        return 0;
    }

    IMX2D_ERROR("%s error", __func__);
    return -1;
}

int g2d_cache_invalidate(struct g2d_buf *buf)
{
    int ret = g2d_cache_op(buf, G2D_CACHE_INVALIDATE);
    if (ret == 0 || ret == G2D_STATUS_NOT_SUPPORTED) {
        return 0;
    }

    IMX2D_ERROR("%s error", __func__);
    return -1;
}

void g2d_surface_init(g2d_surface& s, int cn,
                      int width, int height, int step,
                      struct g2d_buf* buf, void* vaddr,
                      enum g2d_rotation rotation)
{
    IMX2D_LOG("width:%d height:%d step:%d", width, height, step);

    s.planes[0] = s.planes[1] = s.planes[2] = buf->buf_paddr;

    // reference: i.MX Graphics User Guide Figure 2
    // width and height from parameters refer to rectangle dimensions, not
    // the surface itself
    int top_bytes = (static_cast<char *>(vaddr) -
                     static_cast<char *>(buf->buf_vaddr));
    int top = top_bytes / step;
    int left_bytes = top_bytes - top * step;
    IMX2D_Assert(left_bytes % cn == 0);
    int left = left_bytes / cn;
    int right = left + width;
    int bottom = top + height;
    IMX2D_Assert(step % cn == 0);
    int stride = step / cn;

    IMX2D_LOG("top:%d left:%d right:%d bottom:%d height:%d width/stride:%d",
              top, left, right, bottom,  buf->buf_size / step, stride);

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

    s.rot = rotation;

    // unused for now
    s.blendfunc = G2D_ZERO;
    s.global_alpha = 0;
    s.clrcolor = 0;
}

} // imx2d::
} // cv::