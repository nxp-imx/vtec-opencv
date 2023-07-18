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

#ifndef __OPENCV_IMX2D_HPP__
#define __OPENCV_IMX2D_HPP__

#include "opencv2/core.hpp"
#include "opencv2/core/utils/allocator_stats.hpp"


/** @defgroup imx2d i.MX 2D accelerated primitives
*/

namespace cv {
namespace imx2d {

/**
@brief graphic memory MatAllocator parameters

Those parameters are used to configure the graphic memory MatAllocator
@param sizeMin Define the minimum buffer size to be allocated from graphic
pool. Smaller buffers will be allocated from system memory (heap).
@param cacheable Attribute defines if graphic buffers shall be mapped as cached
for CPU.
*/

static const size_t GMAT_ALLOCATOR_PARAMS_SIZE_MIN_DEFAULT = (8 * 4096); // approx. 96x96 BGR888
static const bool GMAT_ALLOCATOR_PARAMS_CACHEABLE_DEFAULT = true;

class CV_EXPORTS GMatAllocatorParams
{
public:
    GMatAllocatorParams(ssize_t _sizeMin = GMAT_ALLOCATOR_PARAMS_SIZE_MIN_DEFAULT,
                        bool _cacheable = GMAT_ALLOCATOR_PARAMS_CACHEABLE_DEFAULT) :
        sizeMin(_sizeMin), cacheable(_cacheable) {}

    size_t sizeMin;
    bool cacheable;
};


/**
@brief Configures i.MX 2D acceleration.

@param flag enable (true) or disable (false) i.MX 2D acceleration.
*/
CV_EXPORTS_W void setUseImx2d(bool flag);


/**
@brief Returns the status of i.MX 2D acceleration.
*/
CV_EXPORTS_W bool useImx2d();


/**
@brief Configures graphic memory MatAllocator parameters.

@param allocParams configuration parameters to be applied.
*/
CV_EXPORTS void setGMatAllocatorParams(const GMatAllocatorParams &allocParams);


/**
@brief Enables the graphic memory MatAllocator.

@param flag enable (true) or disable (false) graphic memory MatAllocator.
*/
CV_EXPORTS void setUseGMatAllocator(bool flag);


/**
@brief Returns the activation status of the graphic memory MatAllocator.
*/
CV_EXPORTS bool useGMatAllocator();


/**
@brief Parameters for deallocated buffers cache configuration

Those parameters are used to configure the deallocated buffers cache.
@param cacheUsageMax Total number of bytes pending in the cache.
@param cacheAllocCountMax Maximum number of buffers in the cache.
*/

static const size_t BUFFER_CACHE_PARAMS_USAGE_MAX_DEFAULT = (64 * 1024 *1024);
static const unsigned BUFFER_CACHE_PARAMS_ALLOC_COUNT_MAX_DEFAULT = 16;

class CV_EXPORTS_W_SIMPLE BufferCacheParams
{
public:
    CV_WRAP BufferCacheParams(ssize_t _cacheUsageMax = BUFFER_CACHE_PARAMS_USAGE_MAX_DEFAULT,
                              unsigned _cacheAllocCountMax = BUFFER_CACHE_PARAMS_ALLOC_COUNT_MAX_DEFAULT) :
        cacheUsageMax(_cacheUsageMax), cacheAllocCountMax(_cacheAllocCountMax) {}

    CV_PROP_RW size_t cacheUsageMax;
    CV_PROP_RW unsigned cacheAllocCountMax;
};


/**
@brief Configure the deallocated buffers cache parameters.

@param bufferCacheParams configuration parameters to be applied.
*/
CV_EXPORTS_W void setBufferCacheParams(const BufferCacheParams& bufferCacheParams);


/**
@brief Return AllocatorStatisticsInterface reference to graphic MatAllocator.
*/
CV_EXPORTS cv::utils::AllocatorStatisticsInterface& getGMatAllocatorStats();

}} // cv::imx2d::

#endif //__OPENCV_IMX2D_HPP__


