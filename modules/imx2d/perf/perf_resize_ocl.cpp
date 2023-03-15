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
#include "opencv2/ts/ocl_perf.hpp"
#include "opencv2/core/utils/logger.hpp"


#ifdef HAVE_OPENCL

namespace opencv_test {
namespace ocl {

CV_ENUM(Inter, INTER_NEAREST, INTER_LINEAR, INTER_CUBIC, INTER_LANCZOS4);

typedef tuple<Size,Size> Size_Size_t;
typedef tuple<MatType, Size_Size_t, Inter> MatInfo_Pair_Inter_t;
typedef TestBaseWithParam<MatInfo_Pair_Inter_t> MatInfo_Pair_Inter;

#ifdef IMX2D_PERF_OCL_BENCHMARK
// Benchmarks execution of OCL based resize algorithms
OCL_PERF_TEST_P(MatInfo_Pair_Inter, oclResizeAllInterpolations,
                testing::Combine(
                    testing::Values(CV_8UC3, CV_8UC4),
                    testing::Values(Size_Size_t(szVGA, sz1080p),
                                    Size_Size_t(sz1080p, sz2160p),
                                    Size_Size_t(sz1080p, szVGA),
                                    Size_Size_t(szVGA, cv::Size(200, 200))),
                    Inter::all()
                    )
                )
{
    int matType = get<0>(GetParam());
    Size_Size_t sizes = get<1>(GetParam());
    Size from = get<0>(sizes);
    Size to = get<1>(sizes);
    int inter = get<2>(GetParam());

    Mat src(from, matType);
    cvtest::fillGradient(src);
    UMat usrc = src.getUMat(ACCESS_WRITE);
    UMat udst(to, matType);

    OCL_TEST_CYCLE_N(10)
    {
        resize(usrc, udst, to, 0, 0, inter);
    }

    SANITY_CHECK_NOTHING();
}
#endif // IMX2D_PERF_OCL_BENCHMARK


}}

#endif // HAVE_OPENCL