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

#define DEBUG
#ifdef DEBUG
#define CV_LOG_STRIP_LEVEL (CV_LOG_LEVEL_VERBOSE + 1)
#endif

#include "perf_precomp.hpp"
#include "opencv2/core/utils/logger.hpp"

#include "imx2d_common.hpp"
namespace opencv_test {

CV_ENUM(Inter_t, INTER_NEAREST, INTER_LINEAR, INTER_CUBIC, INTER_LANCZOS4);
enum {MATBUFFER_G2D_CACHED, MATBUFFER_G2D_UNCACHED, MATBUFFER_HEAP};
CV_ENUM(MatBuffer_t, MATBUFFER_G2D_CACHED, MATBUFFER_G2D_UNCACHED, MATBUFFER_HEAP);

typedef tuple<Size,Size> Size_Size_t;
typedef TestBaseWithParam<Size_Size_t> Size_Size;

typedef tuple<MatType, Size_Size_t, Inter_t> MatInfo_Pair_Inter_t;
typedef TestBaseWithParam<MatInfo_Pair_Inter_t> MatInfo_Pair_Inter;

typedef tuple<MatType, Size_Size_t, MatBuffer_t> MatInfo_Pair_MatBuffer_t;
typedef TestBaseWithParam<MatInfo_Pair_MatBuffer_t> MatInfo_Pair_MatBuffer;

typedef tuple<MatType, Size_Size_t, bool> MatInfo_Pair_Bool_t;
typedef TestBaseWithParam<MatInfo_Pair_Bool_t> MatInfo_Pair_Bool;

#define PSNR_DB_MIN 30


static inline unsigned getResizeHalCount()
{
    imx2d::Imx2dHal& imxHal = imx2d::Imx2dHal::getInstance();
    imx2d::Imx2dHalCounters& counters = imxHal.counters;
    return counters.readCount(imx2d::Imx2dHalCounters::RESIZE);
}

#ifdef IMX2D_PERF_G2D_BENCHMARK

// Benchmark full matrix IMX2D resize
PERF_TEST_P(MatInfo_Pair_MatBuffer, imx2dResizeMatrix,
            testing::Combine(
                testing::Values(CV_8UC3, CV_8UC4),
                testing::Values(Size_Size_t(szVGA, sz1080p),
                                Size_Size_t(sz1080p, sz2160p),
                                Size_Size_t(sz1080p, szVGA),
                                Size_Size_t(szVGA, cv::Size(200, 200))),
                MatBuffer_t::all()
                )
            )
{
    int matType = get<0>(GetParam());
    Size_Size_t sizes = get<1>(GetParam());
    Size from = get<0>(sizes);
    Size to = get<1>(sizes);
    int matBuffer = get<2>(GetParam());
    unsigned halCount;

    bool useAllocator = (matBuffer != MATBUFFER_HEAP);
    bool cacheable = (matBuffer == MATBUFFER_G2D_CACHED);
    setUseImx2d(true);
    setGMatAllocatorParams(GMatAllocatorParams(0, cacheable));
    setUseGMatAllocator(useAllocator);

    Mat src(from, matType);
    cvtest::fillGradient(src);
    Mat dst = Mat::zeros(to, matType);

    declare.in(src).out(dst);

    halCount = getResizeHalCount();
    TEST_CYCLE_N(20)
    {
        resize(src, dst, to, 0, 0, INTER_LINEAR);
        halCount++;
    }
    ASSERT_EQ(getResizeHalCount(), halCount);

    setUseImx2d(false);
    setUseGMatAllocator(false);

    // Compare accelerated resize() with CPU version
    Mat golden;
    resize(src, golden, to, 0, 0, INTER_LANCZOS4);
    ASSERT_EQ(getResizeHalCount(), halCount);

    double psnr = cv::PSNR(dst, golden, cv::norm(golden, NORM_INF));
    CV_LOG_DEBUG(NULL, "PSNR:" << psnr);
    ASSERT_GE(psnr, PSNR_DB_MIN);

    SANITY_CHECK_NOTHING();
}

// Benchmark sub matrix IMX2D resize
PERF_TEST_P(MatInfo_Pair_MatBuffer, imx2dResizeSubmatrix,
            testing::Combine(
                testing::Values(CV_8UC3, CV_8UC4),
                testing::Values(Size_Size_t(szVGA, sz1080p),
                                Size_Size_t(sz1080p, sz2160p),
                                Size_Size_t(sz1080p, szVGA),
                                Size_Size_t(szVGA, cv::Size(200, 200))),
                MatBuffer_t::all()
                )
            )
{
    int matType = get<0>(GetParam());
    Size_Size_t sizes = get<1>(GetParam());
    Size from = get<0>(sizes);
    Size to = get<1>(sizes);
    int matBuffer = get<2>(GetParam());
    unsigned halCount;

    bool useAllocator = (matBuffer != MATBUFFER_HEAP);
    bool cacheable = (matBuffer == MATBUFFER_G2D_CACHED);
    setUseImx2d(true);
    setGMatAllocatorParams(GMatAllocatorParams(0, cacheable));
    setUseGMatAllocator(useAllocator);

    Mat src(from, matType);
    cvtest::fillGradient(src);
    Mat dst = Mat::zeros(to, matType);

    cv::Rect r(from.width / 4, from.height / 4, from.width / 4, from.height / 4); // Rect(x, y, width, height)
    Mat sub = src(r);

    declare.in(sub).out(dst);

    halCount = getResizeHalCount();
    TEST_CYCLE_N(20)
    {
        resize(sub, dst, to, 0, 0, INTER_LINEAR);
        halCount++;
    }
    ASSERT_EQ(getResizeHalCount(), halCount);

    setUseImx2d(false);
    setUseGMatAllocator(false);

    // Compare accelerated resize() with CPU version
    Mat golden;
    resize(sub, golden, to, 0, 0, INTER_LANCZOS4);
    ASSERT_EQ(getResizeHalCount(), halCount);

    double psnr = cv::PSNR(dst, golden, cv::norm(golden, NORM_INF));
    CV_LOG_DEBUG(NULL, "PSNR:" << psnr);
    ASSERT_GE(psnr, PSNR_DB_MIN);

    SANITY_CHECK_NOTHING();
}
#endif

#ifdef IMX2D_PERF_MAT_ACCESS_BENCHMARK

// Benchmark copy from system heap to G2D buffer
PERF_TEST_P(MatInfo_Pair_MatBuffer, imx2dCopyHeapToMat,
            testing::Combine(
                testing::Values(CV_8UC3, CV_8UC4),
                testing::Values(Size_Size_t(szVGA, szVGA),
                                Size_Size_t(sz1080p, sz1080p),
                                Size_Size_t(sz2160p, sz2160p),
                                Size_Size_t(szVGA, cv::Size(200, 200))),
                MatBuffer_t::all()
                )
            )
{
    int matType = get<0>(GetParam());
    Size_Size_t sizes = get<1>(GetParam());
    Size from = get<0>(sizes);
    int matBuffer = get<2>(GetParam());
    bool useAllocator = (matBuffer != MATBUFFER_HEAP);
    bool cacheable = (matBuffer == MATBUFFER_G2D_CACHED);
    int cn = CV_MAT_CN(matType);

    void *psrc = malloc(from.height * from.width * cn);
    Mat src(from.height, from.width, matType, psrc, from.width * cn);

    setGMatAllocatorParams(GMatAllocatorParams(0, cacheable));
    setUseGMatAllocator(useAllocator);

    Mat dst(from, matType);

    declare.in(src).out(dst);

    TEST_CYCLE_N(20)
    {
        src.copyTo(dst);
    }

    setUseGMatAllocator(false);

    free(psrc);

    SANITY_CHECK_NOTHING();
}

PERF_TEST_P(MatInfo_Pair_MatBuffer, imx2dCopyMatToHeap,
            testing::Combine(
                testing::Values(CV_8UC3, CV_8UC4),
                testing::Values(Size_Size_t(szVGA, szVGA),
                                Size_Size_t(sz1080p, sz1080p),
                                Size_Size_t(sz2160p, sz2160p),
                                Size_Size_t(szVGA, cv::Size(200, 200))),
                MatBuffer_t::all()
                )
            )
{
    int matType = get<0>(GetParam());
    Size_Size_t sizes = get<1>(GetParam());
    Size from = get<0>(sizes);
    int matBuffer = get<2>(GetParam());
    bool useAllocator = (matBuffer != MATBUFFER_HEAP);
    bool cacheable = (matBuffer == MATBUFFER_G2D_CACHED);
    int cn = CV_MAT_CN(matType);

    void *pdst = malloc(from.height * from.width * cn);
    Mat dst(from.height, from.width, matType, pdst, from.width * cn);

    setGMatAllocatorParams(GMatAllocatorParams(0, cacheable));
    setUseGMatAllocator(useAllocator);

    Mat src(from, matType);

    declare.in(src).out(dst);

    TEST_CYCLE_N(20)
    {
        src.copyTo(dst);
    }

    setUseGMatAllocator(false);

    free(pdst);

    SANITY_CHECK_NOTHING();
}

PERF_TEST_P(MatInfo_Pair_MatBuffer, imx2dMatPlusOne,
            testing::Combine(
                testing::Values(CV_8UC3, CV_8UC4),
                testing::Values(Size_Size_t(szVGA, szVGA),
                                Size_Size_t(sz1080p, sz1080p),
                                Size_Size_t(sz2160p, sz2160p),
                                Size_Size_t(szVGA, cv::Size(200, 200))),
                MatBuffer_t::all()
                )
            )
{
    int matType = get<0>(GetParam());
    Size_Size_t sizes = get<1>(GetParam());
    Size from = get<0>(sizes);
    int matBuffer = get<2>(GetParam());
    bool useAllocator = (matBuffer != MATBUFFER_HEAP);
    bool cacheable = (matBuffer == MATBUFFER_G2D_CACHED);
    int cn = CV_MAT_CN(matType);

    setGMatAllocatorParams(GMatAllocatorParams(0, cacheable));
    setUseGMatAllocator(useAllocator);

    Mat src(from, matType);

    Scalar s;
    if (cn == 3)
        s = Scalar(1,1,1);
    else if (cn == 4)
        s = Scalar(1,1,1,1);
    else
        s = Scalar(1);

    declare.in(src).out(src);

    TEST_CYCLE_N(20)
    {
        src += s;
    }

    setUseGMatAllocator(false);

    SANITY_CHECK_NOTHING();
}

#endif

#ifdef IMX2D_PERF_CPU_BENCHMARK

// Benchmark CPU time needed for 3 chans / 4 chans Mat conversions
// this is to evaluate 3 channels support overhead for CPU-based CSC
PERF_TEST_P(MatInfo_Pair_MatBuffer, cpuCscBench,
            testing::Combine(
                testing::Values(CV_8UC3),
                testing::Values(Size_Size_t(szVGA, sz1080p),
                                Size_Size_t(sz1080p, sz2160p),
                                Size_Size_t(sz1080p, szVGA),
                                Size_Size_t(szVGA, cv::Size(200, 200))),
                MatBuffer_t::all()
                )
            )
{
    int matType = get<0>(GetParam());
    CV_UNUSED(matType);

    Size_Size_t sizes = get<1>(GetParam());
    Size from = get<0>(sizes);
    Size to = get<1>(sizes);
    int matBuffer = get<2>(GetParam());

    bool useAllocator = (matBuffer != MATBUFFER_HEAP);
    bool cacheable = (matBuffer == MATBUFFER_G2D_CACHED);

    setUseImx2d(true);
    setGMatAllocatorParams(GMatAllocatorParams(0, cacheable));
    setUseGMatAllocator(useAllocator);

    Mat src3(from, CV_8UC3);
    cvtest::fillGradient(src3);
    Mat src4;

    Mat dst4(to, CV_8UC4);
    cvtest::fillGradient(dst4);
    Mat dst3;

    declare.in(src3, dst4);

    TEST_CYCLE_N(20)
    {
        cvtColor(src3, src4, cv::COLOR_BGR2BGRA);
        cvtColor(dst4, dst3, cv::COLOR_BGRA2BGR);
    }

    setUseImx2d(false);
    setUseGMatAllocator(false);

    SANITY_CHECK_NOTHING();
}



// Benchmarks execution of CPU based resize algorithms
PERF_TEST_P(MatInfo_Pair_Inter, cpuResizeAllInterpolations,
            testing::Combine(
                testing::Values(CV_8UC3, CV_8UC4),
                testing::Values(Size_Size_t(szVGA, sz1080p),
                                Size_Size_t(sz1080p, sz2160p),
                                Size_Size_t(sz1080p, szVGA),
                                Size_Size_t(szVGA, cv::Size(200, 200))),
                Inter_t::all()
                )
            )
{
    int matType = get<0>(GetParam());
    Size_Size_t sizes = get<1>(GetParam());
    Size from = get<0>(sizes);
    Size to = get<1>(sizes);
    int inter = get<2>(GetParam());

    setUseImx2d(false);
    setUseGMatAllocator(false);

    Mat src(from, matType), dst(to, matType);
    cvtest::fillGradient(src);
    declare.in(src).out(dst);

    TEST_CYCLE_N(10)
    {
        resize(src, dst, to, 0, 0, inter);
    }

    SANITY_CHECK_NOTHING();
}

#endif // IMX2D_PERF_CPU_BENCHMARK


#ifdef IMX2D_DUMP_PNG
PERF_TEST_P(MatInfo_Pair_Bool, imx2dResizeUpLena,
            testing::Combine(
                testing::Values(CV_8UC4),
                testing::Values(Size_Size_t(szVGA, sz2160p)),
                testing::Values(true, false)
                )
            )
{
    int matType = get<0>(GetParam());
    Size_Size_t sizes = get<1>(GetParam());
    Size from = get<0>(sizes);
    Size to = get<1>(sizes);
    bool imx2d = get<2>(GetParam());

    setUseImx2d(imx2d)
    GMatAllocatorParams(GMatAllocatorParams(0, true));
    setUseGMatAllocator(imx2d);

    Mat lenabgr, lenabgra;
    lenabgr = imread(getDataPath("cv/shared/lena.png"), IMREAD_COLOR);
    cvtColor(lenabgr, lenabgra, COLOR_BGR2BGRA);

    Mat dst(to, matType);

    declare.in(lenabgra).out(dst);

    TEST_CYCLE_N(1)
    {
        String name;

        resize(lenabgra, dst, to, 0, 0, INTER_NEAREST);
        name = imx2d ? "lenaimx2d.png" : "lenacpunearest.png";
        imwrite(name, dst);

        if (!imx2d)
        {
            resize(lenabgra, dst, to, 0, 0, INTER_LINEAR);
            name = "lenacpulinear.png";
            imwrite(name, dst);

            resize(lenabgra, dst, to, 0, 0, INTER_CUBIC);
            name = "lenacpucubic.png";
            imwrite(name, dst);

            resize(lenabgra, dst, to, 0, 0, INTER_AREA);
            name = "lenacpuarea.png";
            imwrite(name, dst);

            resize(lenabgra, dst, to, 0, 0, INTER_LANCZOS4);
            name = "lenacpulanczos.png";
            imwrite(name, dst);
        }
    }

    setUseImx2d(false);
    setUseGMatAllocator(false);

    SANITY_CHECK_NOTHING();
}
#endif // IMX2D_DUMP_PNG

} // namespace
