#ifndef _WALLOC_HPP
#define _WALLOC_HPP

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <memory.h>
#include <stdlib.h>
#include <atomic>

static inline void _walloc_mfence() { std::atomic_thread_fence(std::memory_order_seq_cst); }

#ifndef WALLOC_CUSTOMHEADER
struct WAllocHeader
{
    size_t size;
    WAllocHeader * next;
};
#endif

#define dbgassert(X) assert(X)
//#define dbgassert(X) (void)(X)

typedef WAllocHeader * WAllocHeaderPtr;

#ifndef WALLOC_CUSTOMALIGN
#define WALLOC_CUSTOMALIGN 16
#endif

#ifndef WALLOC_PAD
#define WALLOC_PAD 0
#endif

// WALLOC_SYS_MALLOC

// WALLOC_NOHEADERNOPOISON
// WALLOC_CACHEHINT
// WALLOC_NOZERO
// WALLOC_LINUX_NO_PROT_NONE
// WALLOC_LINUX_NO_MADV_FREE

#ifndef WALLOC_SLOW
#define WALLOC_LINUX_NO_PROT_NONE // up to a 3x or 5x slowdown if not defined
#endif

#ifdef WALLOC_FASTISH
#define WALLOC_LINUX_PREUNPROTECT
#define WALLOC_LINUX_NO_PROT_NONE
#endif

#ifdef WALLOC_FAST_SAFE
#define WALLOC_LINUX_PREUNPROTECT
#define WALLOC_LINUX_NO_PROT_NONE
#define WALLOC_LINUX_NO_MADV_FREE
#define WALLOC_WINDOWS_NO_DECOMMIT
#endif

#ifdef WALLOC_FAST
#define WALLOC_LINUX_PREUNPROTECT
#define WALLOC_NOZERO
#define WALLOC_LINUX_NO_PROT_NONE
#endif

#ifdef WALLOC_MAXIMUM_FAST
#define WALLOC_LINUX_PREUNPROTECT
#define WALLOC_NOZERO
#define WALLOC_LINUX_NO_PROT_NONE
#define WALLOC_LINUX_NO_MADV_FREE
#define WALLOC_WINDOWS_NO_DECOMMIT
#endif

#include <thread>
struct WMallocSpinLock
{
    std::atomic_flag locked = ATOMIC_FLAG_INIT;
    void lock()
    {
        if (!locked.test_and_set(std::memory_order_acq_rel))
            return;
        do {
            std::this_thread::yield();
        } while (locked.test_and_set(std::memory_order_acq_rel));
    }
    void unlock() { locked.clear(std::memory_order_release); }
};
#include <mutex>
//static std::mutex _walloc_free_mtx; // global freelist mutex
static WMallocSpinLock _walloc_free_mtx; // global freelist mutex

//static std::mutex _walloc_commit_mtx;
static WMallocSpinLock _walloc_commit_mtx;

static size_t _walloc_os_page_size = 0;
static size_t _walloc_os_commit_span = 0;
static size_t _walloc_os_page_size_log2 = 0;
static char * _walloc_heap_base = 0;
static std::atomic<char *> _walloc_heap_cur = 0;
static std::atomic<char *> _walloc_heap_top = 0;

// global freelist
static std::atomic<size_t> _walloc_heap_free_cap[64] = {};
static std::atomic<WAllocHeaderPtr> _walloc_heap_free[64] = {};
static std::atomic<WAllocHeaderPtr> _walloc_heap_free_backs[64] = {};

// threadlocal freelists
static thread_local size_t _walloc_heap_free_cap_tls[64] = {};
static thread_local WAllocHeaderPtr _walloc_heap_free_backs_tls[64] = {};
struct WAllocFreeList {
    WAllocFreeList()
    {
        for (size_t i = 0; i < 64; i++)
            dbgassert(!_walloc_heap_free_backs_tls[i]);
        for (size_t i = 0; i < 64; i++)
            dbgassert(!list[i]);
    }
    WAllocHeaderPtr list[64] = {};
    WAllocHeaderPtr & operator[](size_t n) { return list[n]; }
    const WAllocHeaderPtr & operator[](size_t n) const { return list[n]; }
    ~WAllocFreeList();
};
static thread_local WAllocFreeList _walloc_heap_free_tls;
WAllocFreeList * _getfreelist()
{
    return &_walloc_heap_free_tls;
}
size_t * _getfreelistcap()
{
    return _walloc_heap_free_cap_tls;
}

static inline void _walloc_flush_freelists();
WAllocFreeList::~WAllocFreeList() { _walloc_flush_freelists(); }

static inline void _walloc_flush_freelists()
{
    #ifndef WALLOC_GLOBAL_FREELIST
    
    //puts("flush...");
    
    for (size_t bin = 0; bin < 64; bin++)
    {
        _walloc_free_mtx.lock();
        
        auto last = _walloc_heap_free_backs_tls[bin];
        if (!last)
        {
            _walloc_free_mtx.unlock();
            continue;
        }
        
        auto top = _walloc_heap_free_tls[bin];
        auto cap = _walloc_heap_free_cap_tls[bin];
        //printf("%d\n", bin);
        assert(cap);
        assert(top);
        
        last->next = _walloc_heap_free[bin].exchange(top, std::memory_order_acq_rel);
        _walloc_heap_free_cap[bin].fetch_add(cap, std::memory_order_acq_rel);
        if (!_walloc_heap_free_backs[bin].load(std::memory_order_acquire))
            _walloc_heap_free_backs[bin].store(_walloc_heap_free_backs_tls[bin], std::memory_order_release);
        
        _walloc_free_mtx.unlock();
        
        _walloc_heap_free_tls[bin] = 0;
        _walloc_heap_free_backs_tls[bin] = 0;
        _walloc_heap_free_cap_tls[bin] = 0;
    }
    
    #endif
}

struct CharP { char * p; };

#define WALLOC_OFFS (WALLOC_CUSTOMALIGN > sizeof(WAllocHeader) ? WALLOC_CUSTOMALIGN : sizeof(WAllocHeader))

#if (__has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__))
#include <sanitizer/asan_interface.h>
void _walloc_asan_poison(void * a, size_t n)
{
    #ifndef WALLOC_NOHEADERNOPOISON
    if (a) ASAN_POISON_MEMORY_REGION(a, n);
    #endif
}
void _walloc_asan_unpoison(void * a, size_t n) { if (a) ASAN_UNPOISON_MEMORY_REGION(a, n); }
#else
void _walloc_asan_unpoison(void *, size_t) { }
void _walloc_asan_poison(void *, size_t) { }
#endif

inline static void _walloc_rawmal_inner_pages(char ** p, size_t * s)
{
    char * p2 = *p;
    char * end = *p + *s;
    p2 += _walloc_os_page_size-1;
    p2 = (char *)(size_t(p2) >> _walloc_os_page_size_log2);
    p2 = (char *)(size_t(p2) << _walloc_os_page_size_log2);
    *p = 0;
    *s = 0;
    if (end < p2 + _walloc_os_page_size)
        return;
    size_t byte_count = end-p2;
    size_t page_count = byte_count >> _walloc_os_page_size_log2;
    *p = p2;
    *s = page_count << _walloc_os_page_size_log2;
}

static inline void _walloc_commit(char * p, size_t n);
static inline void _walloc_decommit(char * p, size_t n);
static inline void _walloc_recommit(char * p, size_t n);
static inline bool _malloc_commit_needed();
extern "C" __attribute__((__malloc__)) void * _walloc_raw_malloc(size_t n);
extern "C" __attribute__((__malloc__)) void * _walloc_raw_calloc(size_t c, size_t n);
extern "C" __attribute__((__malloc__)) void * _walloc_raw_realloc(void * p, size_t n);
extern "C" void _walloc_raw_free(void * p);

//size_t _walloc_shared_to_tls_cap = getenv("WALLOC_A") ? atoi(getenv("WALLOC_A")) : (256);
//size_t _walloc_tls_to_shared_trigger = getenv("WALLOC_B") ? atoi(getenv("WALLOC_B")) : (2048);
//size_t _walloc_tls_to_shared_cap_keep = getenv("WALLOC_C") ? atoi(getenv("WALLOC_C")) : (256);
#ifdef WALLOC_PULL_OVERRIDE
const size_t _walloc_shared_to_tls_cap = WALLOC_PULL_OVERRIDE;
#else
const size_t _walloc_shared_to_tls_cap = (256*64);
#endif

#ifdef WALLOC_FLUSH_OVERRIDE
const size_t _walloc_tls_to_shared_trigger = WALLOC_FLUSH_OVERRIDE;
#else
const size_t _walloc_tls_to_shared_trigger = (4096*32);
#endif

#ifdef WALLOC_FLUSH_KEEP_OVERRIDE
const size_t _walloc_tls_to_shared_cap_keep = WALLOC_FLUSH_KEEP_OVERRIDE;
#else
const size_t _walloc_tls_to_shared_cap_keep = 0;
#endif

static std::atomic_ptrdiff_t total_alloc_ever_size = 0;
static std::atomic_ptrdiff_t total_freed_ever_size = 0;

extern "C" __attribute__((__malloc__)) void * _walloc_raw_malloc(size_t n)
{
    size_t orig_n = n;
    (void)orig_n; // only used in some ifdef cases
    
    #ifdef WALLOC_SYS_MALLOC
    #ifdef WALLOC_NOZERO
        return malloc(n);
    #else
        return calloc(1, n);
    #endif
    
    #else
    
    if (!n || n > 0x100000000000) return 0;
    if (n < 8) n = 8;
    //n = std::bit_ceil(n);
    
    n = 1ULL << (64-__builtin_clzll(n-1));
    
    //total_alloc_ever_size.fetch_add(n, std::memory_order_acq_rel);
    
    int bin = __builtin_ctzll(n);
    
    #ifdef WALLOC_GLOBAL_FREELIST
    
    if (_walloc_heap_free[bin].load(std::memory_order_acquire))
    {
        _walloc_free_mtx.lock();
        _walloc_mfence();
        
        char * p = (char *)_walloc_heap_free[bin].load(std::memory_order_acquire);
        if (p)
            _walloc_heap_free[bin].store((WAllocHeaderPtr)WAllocHeaderPtr(p)->next, std::memory_order_release);
        
        _walloc_mfence();
        _walloc_free_mtx.unlock();
        
        if (p)
        {
            WAllocHeaderPtr(p)->next = 0;
            //_walloc_heap_free_cap[bin].fetch_sub(n, std::memory_order_acq_rel);
            
            char * p2 = p+WALLOC_OFFS;
            size_t s = n;
            _walloc_rawmal_inner_pages(&p2, &s);
            _walloc_recommit(p2, s);
            
            memset(p, 0, WALLOC_OFFS);
            #ifndef WALLOC_NOZERO
            memset(p, 0, n + WALLOC_OFFS);
            #endif
            
            WAllocHeaderPtr(p)->size = n;
            
            return (void *)(p+WALLOC_OFFS);
        }
    }
    
    #else
    
    assert(bin != 0);
    
    if (!_walloc_heap_free_tls[bin])
    {
        dbgassert(!_walloc_heap_free_backs_tls[bin]);
        auto temp = _walloc_heap_free_cap[bin].load(std::memory_order_acquire);
        if (temp >= 1)
        {
            auto cap = _walloc_shared_to_tls_cap;
            
            // try to acquire up to _walloc_shared_to_tls_cap worth of objects
            // (might fail and produce no objects and that's OK)
            WAllocHeaderPtr top = 0;
            WAllocHeaderPtr back = 0;
            size_t totalsize = 0;
            
            _walloc_free_mtx.lock();
            top = _walloc_heap_free[bin].load(std::memory_order_acquire);
            back = _walloc_heap_free_backs[bin].load(std::memory_order_acquire);
            totalsize = _walloc_heap_free_cap[bin].load(std::memory_order_acquire);
            
            if (top)
            {
                #if 0
                auto last = back;
                size_t consumed = totalsize;
                #else
                auto last = top;
                size_t consumed = n;
                while (last->next && consumed < cap)
                {
                    last = (WAllocHeaderPtr)last->next;
                    consumed += n;
                }
                #endif
                
                if (!last->next) [[unlikely]]
                    back = 0;
                totalsize -= consumed;
                
                _walloc_heap_free[bin].store((WAllocHeaderPtr)last->next, std::memory_order_release);
                _walloc_heap_free_backs[bin].store(back, std::memory_order_release);
                _walloc_heap_free_cap[bin].store(totalsize, std::memory_order_release);
                
                _walloc_free_mtx.unlock();
                
                std::atomic_ref(last->next).store(0, std::memory_order_release);
                
                _walloc_heap_free_tls[bin] = top;
                _walloc_heap_free_backs_tls[bin] = last;
                _walloc_heap_free_cap_tls[bin] += consumed;
                //printf("pulling freelist %zd %zd %p\n", _walloc_heap_free_cap[bin].load(), _walloc_heap_free_cap_tls[bin], last->next);
            }
            else
            {
                _walloc_free_mtx.unlock();
            }
        }
    }
    else
        dbgassert(_walloc_heap_free_backs_tls[bin]);
    
    if (_walloc_heap_free_tls[bin])
    {
        char * p = (char *)_walloc_heap_free_tls[bin];
        dbgassert(p);
        if (p)
        {
            //total_freed_ever_size.fetch_sub(n, std::memory_order_acq_rel);
            
            _walloc_heap_free_tls[bin] = (WAllocHeaderPtr)WAllocHeaderPtr(p)->next;
            if (!_walloc_heap_free_tls[bin])
                _walloc_heap_free_backs_tls[bin] = 0;
            WAllocHeaderPtr(p)->next = 0;
            _walloc_heap_free_cap_tls[bin] -= n;
            if (_walloc_heap_free_cap_tls[bin])
                dbgassert(_walloc_heap_free_backs_tls[bin]);
            else
                dbgassert(!_walloc_heap_free_backs_tls[bin]);
            
            char * p2 = p+WALLOC_OFFS;
            size_t s = n;
            _walloc_rawmal_inner_pages(&p2, &s);
            _walloc_recommit(p2, s);
            
            size_t nbad = std::atomic_ref(WAllocHeaderPtr(p)->size).load();
            dbgassert(!nbad); // double allocation
            
            memset(p, 0, WALLOC_OFFS);
            
            #ifndef WALLOC_NOZERO
            memset(p, 0, n + WALLOC_OFFS);
            //for (int i = 0; i < orig_n; i++)
            //    *((volatile char * volatile)(p+WALLOC_OFFS)) = 0x6B;
            #endif
            
            WAllocHeaderPtr(p)->size = n;
            
            return (void *)(p+WALLOC_OFFS);
        }
    }
    
    #endif
    
    // n is basic allocation size (rounded up to a power of 2)
    // n2 is how many bytes in the heap we actually consume
    auto n2 = n + WALLOC_OFFS + WALLOC_PAD;
    n2 = (n2+(WALLOC_CUSTOMALIGN-1)) & ~(WALLOC_CUSTOMALIGN-1);
    #ifdef WALLOC_CACHEHINT
    n2 = (n2+(WALLOC_CACHEHINT-1)) & ~(WALLOC_CACHEHINT-1);
    #endif
    //printf("WALLOC: bumping by %zd\n", n2);
    char * p = _walloc_heap_cur.fetch_add(n2);
    
    if (_malloc_commit_needed())
    {
        char * top = _walloc_heap_top.load(std::memory_order_acquire);
        ptrdiff_t over = p + n2 - top;
        if (over > 0)
        {
            _walloc_commit_mtx.lock();
            top = _walloc_heap_top.load(std::memory_order_acquire);
            over = p + n2 - top;
            over = (over + (_walloc_os_commit_span - 1)) & ~(_walloc_os_commit_span-1);
            if (over > 0)
            {
                _walloc_commit(top, over);
                _walloc_heap_top.store(top + over, std::memory_order_release);
            }
            _walloc_commit_mtx.unlock();
        }
    }
    
    memset(p, 0, WALLOC_OFFS);
    
    WAllocHeaderPtr(p)->next = 0;
    WAllocHeaderPtr(p)->size = n;
    dbgassert(WAllocHeaderPtr(p)->size);
    
    //std::atomic_thread_fence(std::memory_order_seq_cst);
    
    return (void *)(p+WALLOC_OFFS);
    #endif
}
extern "C" __attribute__((__malloc__)) void * _walloc_raw_calloc(size_t c, size_t n)
{
    #ifdef WALLOC_SYS_MALLOC
    return calloc(c, n);
    
    #else
    
    auto r = _walloc_raw_malloc(c*n);
    #ifndef WALLOC_NOZERO
    memset(r, 0, c*n);
    #endif
    return r;
    #endif
}
extern "C" __attribute__((__malloc__)) void * _walloc_raw_realloc(void * p, size_t n)
{
    #ifdef WALLOC_SYS_MALLOC
    return realloc(p, n);
    
    #else
    auto r = _walloc_raw_malloc(n);
    if (!p) return r;
    char * _p = ((char *)p)-WALLOC_OFFS;
    size_t s = WAllocHeaderPtr(_p)->size;
    dbgassert(WAllocHeaderPtr(_p)->size);
    
    // the user is allowed to store 8 bits of info inside of the `size` field of the allocation header
    s <<= 8;
    s >>= 8;
    
    if (s < n) n = s;
    memcpy(r, p, n);
    return r;
    #endif
}

extern "C" void _walloc_raw_free(void * _p)
{
    #ifdef WALLOC_SYS_MALLOC
    return free(_p);
    
    #else
    
    if (!_p) return;
    char * p = ((char *)_p)-WALLOC_OFFS;
    
    size_t n = WAllocHeaderPtr(p)->size;
    size_t _n = n; // for viewing with debuggers
    (void) _n;
    
    dbgassert(n); // double free
    
    std::atomic_ref(WAllocHeaderPtr(p)->size).store(0);
    // the user is allowed to store 8 bits of info inside of the `size` field of the allocation header
    n <<= 8;
    n >>= 8;
    
    //n = std::bit_ceil(n);
    n = 1ULL << (64-__builtin_clzll(n-1));
    
    //total_alloc_ever_size.fetch_sub(n, std::memory_order_acq_rel);
    //total_freed_ever_size.fetch_add(n, std::memory_order_acq_rel);
    
    int bin = __builtin_ctzll(n);
    //printf("%zd\n", bin);
    
    _walloc_decommit(p, n+WALLOC_OFFS);
    
    //std::atomic_thread_fence(std::memory_order_seq_cst);
    
    auto gp = WAllocHeaderPtr(p);
    
    #ifdef WALLOC_GLOBAL_FREELIST
    
    _walloc_free_mtx.lock();
    gp->next = _walloc_heap_free[bin].exchange(gp, std::memory_order_acq_rel);
    _walloc_free_mtx.unlock();
    
    #else
    
    assert(bin != 0);
    
    gp->next = _walloc_heap_free_tls[bin];
    _walloc_heap_free_tls[bin] = gp;
    if (!_walloc_heap_free_backs_tls[bin])
        _walloc_heap_free_backs_tls[bin] = _walloc_heap_free_tls[bin];
    
    _walloc_heap_free_cap_tls[bin] += n;
    
    if (_walloc_heap_free_cap_tls[bin] >= _walloc_tls_to_shared_trigger)
    {
        // dump our freelist onto the shared one
        
        auto last = _walloc_heap_free_tls[bin];
        size_t consumed = n;
        if (_walloc_tls_to_shared_cap_keep == 0)
        {
            last = _walloc_heap_free_backs_tls[bin];
            consumed = _walloc_heap_free_cap_tls[bin];
            _walloc_heap_free_cap_tls[bin] = 0;
            _walloc_heap_free_backs_tls[bin] = 0;
            assert(!last->next);
        }
        else
        {
            _walloc_heap_free_cap_tls[bin] -= n;
            while (last->next && _walloc_heap_free_cap_tls[bin] > _walloc_tls_to_shared_cap_keep)
            {
                last = (WAllocHeaderPtr)last->next;
                consumed += n;
                _walloc_heap_free_cap_tls[bin] -= n;
            }
        }
        
        auto top = _walloc_heap_free_tls[bin];
        _walloc_heap_free_tls[bin] = (WAllocHeaderPtr)last->next;
        if (_walloc_tls_to_shared_cap_keep == 0)
            dbgassert(!last->next);
        
        _walloc_free_mtx.lock();
        
        last->next = _walloc_heap_free[bin].exchange(top, std::memory_order_acq_rel);
        _walloc_heap_free_cap[bin].fetch_add(consumed, std::memory_order_acq_rel);
        
        if (!_walloc_heap_free_backs[bin].load(std::memory_order_acquire))
        {
            _walloc_heap_free_backs[bin].store(last, std::memory_order_release);
            dbgassert(top);
            dbgassert(!last->next);
        }
        else
        {
            dbgassert(last->next);
        }
        
        _walloc_free_mtx.unlock();
        
        if (_walloc_tls_to_shared_cap_keep == 0)
            assert(!_walloc_heap_free_tls[bin]);
        
        if (!_walloc_heap_free_tls[bin])
            _walloc_heap_free_backs_tls[bin] = 0;
    }
    
    #endif
    //else assert(n < _walloc_tls_to_shared_trigger);
    
    //printf("%p %zd\n", _walloc_heap_free[bin].load().next, _walloc_heap_free[bin].load().tag);
    
    //std::atomic_thread_fence(std::memory_order_seq_cst);
    
    #endif
}

#ifdef _WIN32

///////
/////// platform-specific Windows stuff ///////
///////

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <windows.h>
#include <psapi.h>

static inline CharP _walloc_os_init_heap()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    _walloc_os_page_size = info.dwPageSize;
    _walloc_os_commit_span = _walloc_os_page_size;
    _walloc_os_page_size_log2 = 64-__builtin_clzll(_walloc_os_page_size-1);
    // 16 TiB should be enough for anybody
    auto ret = (char *)VirtualAlloc(0, 0x100000000000, MEM_RESERVE, PAGE_READWRITE);
    assert(ret);
    _walloc_heap_base = ret;
    _walloc_heap_cur.store(ret);
    _walloc_heap_top.store(ret);
    //for (size_t i = 0; i < 64; i++)
    //    assert(_walloc_heap_free[i] == 0);
    return CharP{ret};
}
static CharP _walloc_heap_base_s __attribute__((init_priority(101))) = _walloc_os_init_heap();

static inline bool _malloc_commit_needed()
{
    return true;
}
static inline void _walloc_commit(char * loc, size_t over)
{
    std::atomic<char *> last_end = 0;
    assert(loc + over >= last_end.load());
    assert(VirtualAlloc(loc, over, MEM_COMMIT, PAGE_READWRITE));
    last_end.store(loc + over);
}
static inline void _walloc_recommit(char * p2, size_t s)
{
    (void)p2;
    (void)s;
    #ifndef WALLOC_WINDOWS_NO_DECOMMIT
    if (s)
        assert(VirtualAlloc(p2, s, MEM_COMMIT, PAGE_READWRITE));
    #endif
}
static inline void _walloc_decommit(char * p, size_t n)
{
    n -= WALLOC_OFFS;
    char * p2 = p+WALLOC_OFFS;
    size_t s = n;
    _walloc_rawmal_inner_pages(&p2, &s);
#ifdef WALLOC_WINDOWS_NO_DECOMMIT
    #ifndef WALLOC_NOZERO
    memset(p, 0, n + WALLOC_OFFS);
    #endif
#else
    if (s)
    {
        #ifndef WALLOC_NOZERO
        memset(p, 0, p2-p);
        memset(p2+s, 0, p+n+WALLOC_OFFS-(p2+s));
        #endif
        
        VirtualFree(p2, s, MEM_DECOMMIT);
    }
    else
    {
        #ifndef WALLOC_NOZERO
        memset(p, 0, n + WALLOC_OFFS);
        #endif
    }
#endif
}
#else // WIN32 -> LINUX

///////
/////// platform-specific Linux stuff ///////
///////

#include <sys/mman.h>
#include <linux/mman.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <ucontext.h>

static inline CharP _walloc_os_init_heap()
{
    _walloc_os_page_size = (size_t)sysconf(_SC_PAGESIZE);
    _walloc_os_commit_span = _walloc_os_page_size*8;
    _walloc_os_page_size_log2 = 64-__builtin_clzll(_walloc_os_page_size-1);
    // 16 TiB should be enough for anybody
    auto flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
    #ifdef WALLOC_NOZERO
    flags |= MAP_UNINITIALIZED;
    #endif
    #ifdef WALLOC_LINUX_PREUNPROTECT
    auto mode = PROT_READ | PROT_WRITE;
    #else
    auto mode = PROT_NONE;
    #endif
    auto ret = (char *)mmap(0, 0x100000000000, mode, flags, -1, 0);
    assert((size_t)ret != (size_t)-1);
    _walloc_heap_base = ret;
    _walloc_heap_cur.store(ret);
    _walloc_heap_top.store(ret);
    //for (size_t i = 0; i < 64; i++)
    //    assert(_walloc_heap_free[i] == 0);
    return CharP{ret};
}
static CharP _walloc_heap_base_s __attribute__((init_priority(101))) = _walloc_os_init_heap();

static inline bool _malloc_commit_needed()
{
    #ifdef WALLOC_LINUX_PREUNPROTECT
    return false;
    #else
    return true;
    #endif
}
static inline void _walloc_commit(char * loc, size_t over)
{
    if (mprotect(loc, over, PROT_READ | PROT_WRITE))
        assert(0);
}
static inline void _walloc_recommit(char * p2, size_t s)
{
    (void)p2;
    (void)s;
#ifndef WALLOC_LINUX_NO_PROT_NONE
    if (s)
    {
        if (mprotect(p2, s, PROT_READ | PROT_WRITE))
            assert(0);
    }
#endif
}
static inline void _walloc_decommit(char * p, size_t n)
{
    _walloc_asan_unpoison(p, n);
    
    n -= WALLOC_OFFS;
    char * p2 = p+WALLOC_OFFS;
    size_t s = n;
    _walloc_rawmal_inner_pages(&p2, &s);
#ifdef WALLOC_LINUX_NO_MADV_FREE
    #ifndef WALLOC_NOZERO
    memset(p, 0, n + WALLOC_OFFS);
    #endif
#ifndef WALLOC_LINUX_NO_PROT_NONE
    if (s) mprotect(p2, s, PROT_NONE);
#endif
#else
    if (s)
    {
        #ifndef WALLOC_NOZERO
        memset(p, 0, p2-p);
        memset(p2+s, 0, p+n+WALLOC_OFFS-(p2+s));
        #endif
        
#ifndef WALLOC_LINUX_NO_PROT_NONE
        if (s) mprotect(p2, s, PROT_NONE);
#endif
        #ifdef WALLOC_NOZERO
        madvise(p2, s, MADV_FREE);
        #else
        madvise(p2, s, MADV_DONTNEED);
        #endif
    }
    else
    {
        #ifndef WALLOC_NOZERO
        memset(p, 0, n + WALLOC_OFFS);
        #endif
    }
#endif
}

#endif // else of WIN32

#endif // _WALLOC_HPP
