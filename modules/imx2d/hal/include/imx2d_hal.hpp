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


#undef  cv_hal_flip
#define cv_hal_flip __imx2d_flip

enum {
    IMX2D_FLIP_NONE,
    IMX2D_FLIP_HORIZONTAL,
    IMX2D_FLIP_VERTICAL,
    IMX2D_FLIP_BOTH,
};

int imx2d_flip(int src_type, const uchar* src_data, size_t src_step,
               int src_width, int src_height,
               uchar* dst_data, size_t dst_step, int flip_type);

inline int __imx2d_flip(int src_type, const uchar* src_data, size_t src_step,
                        int src_width, int src_height,
                        uchar* dst_data, size_t dst_step, int flip_mode)
{
    int ret, flip_type;

    if (flip_mode == 0)
        flip_type = IMX2D_FLIP_VERTICAL;
    else if (flip_mode > 0)
        flip_type = IMX2D_FLIP_HORIZONTAL;
    else
        flip_type = IMX2D_FLIP_BOTH;

    ret = imx2d_flip(src_type, src_data, src_step,
                     src_width, src_height,
                     dst_data, dst_step, flip_type);

    return ret;
}


#undef cv_hal_rotate
#define cv_hal_rotate __imx2d_rotate

enum {
    IMX2D_ROTATE_NONE,
    IMX2D_ROTATE_90,
    IMX2D_ROTATE_180,
    IMX2D_ROTATE_270,
};

int imx2d_rotate(int src_type, const uchar* src_data, size_t src_step,
                 int src_width, int src_height,
                 uchar* dst_data, size_t dst_step, int rotate_type);

inline int __imx2d_rotate(int src_type, const uchar* src_data, size_t src_step,
                          int src_width, int src_height,
                          uchar* dst_data, size_t dst_step, int angle)
{
    int ret, type;

    if (angle == 90)
        type = IMX2D_ROTATE_90;
    else if (angle == 180)
        type = IMX2D_ROTATE_180;
    else if (angle == 270)
        type = IMX2D_ROTATE_270;
    else
        type = IMX2D_ROTATE_NONE;

    ret = imx2d_rotate(src_type, src_data, src_step,
                       src_width, src_height,
                       dst_data, dst_step, type);

    return ret;
}



