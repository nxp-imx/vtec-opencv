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

#ifndef __OPENCV_PERF_PRECOMP_HPP__
#define __OPENCV_PERF_PRECOMP_HPP__

#define IMX2D_PERF_G2D_BENCHMARK
//#define IMX2D_PERF_MAT_ACCESS_BENCHMARK
#define IMX2D_PERF_CPU_BENCHMARK
#define IMX2D_PERF_OCL_BENCHMARK
//#define IMX2D_DUMP_PNG

#include "opencv2/imx2d.hpp"
#include "opencv2/ts.hpp"
#include "opencv2/imgproc.hpp"

namespace opencv_test {
using namespace perf;
using namespace cv;
using namespace cv::imx2d;
}

#endif
