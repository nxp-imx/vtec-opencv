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

#include "perf_precomp.hpp"
#include <opencv2/core/utils/logger.hpp>

#include "imx2d_common.hpp"

namespace opencv_test {

enum {FLIP_HORIZONTAL, FLIP_VERTICAL, FLIP_BOTH};
CV_ENUM(Flip_t, FLIP_HORIZONTAL, FLIP_VERTICAL, FLIP_BOTH);
enum {MATBUFFER_G2D_CACHED, MATBUFFER_G2D_UNCACHED, MATBUFFER_HEAP};
CV_ENUM(MatBuffer_t, MATBUFFER_G2D_CACHED, MATBUFFER_G2D_UNCACHED, MATBUFFER_HEAP);

typedef tuple<MatType, Size, Flip_t> MatInfo_Size_Flip_t;
typedef TestBaseWithParam<MatInfo_Size_Flip_t> MatInfo_Size_Flip;

typedef tuple<MatType, Size, Flip_t, MatBuffer_t> MatInfo_Size_Flip_MatBuffer_t;
typedef TestBaseWithParam<MatInfo_Size_Flip_MatBuffer_t> MatInfo_Size_Flip_MatBuffer;


#define PSNR_DB_MIN 30


static inline unsigned getFlipHalCount()
{
    imx2d::Imx2dHal& imxHal = imx2d::Imx2dHal::getInstance();
    imx2d::Imx2dHalCounters& counters = imxHal.counters;
    return counters.readCount(imx2d::Imx2dHalCounters::FLIP);
}

static inline bool hasCapThreeChannels()
{
    cv::imx2d::Imx2dHal& hal = cv::imx2d::Imx2dHal::getInstance();
    cv::imx2d::HardwareCapabilities& hwCaps = hal.getHardwareCapabilities();
    return hwCaps.hasCapability(cv::imx2d::HardwareCapabilities::THREE_CHANNELS);
}

// Benchmark full matrix IMX2D flip
PERF_TEST_P(MatInfo_Size_Flip_MatBuffer, imx2dFlipMatrix,
            testing::Combine(
                testing::Values(CV_8UC3, CV_8UC4),
                testing::Values(cv::Size(200, 200), szVGA, sz1080p, sz2160p),
                Flip_t::all(),
                MatBuffer_t::all()
                )
            )
{
    int matType = get<0>(GetParam());
    Size size = get<1>(GetParam());
    int flipType = get<2>(GetParam());
    int matBuffer = get<3>(GetParam());
    unsigned halCount;
    int flipCode;
    bool cpuFallback = ((matType == CV_8UC3) && !hasCapThreeChannels());

    // start with default state
    setUseImx2d(false);
    setUseGMatAllocator(false);

    bool useAllocator = (matBuffer != MATBUFFER_HEAP);
    bool cacheable = (matBuffer == MATBUFFER_G2D_CACHED);
    setUseImx2d(true);
    setGMatAllocatorParams(GMatAllocatorParams(0, cacheable));
    setUseGMatAllocator(useAllocator);

    Mat src(size, matType);
    cvtest::fillGradient(src);
    Mat dst = Mat::zeros(size, matType);

    if (flipType == FLIP_HORIZONTAL)
        flipCode = 1;
    else if (flipType == FLIP_VERTICAL)
        flipCode = 0;
    else // flipType == FLIP_BOTH
        flipCode = -1;

    declare.in(src).out(dst);

    halCount = getFlipHalCount();
    TEST_CYCLE_N(20)
    {
        if (!cpuFallback)
        {
            flip(src, dst, flipCode);
            halCount++;
        }
    }
    ASSERT_EQ(getFlipHalCount(), halCount);

    if (cpuFallback)
        flip(src, dst, flipCode); // for output PSNR check in fallback case

    setUseImx2d(false);
    setUseGMatAllocator(false);

    // Compare accelerated flip() with CPU version
    Mat golden;
    flip(src, golden, flipCode);
    ASSERT_EQ(getFlipHalCount(), halCount);

    double psnr = cv::PSNR(dst, golden, cv::norm(golden, NORM_INF));
    CV_LOG_DEBUG(NULL, "PSNR:" << psnr);
    ASSERT_GE(psnr, PSNR_DB_MIN);

    SANITY_CHECK_NOTHING();
}

// Benchmark sub matrix IMX2D flip
PERF_TEST_P(MatInfo_Size_Flip_MatBuffer, imx2dFlipSubmatrix,
            testing::Combine(
                testing::Values(CV_8UC3, CV_8UC4),
                testing::Values(cv::Size(200, 200), szVGA, sz1080p, sz2160p),
                Flip_t::all(),
                MatBuffer_t::all()
                )
            )
{
    int matType = get<0>(GetParam());
    Size size = get<1>(GetParam());
    int flipType = get<2>(GetParam());
    int matBuffer = get<3>(GetParam());
    unsigned halCount;
    int flipCode;
    bool cpuFallback = ((matType == CV_8UC3) && !hasCapThreeChannels());

    // start with default state
    setUseImx2d(false);
    setUseGMatAllocator(false);

    bool useAllocator = (matBuffer != MATBUFFER_HEAP);
    bool cacheable = (matBuffer == MATBUFFER_G2D_CACHED);
    setUseImx2d(true);
    setGMatAllocatorParams(GMatAllocatorParams(0, cacheable));
    setUseGMatAllocator(useAllocator);

    Mat src(size, matType);
    cvtest::fillGradient(src);
    Mat dst = Mat::zeros(Size(size.width / 4, size.height / 4), matType);

    cv::Rect r(size.width / 4, size.height / 4, size.width / 4, size.height / 4); // Rect(x, y, width, height)
    Mat sub = src(r);

    if (flipType == FLIP_HORIZONTAL)
        flipCode = 1;
    else if (flipType == FLIP_VERTICAL)
        flipCode = 0;
    else // flipType == FLIP_BOTH
        flipCode = -1;

    declare.in(src).out(dst);

    halCount = getFlipHalCount();
    TEST_CYCLE_N(20)
    {
        if (!cpuFallback)
        {
            flip(sub, dst, flipCode);
            halCount++;
        }
    }
    ASSERT_EQ(getFlipHalCount(), halCount);

    if (cpuFallback)
        flip(sub, dst, flipCode); // for output PSNR check in fallback case

    setUseImx2d(false);
    setUseGMatAllocator(false);

    // Compare accelerated flip() with CPU version
    Mat golden;
    flip(sub, golden, flipCode);
    ASSERT_EQ(getFlipHalCount(), halCount);

    double psnr = cv::PSNR(dst, golden, cv::norm(golden, NORM_INF));
    CV_LOG_DEBUG(NULL, "PSNR:" << psnr);
    ASSERT_GE(psnr, PSNR_DB_MIN);

    SANITY_CHECK_NOTHING();
}


// Benchmarks execution of CPU based resize algorithms
PERF_TEST_P(MatInfo_Size_Flip, cpuFlipAllModes,
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

    // start with default state
    setUseImx2d(false);
    setUseGMatAllocator(false);

    Mat src(size, matType), dst(size, matType);
    cvtest::fillGradient(src);
    declare.in(src).out(dst);

    if (flipType == FLIP_HORIZONTAL)
        flipCode = 1;
    else if (flipType == FLIP_VERTICAL)
        flipCode = 0;
    else // flipType == FLIP_BOTH
        flipCode = -1;

    TEST_CYCLE_N(10)
    {
        flip(src, dst, flipCode);
    }

    SANITY_CHECK_NOTHING();
}


} // namespace
