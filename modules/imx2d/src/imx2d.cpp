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

#include <stdint.h>
#include <unistd.h>

#include "opencv2/core/utility.hpp"
#include "opencv2/core/private.hpp"
#include "opencv2/core/types_c.h"
#include "opencv2/core.hpp"
#include "opencv2/core/utils/logger.hpp"
#include "opencv2/imx2d.hpp"

#include "imx2d_common.hpp"

#include <opencv2/core/utils/allocator_stats.impl.hpp>

namespace cv {
namespace imx2d {

enum AllocatorFlags {
        ALLOCATOR_FLAGS_IMX2D_BUFFER = 1 << 0, // allocated from graphic buffers pool
};

static cv::utils::AllocatorStatistics& _getGMatAllocatorStats()
{
    static cv::utils::AllocatorStatistics allocatorStats;
    return allocatorStats;
}


//============================= GMatAllocator ================================


// Implementation of GMatAllocator is derived from cv::StdMatAllocator
class GMatAllocator CV_FINAL : public MatAllocator
{
public:

    GMatAllocator() : minSize(0), cacheable(false)
    {
        stdAllocator = Mat::getStdAllocator();
    }

    UMatData* allocate(int dims, const int* sizes, int type,
                       void* data0, size_t* step, AccessFlag flags,
                       UMatUsageFlags usageFlags) const CV_OVERRIDE
    {
        if(data0)
            return defaultAllocate(dims, sizes, type, data0, step, flags, usageFlags);

        size_t total = CV_ELEM_SIZE(type);
        for( int i = dims-1; i >= 0; i-- )
        {
            if( step )
                step[i] = total;
            total *= sizes[i];
        }

        if( total < minSize )
            return defaultAllocate(dims, sizes, type, data0, step, flags, usageFlags);

        Imx2dGAllocator& gAlloc = Imx2dGAllocator::getInstance();
        void *handle;
        void *ptr = gAlloc.alloc(total, cacheable, handle);
        if (!ptr)
        {
            CV_LOG_WARNING(NULL, "Can't allocate graphic buffer size: " << total);
            return defaultAllocate(dims, sizes, type, data0, step, flags, usageFlags);
        }

        uchar* vaddr = static_cast<uchar*>(ptr);
        CV_Assert(cvAlignPtr(vaddr, CV_MALLOC_ALIGN) == vaddr);

        UMatData* u = new UMatData(this);
        u->handle = handle;
        u->data = u->origdata = vaddr;
        u->size = total;
        u->allocatorFlags_ = ALLOCATOR_FLAGS_IMX2D_BUFFER;

        cv::utils::AllocatorStatistics& stats = _getGMatAllocatorStats();
        stats.onAllocate(u->size);

        return u;
    }

    bool allocate(UMatData* u, AccessFlag /*accessFlags*/, UMatUsageFlags /*usageFlags*/) const CV_OVERRIDE
    {
        if(!u) return false;
        return true;
    }

    void deallocate(UMatData* u) const CV_OVERRIDE
    {
        if(!u)
            return;

        CV_Assert(u->urefcount == 0);
        CV_Assert(u->refcount == 0);
        CV_Assert(!(u->flags & UMatData::USER_ALLOCATED));
        CV_Assert(u->allocatorFlags_ & ALLOCATOR_FLAGS_IMX2D_BUFFER);

        Imx2dGAllocator& gAlloc = Imx2dGAllocator::getInstance();
        void* handle = u->handle;
        gAlloc.free(handle);

        u->origdata = 0;

        cv::utils::AllocatorStatistics& stats = _getGMatAllocatorStats();
        stats.onFree(u->size);

        delete u;
    }

    void setCacheable(bool _cacheable) { cacheable = _cacheable; }
    void setMinSize(size_t _minSize) { minSize = _minSize; }

private:
    UMatData* defaultAllocate(int dims, const int* sizes, int type,
                              void* data0, size_t* step, AccessFlag flags,
                              UMatUsageFlags usageFlags) const
    {
        return stdAllocator->allocate(dims, sizes, type, data0, step, flags, usageFlags);
    }

    void defaultDeallocate(UMatData* u) const
    {
        stdAllocator->deallocate(u);
    }

    MatAllocator* stdAllocator;
    size_t minSize;
    bool cacheable;
};


//============================== GMatHandler ================================

/**
@brief GMatHandler class configures GMatAllocator usage

Singleton instance of this class is aimed at enabling or disabling the custom
GMatAllocator that allocates Mat buffers with contiguous graphic memory.
*/

class GMatHandler
{
public:
    /**
     @brief Returns a reference to object singleton.
     This class can not be instanciated directly by user. This method returns
     reference to the only instance of this object.
     */
    static GMatHandler& getInstance();

    /**
    @brief Configures graphic allocator parameters.
    @param minSize minimum buffer size for graphic memory allocation (0: auto)
    @param cacheable cacheable attribute passed to graphic allocations
    */
    void setGMatAllocatorConfig(size_t _minSize, bool _cacheable);

    /**
    @brief Set usage of graphic allocator as default Mat buffer allocator.
    */
    void setUseGMatAllocator(bool flag);

    /**
     @brief Checks if MatAllocator is enabled.
     */
    bool isEnabled();

protected:
    GMatHandler();
    GMatHandler(GMatHandler const& copy); /* not implemented */
    GMatHandler& operator=(GMatHandler const& copy);  /* not implemented */
    ~GMatHandler();

    GMatAllocator gMatAllocator;
    MatAllocator* oldAllocator;
    int enabled;
};


GMatHandler& GMatHandler::getInstance()
{
    static GMatHandler instance;
    return instance;
}

GMatHandler::GMatHandler(): oldAllocator(nullptr), enabled(false)
{
    // applies default MatAllocator config
    setGMatAllocatorConfig(GMAT_ALLOCATOR_PARAMS_SIZE_MIN_DEFAULT,
                           GMAT_ALLOCATOR_PARAMS_CACHEABLE_DEFAULT);
}

GMatHandler::~GMatHandler()
{
}


void GMatHandler::setGMatAllocatorConfig(size_t _minSize, bool _cacheable)
{
    if (enabled)
        throw std::runtime_error(
            "Configuration shall be done while allocator is disabled!");

    gMatAllocator.setMinSize(_minSize);
    gMatAllocator.setCacheable(_cacheable);
}

void GMatHandler::setUseGMatAllocator(bool flag)
{
    Imx2dGAllocator& gAllocator = Imx2dGAllocator::getInstance();

    if (flag == enabled)
        return;
    enabled = flag;

    if (flag)
    {
        gAllocator.enable();

        oldAllocator = Mat::getDefaultAllocator();
        Mat::setDefaultAllocator(&gMatAllocator);
    }
    else
    {
        Mat::setDefaultAllocator(oldAllocator);

        gAllocator.disable();
    }
}

bool GMatHandler::isEnabled()
{
    return enabled;
}


//============================ Public interface ===============================

static void _setUseHal(bool flag)
{
    Imx2dHal& hal = Imx2dHal::getInstance();

    hal.setEnable(flag);
}

static bool _useHal()
{
    Imx2dHal& hal = Imx2dHal::getInstance();

    return hal.isEnabled();
}

static void _setGMatAllocatorParams(const GMatAllocatorParams &allocParams)
{
    GMatHandler& handler = GMatHandler::getInstance();

    handler.setGMatAllocatorConfig(allocParams.sizeMin,
                                   allocParams.cacheable);
}

static void _setUseGMatAllocator(bool flag)
{
    GMatHandler& handler = GMatHandler::getInstance();

    handler.setUseGMatAllocator(flag);
}

static bool _useGMatAllocator()
{
    GMatHandler& handler = GMatHandler::getInstance();

    return handler.isEnabled();
}

static void _setBufferCacheParams(const BufferCacheParams&
                                bufferCacheParams)
{
    Imx2dGAllocator& allocator = Imx2dGAllocator::getInstance();
    allocator.setCacheConfig(bufferCacheParams.cacheUsageMax,
                             bufferCacheParams.cacheAllocCountMax);
}

void setUseImx2d(bool flag)
{
    imx2d::_setUseHal(flag);
}

bool useImx2d()
{
    return imx2d::_useHal();
}

void setGMatAllocatorParams(const GMatAllocatorParams &allocParams)
{
    imx2d::_setGMatAllocatorParams(allocParams);
}

void setUseGMatAllocator(bool flag)
{
    imx2d::_setUseGMatAllocator(flag);
}

bool useGMatAllocator()
{
    return imx2d::_useGMatAllocator();
}

void setBufferCacheParams(const BufferCacheParams& bufferCacheParams)
{
    imx2d::_setBufferCacheParams(bufferCacheParams);
}

cv::utils::AllocatorStatisticsInterface& getGMatAllocatorStats()
{
    return imx2d::_getGMatAllocatorStats();
}

}} // cv::imx2d::
