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


#include "opencv2/core/hal/interface.h"


#undef  cv_hal_resize
#define cv_hal_resize __imx2d_resize


int imx2d_resize(int src_type, const uchar *src_data, size_t src_step, int src_width, int src_height,
                  uchar *dst_data, size_t dst_step, int dst_width, int dst_height,
                  double inv_scale_x, double inv_scale_y, int interpolation);

inline int __imx2d_resize(int src_type, const uchar *src_data, size_t src_step, int src_width, int src_height,
                  uchar *dst_data, size_t dst_step, int dst_width, int dst_height,
                  double inv_scale_x, double inv_scale_y, int interpolation)
{
    int ret;
    ret = imx2d_resize(src_type, src_data, src_step, src_width, src_height,
                    dst_data, dst_step, dst_width, dst_height,
                    inv_scale_x, inv_scale_y, interpolation);

#ifdef TEGRA_RESIZE
    if (ret == CV_HAL_ERROR_NOT_IMPLEMENTED)
        ret = TEGRA_RESIZE(src_type, src_data, src_step, src_width, src_height,
                    dst_data, dst_step, dst_width, dst_height,
                    inv_scale_x, inv_scale_y, interpolation);
#endif

    return ret;
}


