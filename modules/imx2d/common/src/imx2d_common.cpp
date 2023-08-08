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

#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdint.h>
#include <string>
#include <unistd.h>
#include <vector>

//#define DEBUG
#include "imx2d_common.hpp"

#include "g2d.h"

namespace cv {
namespace imx2d {

#define __TO_STRING(m) #m
#define TO_STRING(m) __TO_STRING(m)

#define IMX2D_Assert( expr ) do { if (!(expr)) \
                                    throw std::runtime_error( \
                                      "(" __FILE__ ":" \
                                      TO_STRING(__LINE__) ")" \
                                      " Assertion failed: " #expr); \
                              } while (0)

#ifdef DEBUG
#define CACHE_LOG(fmt, ...) IMX2D_INFO(fmt, ##__VA_ARGS__)
#else
#define CACHE_LOG(fmt, ...)
#endif

//============================= G2dBufContainer ================================

/**
@brief container to store g2d_buf descriptor with attributes
*/
class G2dBufContainer
{
public:
    G2dBufContainer(struct g2d_buf* _buf, bool _cacheable):
                    g2dBuf(_buf), cacheable(_cacheable) {}
    virtual ~G2dBufContainer() {}
    struct g2d_buf* g2dBuf;
    bool cacheable;
};


typedef std::shared_ptr<G2dBufContainer> GBCPtr;

/**
@brief G2dBufContainer by address comparison object for storage in a STL set.

It is used to store objects in a set with ordered increasing addresses for
faster access.
Comparison is done on buffer address and size only, not on other attributes.
*/
struct G2dBufContainerVaddrCompare
{
    bool operator()(GBCPtr b1, GBCPtr b2)
    {
        char* v1 = static_cast<char*> (b1->g2dBuf->buf_vaddr);
        char* v2 = static_cast<char*> (b2->g2dBuf->buf_vaddr);
        int s1 = static_cast<int>(b1->g2dBuf->buf_size);
        int s2 = static_cast<int>(b2->g2dBuf->buf_size);
        bool overlap;

        // make sure that b2 is not nested within b1 (same buffer)
        // overlap computation principle inherited from IoU computation...
        overlap = !! std::max(std::min(v1 + s1, v2 + s2) - std::max(v1, v2),
                              0L);
        return (v1 < v2) && !overlap;
    }
};


//============================= G2dBufRepo ================================

/**
@brief G2dBufRepo class maintains allocated g2d_buf repository

Class records g2d_buf allocations sorted by virtual address to allow matching
a virtual addresses to one of the allocated g2d buffer.
Public methods require lock to be held by the caller.
*/

class G2dBufRepo
{
public:
    G2dBufRepo(): allocCount(0) {}
    // copy constructor as std::mutex is not copy-able
    G2dBufRepo(const G2dBufRepo& obj) : allocCount(0) { (void)obj; };
    virtual ~G2dBufRepo() {}

    /**
    @brief Record allocation of a new g2d_buf buffer.

    Buffers recorded using this method will have to be removed using
    unregisterDescriptor() before release.
    Method is expected to be used by GMatAllocator.
    @param b pointer to g2d_buf descriptor
    @param cacheable g2d_buf descriptor allocation is cacheable
    */
    void registerDescriptor(struct g2d_buf* b, bool cacheable);

    /**
    @brief Delete allocation of a new g2d_buf buffer.

    Remove from internal records a g2d_buf buffer previously register using
    registerDescriptor().
    Method is expected to be used by GMatAllocator.
    @param b pointer to g2d_buf descriptor
    */
   void unregisterDescriptor(struct g2d_buf* b);

    /**
    @brief Return g2d_buf descriptor associated to a buffer address.

    Return allocated g2d_buf associated to a virtual address contained in that
    buffer.
    Method is expected to be used by IMX2D HAL that receives virtual addresses
    as parameters.
    @param vaddr buffer virtual address visible by user space
    @param b [out] address for storing associated g2d_buf descriptor
    @param cacheable [out] buffer is cacheable
    @return true if a corresponding g2d_buf descriptor has been found.
    */
    bool isVaddrG2dBuf(void* vaddr, g2d_buf*& b, bool& cacheable);

protected:

    /**
    @brief Version of isVaddrG2dBuf() for class-internal usage.
    */
    bool isVaddrG2dBufNoLock(void* vaddr, g2d_buf*& b, bool& cacheable);

    std::set<GBCPtr, G2dBufContainerVaddrCompare> allocSet;
    unsigned allocCount;
    std::mutex mutex;
};

void G2dBufRepo::registerDescriptor(struct g2d_buf* b, bool cacheable)
{
    struct g2d_buf* _buf;
    bool _cacheable;
    GBCPtr bufContPtr(new G2dBufContainer(b, cacheable));
    std::unique_lock<std::mutex> lock(mutex);

    IMX2D_Assert(!isVaddrG2dBufNoLock(b->buf_vaddr, _buf, _cacheable));
    allocSet.insert(bufContPtr);
    allocCount++;
    IMX2D_Assert(allocCount == allocSet.size());
    IMX2D_Assert(isVaddrG2dBufNoLock(b->buf_vaddr, _buf, _cacheable));
}

void G2dBufRepo::unregisterDescriptor(struct g2d_buf* b)
{
    struct g2d_buf* _buf;
    bool _cacheable;
    GBCPtr bufContPtr(new G2dBufContainer(b, false /* unused for search */));
    std::unique_lock<std::mutex> lock(mutex);

    IMX2D_Assert(isVaddrG2dBufNoLock(b->buf_vaddr, _buf, _cacheable));
    allocSet.erase(bufContPtr);
    allocCount--;
    IMX2D_Assert(allocCount == allocSet.size());
    IMX2D_Assert(!isVaddrG2dBufNoLock(b->buf_vaddr, _buf, _cacheable));
}

bool G2dBufRepo::isVaddrG2dBufNoLock(void* vaddr, g2d_buf*& b, bool& cacheable)
{
    struct g2d_buf buf { .buf_handle = nullptr, .buf_vaddr = vaddr,
                       .buf_paddr = 0, .buf_size = 1 /* arbitrary */ };
    GBCPtr bufContPtr(new G2dBufContainer(&buf, false /* unused for search */));
    std::set<GBCPtr>::iterator it;

    it = allocSet.find(bufContPtr);
    bool found = (it != allocSet.end());
    if (found)
    {
        b = (*it)->g2dBuf;
        cacheable = (*it)->cacheable;
    }
    else
    {
        b = nullptr;
    }
    return found;
}

bool G2dBufRepo::isVaddrG2dBuf(void* vaddr, g2d_buf*& b, bool& cacheable)
{
    std::unique_lock<std::mutex> lock(mutex);
    return isVaddrG2dBufNoLock(vaddr, b, cacheable);
}


//============================= G2dBufPool ================================

/**
@brief G2dBufPool class serves gbuffer allocations maintaining a cache

On allocation request, pool will check from buffers cache if there is a
preallocated buffer matching the requested size that can be directly used.
If none is available, a new buffer is allocated to serve the request.
On deallocation request, freed buffer will be stored in the cache of
preallocated requests, available for fast reuse.
When amount of preallocated (freed) buffers exceeds some thresholds,
cache cleanup is operated to limit amount of memory used by cache.
When the pool is disabled, cache is bypassed and memory allocation and
deallocations are directly passed to the underlying allocator.
*/

class G2dBufPoolInstance
{
public:
    G2dBufPoolInstance(bool _cacheable);
    // copy constructor as std::mutex is not copy-able
    G2dBufPoolInstance(const G2dBufPoolInstance& obj);
    virtual ~G2dBufPoolInstance();

    struct g2d_buf* alloc(size_t size);
    void free(struct g2d_buf* buf);
    void setUseCache(bool flag);
    void setCacheConfig(size_t _cacheUsageMax, unsigned _cacheAllocCountMax);
    size_t getCacheUsage();
    unsigned getCacheAllocations();

protected:
    void drainCacheNoLock();

    bool cacheEnabled;
    bool cacheable;
    size_t cacheUsageMax;
    unsigned cacheAllocCountMax;
    size_t cacheUsage;
    unsigned cacheAllocCount;

    std::mutex mutex;
    std::vector<struct g2d_buf*> cacheVector;

    static const size_t USAGE_MAX_DEFAULT = (64 * 1024 *1024);
    static const unsigned ALLOC_COUNT_MAX_DEFAULT = 16;
};


G2dBufPoolInstance::G2dBufPoolInstance(bool _cacheable) :
                       cacheEnabled(false),
                       cacheable(_cacheable),
                       cacheUsageMax(USAGE_MAX_DEFAULT),
                       cacheAllocCountMax(ALLOC_COUNT_MAX_DEFAULT),
                       cacheUsage(0),
                       cacheAllocCount(0)
{
}


G2dBufPoolInstance::G2dBufPoolInstance(const G2dBufPoolInstance& obj) :
                       cacheEnabled(false),
                       cacheable(obj.cacheable),
                       cacheUsageMax(obj.cacheUsageMax),
                       cacheAllocCountMax(obj.cacheAllocCountMax),
                       cacheUsage(0),
                       cacheAllocCount(0)
{
    (void)obj;
}


G2dBufPoolInstance:: ~G2dBufPoolInstance()
{
    try {
        drainCacheNoLock(); // can throw
    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}


struct g2d_buf* G2dBufPoolInstance::alloc(size_t size)
{
    struct g2d_buf *buf;
    std::vector<struct g2d_buf*>::iterator it, matchIt;
    size_t matchHeadroom;
    std::unique_lock<std::mutex> lock(mutex);

    if (!cacheEnabled)
        goto alloc_buff;

    if (cacheVector.empty())
        goto alloc_buff;

    matchHeadroom = SIZE_MAX;
    matchIt = cacheVector.end();
    for (it = cacheVector.begin(); it != cacheVector.end(); it++)
    {
        if ((size_t)((*it)->buf_size) >= size)
        {
            size_t headroom = size_t((*it)->buf_size) - size;
            // reuse buffer if not bigger than twice the requested size
            if ((headroom <= size) && (headroom < matchHeadroom))
            {
                matchHeadroom = headroom;
                matchIt = it;
            }
            if (matchHeadroom == 0)
                break;
        }
    }

    if (matchIt == cacheVector.end())
        goto alloc_buff;

    buf = *matchIt;
    cacheVector.erase(matchIt);
    cacheUsage -= buf->buf_size;
    cacheAllocCount--;

    IMX2D_Assert(cacheAllocCount == cacheVector.size());
    IMX2D_Assert(cacheUsage < cacheUsageMax);
    IMX2D_Assert(cacheAllocCount < cacheAllocCountMax );
    CACHE_LOG("%s(%d) sz:%zu(%d) va:%p u:%zu c:%d (cached)",
              __func__, cacheable, size, buf->buf_size, buf->buf_vaddr,
              cacheUsage, cacheAllocCount);

    return buf;

alloc_buff:
    lock.unlock();

    buf = g2d_alloc(size, cacheable);

    if (buf) {
        CACHE_LOG("%s(%d) sz:%zu(%d) va:%p u:%zu c:%d (alloc-ed)",
                  __func__, cacheable, size, buf->buf_size, buf->buf_vaddr,
                  cacheUsage, cacheAllocCount);
    }
    else
    {
        IMX2D_ERROR("%s g2d allocation failed (%zu)", __func__, size);
    }
    return buf;
}

void G2dBufPoolInstance::free(struct g2d_buf* buf)
{
    size_t size = (size_t)buf->buf_size;
    int ret;

    std::unique_lock<std::mutex> lock(mutex);

    if (!cacheEnabled)
        goto free_buf;

    if (cacheAllocCountMax < 1)
        goto free_buf;

    if (size > cacheUsageMax)
        goto free_buf;

    // pop oldest cache entries until buffer to be freed can fit in the cache
    while ((cacheAllocCount + 1 > cacheAllocCountMax) ||
           (cacheUsage + size > cacheUsageMax))
    {
        struct g2d_buf* cachedBuf;
        IMX2D_Assert(!cacheVector.empty());
        std::vector<struct g2d_buf*>::iterator it;

        it = cacheVector.begin();
        cachedBuf = *it;
        cacheVector.erase(it);
        cacheUsage -= cachedBuf->buf_size;
        cacheAllocCount--;
        IMX2D_Assert(cacheAllocCount == cacheVector.size());

        CACHE_LOG("%s(%d) sz:%d u:%zu c:%d (cache purge)",
                  __func__, cacheable, cachedBuf->buf_size, cacheUsage, cacheAllocCount);
        ret = g2d_free(cachedBuf);
        IMX2D_Assert(ret == 0);
    }

    cacheVector.push_back(buf);
    cacheUsage += buf->buf_size;
    cacheAllocCount++;

    IMX2D_Assert(cacheAllocCount == cacheVector.size());
    IMX2D_Assert(cacheUsage <= cacheUsageMax);
    IMX2D_Assert(cacheAllocCount <= cacheAllocCountMax );

    CACHE_LOG("%s(%d) sz:%d va:%p u:%zu c:%d (cached)",
              __func__, cacheable, buf->buf_size, buf->buf_vaddr,
              cacheUsage, cacheAllocCount);

    return;

free_buf:
    lock.unlock();
    CACHE_LOG("%s(%d) sz:%d va:%p u:%zu c:%d (freed)",
              __func__, cacheable, buf->buf_size, buf->buf_vaddr,
              cacheUsage, cacheAllocCount);

    ret = g2d_free(buf);
    IMX2D_Assert(ret == 0);
}

void G2dBufPoolInstance::drainCacheNoLock()
{
    struct g2d_buf *buf;
    size_t size;
    int ret;

    while (!cacheVector.empty())
    {
        buf = cacheVector[cacheVector.size() - 1];
        size = buf->buf_size;
        cacheUsage -= size;
        cacheAllocCount --;
        cacheVector.pop_back();

        CACHE_LOG("%s(%d) sz:%d u:%zu c:%d (cache purge)",
                  __func__, cacheable, buf->buf_size, cacheUsage, cacheAllocCount);

        ret = g2d_free(buf);
        IMX2D_Assert(ret == 0);
    }

    IMX2D_Assert(cacheAllocCount == 0);
    IMX2D_Assert(cacheUsage == 0);
}

void G2dBufPoolInstance::setUseCache(bool flag)
{
    std::unique_lock<std::mutex> lock(mutex);

    if (!flag && cacheEnabled)
        drainCacheNoLock();

    cacheEnabled = flag;
}

void G2dBufPoolInstance::setCacheConfig(size_t _cacheUsageMax,
                                        unsigned _cacheAllocCountMax)
{
    std::unique_lock<std::mutex> lock(mutex);

    drainCacheNoLock();

    cacheUsageMax = _cacheUsageMax;
    cacheAllocCountMax = _cacheAllocCountMax;
}

size_t G2dBufPoolInstance::getCacheUsage()
{
    return cacheUsage;
}

unsigned G2dBufPoolInstance::getCacheAllocations()
{
    return cacheAllocCount;
}


class G2dBufPool
{
public:
    G2dBufPool();
    virtual ~G2dBufPool();

    void setUseCache(bool flag);
    struct g2d_buf* alloc(size_t size, bool cacheable);
    void free(g2d_buf* buf, bool cacheable);
    void setCacheConfig(size_t _cacheUsageMax, unsigned _cacheAllocCountMax);
    size_t getCacheUsage(bool cacheable);
    unsigned getCacheAllocations(bool cacheable);

protected:
    G2dBufPoolInstance cachedPool;
    G2dBufPoolInstance uncachedPool;
};

G2dBufPool::G2dBufPool() : cachedPool(G2dBufPoolInstance(true)),
                           uncachedPool(G2dBufPoolInstance(false))
{
}

G2dBufPool::~G2dBufPool()
{
}

void G2dBufPool::setUseCache(bool flag)
{
    cachedPool.setUseCache(flag);
    uncachedPool.setUseCache(flag);
}

struct g2d_buf* G2dBufPool::alloc(size_t size, bool cacheable)
{
    G2dBufPoolInstance& pool = cacheable ? cachedPool : uncachedPool;
    return pool.alloc(size);
}

void G2dBufPool::free(struct g2d_buf* buf, bool cacheable)
{
    G2dBufPoolInstance& pool = cacheable ? cachedPool : uncachedPool;
    return pool.free(buf);
}

void G2dBufPool::setCacheConfig(size_t _cacheUsageMax,
                                unsigned _cacheAllocCountMax)
{
    cachedPool.setCacheConfig(_cacheUsageMax, _cacheAllocCountMax);
    uncachedPool.setCacheConfig(_cacheUsageMax,_cacheAllocCountMax);
}

size_t G2dBufPool::getCacheUsage(bool cacheable)
{
    G2dBufPoolInstance& pool = cacheable ? cachedPool : uncachedPool;
    return pool.getCacheUsage();
}

unsigned G2dBufPool::getCacheAllocations(bool cacheable)
{
    G2dBufPoolInstance& pool = cacheable ? cachedPool : uncachedPool;
    return pool.getCacheAllocations();
}


//================================= Imx2dGAllocator ====================================

Imx2dGAllocator::Imx2dGAllocator(): enableCount(0), allocCount(0), usage(0)
{
    g2dBufRepoPtr = std::make_shared<G2dBufRepo>(G2dBufRepo());
    g2dBufPoolPtr = std::make_shared<G2dBufPool>(G2dBufPool());
}

Imx2dGAllocator::~Imx2dGAllocator()
{
}

void Imx2dGAllocator::enable()
{
    std::unique_lock<std::mutex> lock(mutex);
    enableCount++;
    if (enableCount == 1)
        g2dBufPoolPtr.get()->setUseCache(true);
}

void Imx2dGAllocator::disable()
{
    std::unique_lock<std::mutex> lock(mutex);
    IMX2D_Assert(enableCount > 0);
    enableCount--;
    if (enableCount == 0)
        g2dBufPoolPtr.get()->setUseCache(false);
}

void* Imx2dGAllocator::alloc(size_t size, bool cacheable, void*& handle)
{
    struct g2d_buf* buf;

    buf = g2dBufPoolPtr.get()->alloc(size, cacheable);

    if (!buf)
    {
        handle = nullptr;
        return nullptr;
    }
    else
    {
        handle = static_cast<void*>(buf);
    }

    g2dBufRepoPtr.get()->registerDescriptor(buf, cacheable);

    {
        std::unique_lock<std::mutex> lock(mutex);
        allocCount++;
        usage += buf->buf_size;
    }

    return buf->buf_vaddr;
}

void Imx2dGAllocator::free(void* handle)
{
    struct g2d_buf* buf = static_cast<struct g2d_buf*>(handle), * _buf;
    bool isG2dBuf;
    bool cacheable;

    // get cacheable attribute of the buffer for proper routing
    isG2dBuf = g2dBufRepoPtr.get()->isVaddrG2dBuf(buf->buf_vaddr,
                                                  _buf, cacheable);
    IMX2D_Assert(isG2dBuf);
    IMX2D_Assert(buf == _buf);

    g2dBufRepoPtr.get()->unregisterDescriptor(buf);

    {
        std::unique_lock<std::mutex> lock(mutex);
        allocCount--;
        usage -= buf->buf_size;
    }

    g2dBufPoolPtr.get()->free(buf, cacheable);
}

bool Imx2dGAllocator::isGraphicBuffer(void* vaddr, void*& handle,
        bool& cacheable)
{
    std::unique_lock<std::mutex> lock(mutex);
    struct g2d_buf* buf;
    bool ret;

    ret = g2dBufRepoPtr.get()->isVaddrG2dBuf(vaddr, buf, cacheable);
    handle = ret ? static_cast<void*>(buf) : nullptr;

    return ret;
}

size_t Imx2dGAllocator::getUsage()
{
    return usage;
}

unsigned Imx2dGAllocator::getAllocations()
{
    return allocCount;
}

size_t Imx2dGAllocator::getCacheUsage(bool cacheable)
{
    return g2dBufPoolPtr.get()->getCacheUsage(cacheable);
}

unsigned Imx2dGAllocator::getCacheAllocations(bool cacheable)
{
    return g2dBufPoolPtr.get()->getCacheAllocations(cacheable);
}

void Imx2dGAllocator::setCacheConfig(size_t cacheUsageMax,
                                     unsigned cacheAllocCountMax)
{
    return g2dBufPoolPtr.get()->setCacheConfig(cacheUsageMax,
                                               cacheAllocCountMax);
}

Imx2dGAllocator& Imx2dGAllocator::getInstance()
{
    static Imx2dGAllocator instance;
    return instance;
}


//============================= Imx2dHalCounters ================================

void Imx2dHalCounters::incrementCount(Imx2dHalCounters::Primitive primitive)
{
    counters[primitive]++;
}

unsigned int Imx2dHalCounters::readCount(Imx2dHalCounters::Primitive primitive)
{
    return counters[primitive];
}


//================================= HardwareCapabilities ====================================

HardwareCapabilities::HardwareCapabilities(): supported(false),
                                              caps()
{
    const char *SYS_DEVICE_SOC_ID_FILE_PATH = "/sys/devices/soc0/soc_id";

    std::ifstream ifs(SYS_DEVICE_SOC_ID_FILE_PATH);
    if (ifs.fail())
    {
        std::cerr << "Can not open " << SYS_DEVICE_SOC_ID_FILE_PATH << std::endl;
        return;
    }

    std::stringstream buf;
    buf << ifs.rdbuf();
    std::string soc = buf.str();
    std::string trim ("\t\f\v\n\r");
    size_t offset = soc.find_last_not_of(trim);
    if (offset != std::string::npos)
        soc.erase(offset + 1);
    else
        soc.clear();

    // supported SoCs
    std::vector<std::string> socs = {"i.MX8MP", "i.MX93", "i.MX8QM", "i.MX8QXP"};
    bool support = (std::find(socs.begin(), socs.end(), soc) != socs.end());
    if (!support)
    {
        std::cerr << "SoC not supported [" << soc << "]" << std::endl;
        return;
    }
    supported = true;

    // 3 channels support on DPU
    std::vector<std::string> threeChans = {"i.MX8QM", "i.MX8QXP"};
    support = (std::find(threeChans.begin(), threeChans.end(), soc) !=
                   threeChans.end());
    caps[THREE_CHANNELS] = support;
}

bool HardwareCapabilities::hasSupport()
{
    return supported;
}

bool HardwareCapabilities::hasCapability(Capabilities cap)
{
    return caps[cap];
}


//================================= Imx2dHal ====================================

Imx2dHal::Imx2dHal(): enabled(false), g2dHandle(nullptr) {}

Imx2dHal::~Imx2dHal() {}

void Imx2dHal::setEnable(bool flag)
{
    int ret;
    Imx2dGAllocator& gAllocator = Imx2dGAllocator::getInstance();

    std::unique_lock<std::mutex> lock(mutex);

    if (flag == enabled)
        return;
    enabled = flag;

    if (flag)
    {
        IMX2D_Assert(hwCapabilities.hasSupport());

        ret = g2d_open(&g2dHandle);
        IMX2D_Assert(ret == 0);

        gAllocator.enable();
    }
    else
    {
        ret = g2d_close(g2dHandle);
        IMX2D_Assert(ret == 0);
        g2dHandle = nullptr;

        gAllocator.disable();
    }
}

bool Imx2dHal::isEnabled()
{
    return enabled;
}

HardwareCapabilities& Imx2dHal::getHardwareCapabilities()
{
    return hwCapabilities;
}

void* Imx2dHal::getG2dHandle()
{
    std::unique_lock<std::mutex> lock(mutex);
    IMX2D_Assert(enabled);
    return g2dHandle;
}

Imx2dHal& Imx2dHal::getInstance()
{
    static Imx2dHal instance;
    return instance;
}

}} // cv::imx2d::
