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


#include "imx2d_common.hpp"
#include "g2d.h"

#ifdef DEBUG
#define IMX2D_LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#else
#define IMX2D_LOG(fmt, ...)
#endif

#define __IMX2D_TO_STRING(m) #m
#define IMX2D_TO_STRING(m) __IMX2D_TO_STRING(m)

#define IMX2D_Assert( expr ) do { if (!!(expr)) ; else \
                                    throw std::runtime_error( \
                                      "(" __FILE__ ":" \
                                      IMX2D_TO_STRING(__LINE__) ")" \
                                      " Assertion failed: " #expr); \
                                } while (0)


namespace cv {
namespace imx2d {

inline bool IMX2D_HW_SUPPORT_3CH()
{
    cv::imx2d::Imx2dHal& hal = cv::imx2d::Imx2dHal::getInstance();
    cv::imx2d::HardwareCapabilities& hwCaps = hal.getHardwareCapabilities();
    return hwCaps.hasCapability(cv::imx2d::HardwareCapabilities::THREE_CHANNELS);
}

struct io_buffer {
    struct g2d_buf* g2d_buf;
    void* data;
    size_t step;
    int width;
    int height;
    bool cacheable;
};

bool is_g2d_buffer(const void* vaddr, struct g2d_buf*& g2dBuf, bool& cacheable);

struct g2d_buf* galloc(size_t size, bool cacheable);

void gfree(struct g2d_buf *buf);

int g2d_cache_clean(struct g2d_buf *buf);

int g2d_cache_invalidate(struct g2d_buf *buf);

void g2d_surface_init(g2d_surface& s, int cn,
                      int width, int height, int step,
                      struct g2d_buf* buf, void* vaddr,
                      enum g2d_rotation rotation = G2D_ROTATION_0);

} // imx2d::
} // cv::

