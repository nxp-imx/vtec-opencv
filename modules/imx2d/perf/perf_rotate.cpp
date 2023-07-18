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

#define DEBUG

#include "imx2d_common.hpp"

namespace opencv_test {

CV_ENUM(Rotate_t, ROTATE_90_CLOCKWISE, ROTATE_180, ROTATE_90_COUNTERCLOCKWISE);
enum {MATBUFFER_G2D_CACHED, MATBUFFER_G2D_UNCACHED, MATBUFFER_HEAP};
CV_ENUM(MatBuffer_t, MATBUFFER_G2D_CACHED, MATBUFFER_G2D_UNCACHED, MATBUFFER_HEAP);

typedef tuple<MatType, Size, Rotate_t> MatInfo_Size_Rotate_t;
typedef TestBaseWithParam<MatInfo_Size_Rotate_t> MatInfo_Size_Rotate;

typedef tuple<MatType, Size, Rotate_t, MatBuffer_t> MatInfo_Size_Rotate_MatBuffer_t;
typedef TestBaseWithParam<MatInfo_Size_Rotate_MatBuffer_t> MatInfo_Size_Rotate_MatBuffer;

#define PSNR_DB_MIN 30


static inline unsigned getRotateHalCount()
{
    imx2d::Imx2dHal& imxHal = imx2d::Imx2dHal::getInstance();
    imx2d::Imx2dHalCounters& counters = imxHal.counters;
    return counters.readCount(imx2d::Imx2dHalCounters::ROTATE);
}

static inline bool hasCapThreeChannels()
{
    cv::imx2d::Imx2dHal& hal = cv::imx2d::Imx2dHal::getInstance();
    cv::imx2d::HardwareCapabilities& hwCaps = hal.getHardwareCapabilities();
    return hwCaps.hasCapability(cv::imx2d::HardwareCapabilities::THREE_CHANNELS);
}

// Benchmark full matrix IMX2D rotate
PERF_TEST_P(MatInfo_Size_Rotate_MatBuffer, imx2dRotateMatrix,
            testing::Combine(
                testing::Values(CV_8UC3, CV_8UC4),
                testing::Values(cv::Size(200, 200), szVGA, sz1080p, sz2160p),
                Rotate_t::all(),
                MatBuffer_t::all()
                )
            )
{
    int matType = get<0>(GetParam());
    Size size = get<1>(GetParam());
    int rotateCode = get<2>(GetParam());
    int matBuffer = get<3>(GetParam());
    unsigned halCount;
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
    Mat dst;
    switch (rotateCode)
    {
    case ROTATE_90_CLOCKWISE:
    case ROTATE_90_COUNTERCLOCKWISE:
        dst = Mat::zeros(Size(size.height, size.width), matType);
        break;
    default:
        dst = Mat::zeros(size, matType);
        break;
    }

    declare.in(src).out(dst);

    halCount = getRotateHalCount();
    TEST_CYCLE_N(20)
    {
        if (!cpuFallback)
        {
            rotate(src, dst, rotateCode);
            halCount++;
        }
    }
    ASSERT_EQ(getRotateHalCount(), halCount);

    if (cpuFallback)
        rotate(src, dst, rotateCode); // for output PSNR check in fallback case

    setUseImx2d(false);
    setUseGMatAllocator(false);

    // Compare accelerated flip() with CPU version
    Mat golden;
    rotate(src, golden, rotateCode);
    ASSERT_EQ(getRotateHalCount(), halCount);

    double psnr = cv::PSNR(dst, golden, cv::norm(golden, NORM_INF));
    CV_LOG_DEBUG(NULL, "PSNR:" << psnr);
    ASSERT_GE(psnr, PSNR_DB_MIN);

    SANITY_CHECK_NOTHING();
}


// Benchmark full matrix IMX2D rotate
PERF_TEST_P(MatInfo_Size_Rotate_MatBuffer, imx2dRotateSubMatrix,
            testing::Combine(
                testing::Values(CV_8UC3, CV_8UC4),
                testing::Values(cv::Size(200, 200), szVGA, sz1080p, sz2160p),
                Rotate_t::all(),
                MatBuffer_t::all()
                )
            )
{
    int matType = get<0>(GetParam());
    Size size = get<1>(GetParam());
    int rotateCode = get<2>(GetParam());
    int matBuffer = get<3>(GetParam());
    unsigned halCount;
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
    Mat dst;
    switch (rotateCode)
    {
    case ROTATE_90_CLOCKWISE:
    case ROTATE_90_COUNTERCLOCKWISE:
        dst = Mat::zeros(Size(size.height / 4, size.width / 4), matType);
        break;
    default:
        dst = Mat::zeros(size, matType);
        break;
    }

    cv::Rect r(size.width / 4, size.height / 4, size.width / 4, size.height / 4); // Rect(x, y, width, height)
    Mat sub = src(r);

    declare.in(src).out(dst);

    halCount = getRotateHalCount();
    TEST_CYCLE_N(20)
    {
        if (!cpuFallback)
        {
            rotate(sub, dst, rotateCode);
            halCount++;
        }
    }
    ASSERT_EQ(getRotateHalCount(), halCount);

    if (cpuFallback)
        rotate(sub, dst, rotateCode); // for output PSNR check in fallback case

    setUseImx2d(false);
    setUseGMatAllocator(false);

    // Compare accelerated flip() with CPU version
    Mat golden;
    rotate(sub, golden, rotateCode);
    ASSERT_EQ(getRotateHalCount(), halCount);

    double psnr = cv::PSNR(dst, golden, cv::norm(golden, NORM_INF));
    CV_LOG_DEBUG(NULL, "PSNR:" << psnr);
    ASSERT_GE(psnr, PSNR_DB_MIN);

    SANITY_CHECK_NOTHING();
}


// Benchmarks execution of CPU based rotate algorithms
PERF_TEST_P(MatInfo_Size_Rotate, cpuRotateAllModes,
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

    // start with default state
    setUseImx2d(false);
    setUseGMatAllocator(false);

    Mat src(size, matType), dst(size, matType);
    cvtest::fillGradient(src);
    declare.in(src).out(dst);

    TEST_CYCLE_N(10)
    {
        rotate(src, dst, rotateCode);
    }

    SANITY_CHECK_NOTHING();
}


} // namespace
