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

#include "test_precomp.hpp"
#include "opencv2/core/ocl.hpp"

#include "imx2d_common.hpp" // cache snoop

namespace opencv_test { namespace {

class Imx2dBase : public cvtest::BaseTest
{
public:
    void preamble()
    {
        // - check no IMX2D memory is allocated
        // - refresh past allocations counter
        EXPECT_EQ(stats.getCurrentUsage(), 0ULL);
    }

    void postamble()
    {
        // - check no IMX2D memory is allocated
        // - disable MatAllocator
        EXPECT_EQ(stats.getCurrentUsage(), 0ULL);
        setUseGMatAllocator(false);
    }

    Imx2dBase() : stats(getGMatAllocatorStats())
    {
        allocs = stats.getNumberOfAllocations();
    }

    ~Imx2dBase() {}

protected:
    virtual void run(int) {};
    uint64_t allocs;
    cv::utils::AllocatorStatisticsInterface& stats;
};


class Imx2dMatSingleAlloc : public Imx2dBase
{
public:
    Imx2dMatSingleAlloc(unsigned _cols, unsigned _rows,
                        bool _allocator, size_t _minSize, bool _cacheable) :
                cols(_cols), rows(_rows),
                allocator(_allocator), minSize(_minSize),
                cacheable(_cacheable) {
                    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_VERBOSE);
                }
protected:
    void run(int);
    unsigned cols;
    unsigned rows;
    bool allocator;
    size_t minSize;
    bool cacheable;
};

void Imx2dMatSingleAlloc::run(int)
{
    Mat* m;

    preamble();

    // imx2d buffer allocated for Mat instanciation
    // only if allocator selected and buffer is big enough
    bool custom_allocated = (allocator && (rows * cols * 3 >= minSize));
    uint64_t _allocs = allocs + (custom_allocated ? 1 : 0);

    setGMatAllocatorParams(GMatAllocatorParams(minSize, cacheable));
    setUseGMatAllocator(allocator);

    // Allocate rows x cols buffer
    allocs = stats.getNumberOfAllocations();
    m = new Mat(rows, cols, CV_8UC3);
    EXPECT_EQ(stats.getNumberOfAllocations(), _allocs);
    EXPECT_EQ((stats.getCurrentUsage() > 0), custom_allocated);
    delete m;
    EXPECT_EQ(stats.getNumberOfAllocations(), _allocs);
    ASSERT_EQ(stats.getCurrentUsage(), 0ULL);

    postamble();
}

TEST(CV_Imx2dMat, singleAllocStd) {
    // cols, row, allocator, minSize, cacheable
    // 640x480 buffer - imx2d disabled -> allocated on the heap
    Imx2dMatSingleAlloc test(640, 480, false, 320*200*3, false);
    test.safe_run();
}

TEST(CV_Imx2dMat, singleAllocImx2dTooSmall) {
    // cols, row, allocator, minSize, cacheable
    // 640x480 buffer - imx2d enabled, allocator enabled, too small -> heap
    Imx2dMatSingleAlloc test(320, 199, true, 320*200*3, false);
    test.safe_run();
}

TEST(CV_Imx2dMat, singleAllocImx2d) {
    // cols, row, allocator, minSize, cacheable
    // 640x480 buffer - imx2d enabled, allocator enabled -> allocated on G2D
    Imx2dMatSingleAlloc test(320, 200, true, 320*200*3, false);
    test.safe_run();
}

TEST(CV_Imx2dMat, singleAllocStd2) {
    // cols, row, allocator, minSize, cacheable
    // same as singleAllocStd... make sure MatAllocator can be disabled!
    Imx2dMatSingleAlloc test(640, 480, false, 320*200*3, false);
    test.safe_run();
}



class Imx2dMatMaxAllocs : public Imx2dBase
{
protected:
    void run(int);
};

void Imx2dMatMaxAllocs::run(int)
{
    Mat* m;
    unsigned count = 0;
    std::vector<Mat*> v;

    preamble();

    // Enable allocator with default buffer size threshold
    setGMatAllocatorParams(GMatAllocatorParams(0, true));
    setUseGMatAllocator(true);

    // Allocate then free as many HD buffer as possible
    while (true) {
        m = new Mat(1080, 1920, CV_8UC3);
        v.push_back(m);
        if (stats.getNumberOfAllocations() != allocs + count + 1)
            break; // allocated on the heap - abort
        count++;
    }
    CV_LOG_DEBUG(NULL, "Allocated [" << count << "] HD buffers");
    EXPECT_GE(count , 1ULL);

    // Deallocate every allocated buffers
    for (std::vector<Mat*>::iterator it = v.begin(); it != v.end(); ++it)
    {
        EXPECT_EQ(stats.getCurrentUsage(), count * 1920 * 1080 * 3);
        delete *it;
        count--;
    }
    v.clear();

    postamble();
}

TEST(CV_Imx2dMat, maxAllocsHD) {
    Imx2dMatMaxAllocs test;
    test.safe_run();
}


class Imx2dMatCopy : public Imx2dBase
{
protected:
    void run(int);
};

void Imx2dMatCopy::run(int)
{
    Mat* m, * mref;
    uint64_t usage;

    preamble();

    // Enable allocator with default buffer size threshold
    setGMatAllocatorParams(GMatAllocatorParams(0, true));
    setUseGMatAllocator(true);

    mref = new Mat(480, 640, CV_8UC3);
    EXPECT_EQ(stats.getNumberOfAllocations(), allocs + 1);
    allocs = stats.getNumberOfAllocations();
    usage = stats.getCurrentUsage();

    // shallow copy
    allocs = stats.getNumberOfAllocations();
    m = new Mat(*mref);
    EXPECT_EQ(stats.getNumberOfAllocations(), allocs);
    delete m;
    EXPECT_EQ(stats.getNumberOfAllocations(), allocs);

    // deep copy
    allocs = stats.getNumberOfAllocations();
    m = new Mat;
    mref->copyTo(*m);
    EXPECT_EQ(stats.getNumberOfAllocations(), allocs + 1);
    EXPECT_GE(stats.getCurrentUsage(), usage);
    delete m;
    EXPECT_EQ(stats.getCurrentUsage(), usage);

    // UMat constructed from Mat
    allocs = stats.getNumberOfAllocations();
    cv::ocl::setUseOpenCL(false);
    UMat* um;
    um = new UMat;
    *um = mref->getUMat(ACCESS_RW);
    // UMat uses same Mat buffer for host
    EXPECT_EQ(stats.getNumberOfAllocations(), allocs);
     m = new Mat;
    // Reuse same Mat buffer
    *m = um->getMat(ACCESS_RW);
    EXPECT_EQ(stats.getNumberOfAllocations(), allocs);
    delete m;
    delete um;
    EXPECT_EQ(stats.getNumberOfAllocations(), allocs);
    EXPECT_EQ(stats.getCurrentUsage(), usage);

    // clean up
    delete mref;

    postamble();
}

TEST(CV_Imx2dMat, copy) {
    Imx2dMatCopy test;
    test.safe_run();
}


class Imx2dMatReadWrite : public Imx2dBase
{
protected:
    void run(int);
};

void Imx2dMatReadWrite::run(int)
{
    Mat* m;
    int row, col;
    Vec3b v3b;

    preamble();

    // Enable allocator with default buffer size threshold
    setGMatAllocatorParams(GMatAllocatorParams(0, true));
    setUseGMatAllocator(true);

    m = new Mat(480, 640, CV_8UC3);
    EXPECT_EQ(stats.getNumberOfAllocations(), allocs + 1);

    for (row = 0; row < m->rows; ++row)
    {
        for (col = 0; col < m->cols; ++col)
        {
            uchar val = row * 100 + col;
            v3b = Vec3b(val, val + 1, val + 2);
            m->at<Vec3b>(row, col) = v3b;
        }
    }

    for (row = 0; row < m->rows; ++row)
    {
        for (col = 0; col < m->cols; ++col)
        {
            uchar val = row * 100 + col;
            v3b = Vec3b(val, val + 1, val + 2);
            ASSERT_EQ(m->at<Vec3b>(row, col), v3b);
        }
    }

    delete m;

    postamble();
}

TEST(CV_Imx2dMat, readWrite) {
    Imx2dMatReadWrite test;
    test.safe_run();
}


class Imx2dBufferPoolCache : public Imx2dBase
{
public:
    Imx2dBufferPoolCache(bool _cacheable) :
                         cacheable(_cacheable) {}
protected:
    void run(int);
    bool cacheable;
    void allocateMats(const std::vector<unsigned> &vsize,
                      std::vector<cv::Mat *> &vmat);
    void deallocateMats(std::vector<cv::Mat *> &vmat);
};

void Imx2dBufferPoolCache::allocateMats(const std::vector<unsigned> &_vsize,
                                        std::vector<cv::Mat *> &_vmat)
{
    for (auto it = _vsize.begin(); it != _vsize.end(); it++)
    {
        size_t size = *it;
        Mat* m = new Mat(size, 1, CV_8UC1);
        _vmat.push_back(m);
    }
}


void Imx2dBufferPoolCache::deallocateMats(std::vector<cv::Mat *> &_vmat)
{
    while (!_vmat.empty())
    {
        cv::Mat* m = _vmat[0];
        _vmat.erase(_vmat.begin());
        delete m;
    }
}

#define __PAGE_SZ 4096
#define CACHE_PAGES_MAX 16
#define CACHE_ALLOCS_MAX 4


void Imx2dBufferPoolCache::run(int)
{
    std::vector<cv::Mat *> vmat;

    preamble();

    cv::imx2d::Imx2dGAllocator& alloc = cv::imx2d::Imx2dGAllocator::getInstance();

    BufferCacheParams params(__PAGE_SZ * CACHE_PAGES_MAX /* cacheUsageMax */,
                                         CACHE_ALLOCS_MAX /* cacheAllocCountMax */);
    setBufferCacheParams(params);

    // Enable allocator with all buffers allocated from graphic buffers
    setGMatAllocatorParams(GMatAllocatorParams(1, cacheable));
    setUseGMatAllocator(true);
    EXPECT_EQ(alloc.getAllocations(), 0U);

    // allocate 2 buffers
    allocateMats({__PAGE_SZ, __PAGE_SZ}, vmat);
    ASSERT_EQ(vmat.size(), 2U);
    EXPECT_EQ(alloc.getAllocations(), 2U);

    // release them -> in cache (full)
    deallocateMats(vmat);
    ASSERT_EQ(vmat.size(), 0U);
    EXPECT_EQ(alloc.getAllocations(), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 2U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    EXPECT_EQ(alloc.getCacheUsage(cacheable), __PAGE_SZ * 2U);
    EXPECT_EQ(alloc.getCacheUsage(!cacheable), __PAGE_SZ * 0U);

    // allocate one buffer (from cache, one remaining)
    allocateMats({__PAGE_SZ}, vmat);
    EXPECT_EQ(alloc.getAllocations(), 1U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 1U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    EXPECT_EQ(alloc.getCacheUsage(cacheable), __PAGE_SZ * 1U);
    EXPECT_EQ(alloc.getCacheUsage(!cacheable), __PAGE_SZ * 0U);

    // allocate 4 more buffer (1 from cache, none remaining, 3 allocated)
    allocateMats({__PAGE_SZ, __PAGE_SZ, __PAGE_SZ, __PAGE_SZ}, vmat);
    EXPECT_EQ(alloc.getAllocations(), 5U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    EXPECT_EQ(alloc.getCacheUsage(cacheable), __PAGE_SZ * 0U);
    EXPECT_EQ(alloc.getCacheUsage(!cacheable), __PAGE_SZ * 0U);

    // Free 5 buffers, only 4 enter the cache
    deallocateMats(vmat);
    EXPECT_EQ(alloc.getAllocations(), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 4U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    EXPECT_EQ(alloc.getCacheUsage(cacheable), __PAGE_SZ * 4U);
    EXPECT_EQ(alloc.getCacheUsage(!cacheable), __PAGE_SZ * 0U);
    // drain cache
    setUseGMatAllocator(false);
    setUseGMatAllocator(true);
    EXPECT_EQ(alloc.getAllocations(), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);

    // Allocate / deallocate big buffer, make sure it does not enter the cache
    allocateMats({__PAGE_SZ * (CACHE_PAGES_MAX + 1)}, vmat);
    EXPECT_EQ(alloc.getAllocations(), 1U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    deallocateMats(vmat);
    EXPECT_EQ(alloc.getAllocations(), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);

    // Allocate / deallocate max cacheable buffer, make sure it does enter the cache
    allocateMats({__PAGE_SZ * (CACHE_PAGES_MAX)}, vmat);
    EXPECT_EQ(alloc.getAllocations(), 1U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    deallocateMats(vmat);
    EXPECT_EQ(alloc.getAllocations(), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 1U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);

    // Allocate a buffer whose size is small (1/4) compared to cached buffer: no cache reuse
    allocateMats({__PAGE_SZ * (CACHE_PAGES_MAX / 4)}, vmat);
    EXPECT_EQ(alloc.getAllocations(), 1U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 1U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    // Allocate a buffer whose size is big enough (1/2) compared to cached buffer: cache reuse
    allocateMats({__PAGE_SZ * (CACHE_PAGES_MAX / 2)}, vmat);
    EXPECT_EQ(alloc.getAllocations(), 2U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    // drain mat buffers and cache
    deallocateMats(vmat);
    setUseGMatAllocator(false);
    setUseGMatAllocator(true);
    EXPECT_EQ(alloc.getAllocations(), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);

    // Fill cache with buffers from different sizes in increasing order
    allocateMats({__PAGE_SZ * (CACHE_PAGES_MAX / 16),
                  __PAGE_SZ * (CACHE_PAGES_MAX / 8),
                  __PAGE_SZ * (CACHE_PAGES_MAX / 4),
                  __PAGE_SZ * (CACHE_PAGES_MAX / 2),
                  __PAGE_SZ * (CACHE_PAGES_MAX * 2) /* too big for cache */},
                 vmat);
    EXPECT_EQ(alloc.getAllocations(), 5U);
    deallocateMats(vmat);
    EXPECT_EQ(alloc.getAllocations(), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 4U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    EXPECT_EQ(alloc.getCacheUsage(cacheable), __PAGE_SZ * (1U + 2U + 4U + 8U));
    EXPECT_EQ(alloc.getCacheUsage(!cacheable), __PAGE_SZ * 0U);
    // Allocate buffers in random order from cache or not
    allocateMats({__PAGE_SZ * (CACHE_PAGES_MAX) / 8}, vmat);
    EXPECT_EQ(alloc.getAllocations(), 1U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 3U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    EXPECT_EQ(alloc.getCacheUsage(cacheable), __PAGE_SZ * (1U + 4U + 8U));
    EXPECT_EQ(alloc.getCacheUsage(!cacheable), __PAGE_SZ * 0U);
    allocateMats({__PAGE_SZ * (CACHE_PAGES_MAX) / 2}, vmat);
    EXPECT_EQ(alloc.getAllocations(), 2U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 2U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    EXPECT_EQ(alloc.getCacheUsage(cacheable), __PAGE_SZ * (1U + 4U));
    EXPECT_EQ(alloc.getCacheUsage(!cacheable), __PAGE_SZ * 0U);
    allocateMats({__PAGE_SZ * (CACHE_PAGES_MAX) / 2}, vmat); // not available in cache
    EXPECT_EQ(alloc.getAllocations(), 3U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 2U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    EXPECT_EQ(alloc.getCacheUsage(cacheable), __PAGE_SZ * (1U + 4U));
    EXPECT_EQ(alloc.getCacheUsage(!cacheable), __PAGE_SZ * 0U);
    allocateMats({__PAGE_SZ * (CACHE_PAGES_MAX / 16) - 1}, vmat);
    EXPECT_EQ(alloc.getAllocations(), 4U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 1U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    EXPECT_EQ(alloc.getCacheUsage(cacheable), __PAGE_SZ * (4U));
    EXPECT_EQ(alloc.getCacheUsage(!cacheable), __PAGE_SZ * 0U);
    allocateMats({__PAGE_SZ * (CACHE_PAGES_MAX / 4) - 1}, vmat);
    EXPECT_EQ(alloc.getAllocations(), 5U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    EXPECT_EQ(alloc.getCacheUsage(cacheable), __PAGE_SZ * 0U);
    EXPECT_EQ(alloc.getCacheUsage(!cacheable), __PAGE_SZ * 0U);
    allocateMats({__PAGE_SZ * (CACHE_PAGES_MAX)}, vmat);
    EXPECT_EQ(alloc.getAllocations(), 6U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    EXPECT_EQ(alloc.getCacheUsage(cacheable), __PAGE_SZ * 0U);
    EXPECT_EQ(alloc.getCacheUsage(!cacheable), __PAGE_SZ * 0U);
    deallocateMats(vmat);
    setUseGMatAllocator(false);
    setUseGMatAllocator(true);
    EXPECT_EQ(alloc.getAllocations(), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);


    // Check that oldest cache entries gets removed to make earlier one
    allocateMats({__PAGE_SZ * 6,
                  __PAGE_SZ * 3,
                  __PAGE_SZ * 4,
                  __PAGE_SZ * 2
                  },
                 vmat);
    EXPECT_EQ(alloc.getAllocations(), 4U);
    deallocateMats(vmat);
    EXPECT_EQ(alloc.getAllocations(), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 4U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    EXPECT_EQ(alloc.getCacheUsage(cacheable), __PAGE_SZ * (6U + 3U + 4U + 2U));
    EXPECT_EQ(alloc.getCacheUsage(!cacheable), __PAGE_SZ * 0U);
    allocateMats({__PAGE_SZ * 8}, vmat);
    deallocateMats(vmat);
    EXPECT_EQ(alloc.getAllocations(), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 3U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    EXPECT_EQ(alloc.getCacheUsage(cacheable), __PAGE_SZ * (4U + 2U + 8U));
    EXPECT_EQ(alloc.getCacheUsage(!cacheable), __PAGE_SZ * 0U);
    setUseGMatAllocator(false);
    EXPECT_EQ(alloc.getAllocations(), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);

    // Postamble checks: drain allocations and caches
    setUseGMatAllocator(false);
    EXPECT_EQ(alloc.getAllocations(), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(cacheable), 0U);
    EXPECT_EQ(alloc.getCacheAllocations(!cacheable), 0U);
    EXPECT_EQ(alloc.getCacheUsage(cacheable), __PAGE_SZ * 0U);
    EXPECT_EQ(alloc.getCacheUsage(!cacheable), __PAGE_SZ * 0U);

    postamble();
}

TEST(CV_Imx2dMat, poolCacheCacheable) {
    Imx2dBufferPoolCache test(true);
    test.safe_run();
}

TEST(CV_Imx2dMat, poolCacheNonCacheable) {
    Imx2dBufferPoolCache test(false);
    test.safe_run();
}


}} // namespace
