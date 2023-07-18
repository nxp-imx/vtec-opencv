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

#ifndef __OPENCV_IMX2D_COMMON_HPP__
#define __OPENCV_IMX2D_COMMON_HPP__

#include <atomic>
#include <memory>
#include <mutex>
#include <set>

#if __GNUC__ >= 4
    #define DSO_EXPORT __attribute__ ((visibility ("default")))
    #define DSO_LOCAL  __attribute__ ((visibility ("hidden")))
#endif


#ifdef DEBUG
#define IMX2D_DEBUG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#else
#define IMX2D_DEBUG(fmt, ...)
#endif
#define IMX2D_INFO(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define IMX2D_ERROR(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)


namespace cv {
namespace imx2d {

/**
@brief G2dBufRepo buffer pool handling class forward declarations
*/
class G2dBufRepo;
typedef std::shared_ptr<G2dBufRepo> G2dBufRepoPtr;

class G2dBufPool;
typedef std::shared_ptr<G2dBufPool> G2dBufPoolPtr;

/**
@brief Imx2dGAllocator class manages Graphic buffers allocations
*/
class DSO_EXPORT Imx2dGAllocator
{
public:
    /**
     @brief Return a reference to class instance singleton.
    */
    static Imx2dGAllocator& getInstance();

    /**
    @brief Enable Imx2d Graphic Allocator

    Graphic allocator may be enabled by GMatAllocator or by HAL that both require
    buffer allocations from underlying graphic allocator (G2D).
    Thus Graphic Allocator enablement state is refcounted.
    */
    void enable();

    /**
    @brief Disable Imx2d Graphic Allocator
    */
    void disable();

    /**
    @brief Allocate graphic buffer

    Allocation will be done either from buffers cache if one buffer matches the
    pending request - if no cached buffer is available a new buffer will be
    allocated from underlying graphic buffer allocator.
    @param size requested size in bytes
    @param cacheable buffer is cacheable
    @param [out] handle handle associated to the buffer - used for free()
    @return virtual address of mapped buffer
    */
    void* alloc(size_t size, bool cacheable, void*& handle);

    /**
    @brief Free graphic buffer

    @param handle handle associated to the buffer during alloc()
    */
    void free(void *handle);

    /**
    @brief Test and report graphic buffer info corresponding to virtual address

    Return handle and cacheable attribute information corresponding to the
    backing graphic buffer that contains the virtual address passed in
    parameters.
    @param vaddr virtual address contained in the requested buffer
    @param [out] handle handle associated to the buffer
    @param [out] cacheable buffer is cacheable
    @return true if graphic buffer matching virtual address was found
    */
    bool isGraphicBuffer(void* vaddr, void*& handle, bool& cacheable);

    /**
    @brief Return total number of bytes allocated
    */
    size_t getUsage();

    /**
    @brief Return number of buffers allocated
    */
    unsigned getAllocations();

    /**
    @brief Return total number of bytes in cache (deallocated buffers)

    @param bool true for cacheable pool, false for non-cacheable
    */
    size_t getCacheUsage(bool cacheable);

    /**
    @brief Return number of deallocated buffers in cache

    @param bool true for cacheable pool, false for non-cacheable
    */
    unsigned getCacheAllocations(bool cacheable);

    /**
    @brief Configure the cache of deallocated buffers

    Deallocated graphic buffers are not freed immediately, but placed in a
    cache of available buffers for fast reuse. To avoid having the cache to
    grow indefinitely, it is configured with tunables below to limit its
    expansion.
    @param cacheUsageMax Total number of bytes in the cache
    @param cacheAllocCountMax Maximum number of buffers in the cache
    */
    void setCacheConfig(size_t cacheUsageMax, unsigned cacheAllocCountMax);

protected:
    Imx2dGAllocator();
    virtual ~Imx2dGAllocator();
    Imx2dGAllocator(Imx2dGAllocator const& copy); /* not implemented */
    Imx2dGAllocator& operator=(Imx2dGAllocator const& copy);  /* not implemented */

    unsigned enableCount;
    G2dBufRepoPtr g2dBufRepoPtr;
    G2dBufPoolPtr g2dBufPoolPtr;
    std::mutex mutex;
    unsigned allocCount;
    size_t usage;
};

/**
@brief Imx2dHalCounters class manages HAL primitives counter
*/

class DSO_EXPORT Imx2dHalCounters
{
public:
    /**
     @brief HAL primitives
    */
    enum Primitive {
        FLIP,
        RESIZE,
        ROTATE,
        PRIMITIVES_MAX
    };

    Imx2dHalCounters(): counters() {}
    virtual ~Imx2dHalCounters() {}

    /**
     @brief Increment primitive usage counter
    */
    void incrementCount(Primitive primitive);

    /**
     @brief Read primitive usage counter
    */
    unsigned readCount(Primitive primitive);

protected:
    std::atomic<unsigned>counters[PRIMITIVES_MAX];
};


/**
@brief Describes hardware accelerator capabilities
*/

class DSO_EXPORT HardwareCapabilities {
public:
    HardwareCapabilities();

    /**
     @brief Hardware features
    */
    enum Capabilities {
        THREE_CHANNELS,
        CAPABILITY_MAX
    };
    bool hasSupport();
    bool hasCapability(Capabilities cap);

private:
    bool supported;
    bool caps[CAPABILITY_MAX];
};


/**
@brief Imx2dHal class manages HAL
*/

class DSO_EXPORT Imx2dHal
{
public:
    /**
     @brief Return a reference to class instance singleton.
    */
    static Imx2dHal& getInstance();

    /**
     @brief Enables or disable HAL usage
    */
    void setEnable(bool flag);

    /**
     @brief Returns HAL enablement status
    */
    bool isEnabled();

    /**
     @brief Returns HAL hardware support
    */
    HardwareCapabilities& getHardwareCapabilities();

    /**
     @brief Returns G2D handle created when HAL is enabled
    */
    void* getG2dHandle();

    /**
     @brief HAL counters for debug purpose
    */
    Imx2dHalCounters counters;

protected:
    Imx2dHal();
    virtual ~Imx2dHal();
    Imx2dHal(Imx2dHal const& copy); /* not implemented */
    Imx2dHal& operator=(Imx2dHal const& copy);  /* not implemented */

    bool enabled;
    std::mutex mutex;
    void* g2dHandle;
    HardwareCapabilities hwCapabilities;
};



//! @}
}} // cv::imx2d::

#endif //__OPENCV_IMX2D_COMMON_HPP__


