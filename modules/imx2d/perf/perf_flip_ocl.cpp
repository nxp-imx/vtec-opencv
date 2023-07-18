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

#include "perf_precomp.hpp"
#include <opencv2/ts/ocl_perf.hpp>
#include <opencv2/core/utils/logger.hpp>


#ifdef HAVE_OPENCL

namespace opencv_test {
namespace ocl {

enum {FLIP_HORIZONTAL, FLIP_VERTICAL, FLIP_BOTH};
CV_ENUM(Flip_t, FLIP_HORIZONTAL, FLIP_VERTICAL, FLIP_BOTH);

typedef tuple<MatType, Size, Flip_t> MatInfo_Size_Flip_t;
typedef TestBaseWithParam<MatInfo_Size_Flip_t> MatInfo_Size_Flip;

#ifdef IMX2D_PERF_OCL_BENCHMARK
// Benchmarks execution of OCL based flip algorithms
OCL_PERF_TEST_P(MatInfo_Size_Flip, oclFlipAllModes,
                testing::Combine(
                    testing::Values(CV_8UC3, CV_8UC4),
                    testing::Values(cv::Size(200, 200), szVGA, sz1080p, sz2160p),
                    Flip_t::all()
                    )
                )
{
    int matType = get<0>(GetParam());
    Size size = get<1>(GetParam());
    int flipType = get<2>(GetParam());
    int flipCode;

    Mat src(size, matType);
    cvtest::fillGradient(src);
    UMat usrc = src.getUMat(ACCESS_WRITE);
    UMat udst(size, matType);

    if (flipType == FLIP_HORIZONTAL)
        flipCode = 1;
    else if (flipType == FLIP_VERTICAL)
        flipCode = 0;
    else // flipType == FLIP_BOTH
        flipCode = -1;

    OCL_TEST_CYCLE_N(10)
    {
        flip(usrc, udst, flipCode);
    }

    SANITY_CHECK_NOTHING();
}

#endif // IMX2D_PERF_OCL_BENCHMARK


}}

#endif // HAVE_OPENCL