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

CV_ENUM(Rotate_t, ROTATE_90_CLOCKWISE, ROTATE_180, ROTATE_90_COUNTERCLOCKWISE);

typedef tuple<MatType, Size, Rotate_t> MatInfo_Size_Rotate_t;
typedef TestBaseWithParam<MatInfo_Size_Rotate_t> MatInfo_Size_Rotate;

#ifdef IMX2D_PERF_OCL_BENCHMARK
// Benchmarks execution of OCL based rotate algorithms
OCL_PERF_TEST_P(MatInfo_Size_Rotate, oclRotateAllModes,
                testing::Combine(
                    testing::Values(CV_8UC3, CV_8UC4),
                    testing::Values(cv::Size(200, 200), szVGA, sz1080p, sz2160p),
                    Rotate_t::all()
                    )
                )
{
    int matType = get<0>(GetParam());
    Size size = get<1>(GetParam());
    int rotateCode = get<2>(GetParam());

    Mat src(size, matType);
    cvtest::fillGradient(src);
    UMat usrc = src.getUMat(ACCESS_WRITE);
    UMat udst(size, matType);

    OCL_TEST_CYCLE_N(10)
    {
        rotate(usrc, udst, rotateCode);
    }

    SANITY_CHECK_NOTHING();
}
#endif // IMX2D_PERF_OCL_BENCHMARK


}}

#endif // HAVE_OPENCL