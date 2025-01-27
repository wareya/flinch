#ifndef _GC_OS_HPP
#define _GC_OS_HPP

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <windows.h>
#include <psapi.h>

struct Context { CONTEXT ctx; };

static inline Context *_gc_suspend_main_and_get_context()
{
    // suspend main thread to read its registers and stack
    static Context ctx = {};
    //ctx.ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
    ctx.ctx.ContextFlags = CONTEXT_INTEGER | CONTEXT_CONTROL | CONTEXT_FLOATING_POINT | CONTEXT_DEBUG_REGISTERS;
    DWORD rval = SuspendThread((HANDLE)_main_thread);
    int nx = 100000;
    while (rval == (DWORD)-1 && nx-- > 0)
        rval = SuspendThread((HANDLE)_main_thread);
    assert(rval != (DWORD)-1);
    
    assert(GetThreadContext((HANDLE)_main_thread, &ctx.ctx));
    //printf("rbp:%p\trsp:%p\trip:%p\tstack height:%zu\n", (void *)ctx.Rbp, (void *)ctx.Rsp, (void *)ctx.Rip, _main_stack_hi-ctx.Rsp);
    
    return &ctx;
}
static inline size_t _gc_context_get_rsp(Context * ctx)
{
    return (size_t)ctx->ctx.Rsp;
}
static inline size_t _gc_context_get_size()
{
    return sizeof(CONTEXT);
}
static inline void _gc_unsuspend_main()
{
    ResumeThread((HANDLE)_main_thread);
}
static inline void _gc_safepoint_impl_os()
{
    while (!_gc_baton_atomic) { fence(); }
    safepoint_mutex.unlock();
    while (_gc_baton_atomic) { fence(); }
    fence();
    safepoint_mutex.lock(); // this is the point at which the main thread gets """suspended"""
    // and unsuspended
    fence();
}
static inline void _gc_get_data_sections()
{
    static int found = 0;
    if (found) return;
    found = 1;
    
    HANDLE process = GetCurrentProcess();

    HMODULE modules[1024];
    DWORD cbNeeded;
    assert(EnumProcessModules(process, modules, sizeof(modules), &cbNeeded));
    for (size_t i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
    {
        MODULEINFO moduleInfo;
        assert(GetModuleInformation(process, modules[i], &moduleInfo, sizeof(moduleInfo)));
        
        // Iterate over memory regions in this module
        MEMORY_BASIC_INFORMATION memInfo;
        unsigned char* address = static_cast<unsigned char*>(moduleInfo.lpBaseOfDll);
        while (address < (static_cast<unsigned char*>(moduleInfo.lpBaseOfDll) + moduleInfo.SizeOfImage))
        {
            if (VirtualQuery(address, &memInfo, sizeof(memInfo)))
            {
                if (memInfo.State == MEM_COMMIT && (memInfo.Protect & PAGE_READWRITE))
                    gc_add_custom_root_region((void **)memInfo.BaseAddress, memInfo.RegionSize / sizeof(size_t));
                address += memInfo.RegionSize;
            }
            else
                break;
        }
    }
}

static inline void gc_run_startup()
{
    if (_main_thread != 0)
        return;
    
    HANDLE h;
    auto cprc = GetCurrentProcess();
    auto cthd = GetCurrentThread();
    assert(DuplicateHandle(cprc, cthd, cprc, &h, 0, FALSE, DUPLICATE_SAME_ACCESS));
    _main_thread = (size_t)h;
    
    printf("got main thread! %zd\n", _main_thread);
    
    ULONGLONG lo;
    ULONGLONG hi;
    GetCurrentThreadStackLimits(&lo, &hi);
    _main_stack_hi = (size_t)hi;
}

static size_t _gc_thread = 0;
extern "C" int gc_start()
{
    gc_run_startup();
    
    std::atomic_uint8_t dummy;
    if (!std::atomic_is_lock_free(&dummy))
        assert(((void)"atomic byte accesses must be lockfree", 0));
    
    wasted_seconds = 0.0;
    _gc_thread = (size_t)CreateThread(0, 0, &_gc_loop, 0, 0, 0);
    assert(_gc_thread);
    return 0;
}
extern "C" int gc_end()
{
    _gc_stop = 1;
    WaitForSingleObject((HANDLE)_gc_thread, 0);
    fence();
    CloseHandle((HANDLE)_gc_thread);
    _gc_thread = 0;
    
    fence();
    _gc_stop = 0;
    fence();
    
    printf("seconds wasted with GC thread blocking main thread: %.4f\n", wasted_seconds);
    printf("pause\tcmd\twhiten\troots\tmark\tsweep\thipause\thxtable_bits\n");
    printf("%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%zu\n", secs_pause, secs_cmd, secs_whiten, secs_roots, secs_mark, secs_sweep, max_pause_time, GC_TABLE_BITS);
    return 0;
}
struct GcCanary { GcCanary() { gc_start(); } ~GcCanary() { gc_end(); } };
static GcCanary _gc_canary = GcCanary();

static size_t _gc_os_page_size = 0;
static size_t _gc_os_page_size_log2 = 0;
static char * _gc_heap_base = 0;
static std::atomic<char *> _gc_heap_cur = 0;
static std::atomic<char *> _gc_heap_top = 0;
static std::atomic<char *> _gc_heap_free[64] = {};
struct CharP { char * p; };
static inline CharP _gc_os_init_heap()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    _gc_os_page_size = info.dwPageSize;
    _gc_os_page_size_log2 = 64-__builtin_clzll(_gc_os_page_size-1);
    printf("%zd\n", _gc_os_page_size_log2);
    // 16 TiB should be enough for anybody
    auto ret = (char *)VirtualAlloc(0, 0x100000000000, MEM_RESERVE, PAGE_READWRITE);
    assert(ret);
    _gc_heap_base = ret;
    _gc_heap_cur.store(ret);
    _gc_heap_top.store(ret);
    for (size_t i = 0; i < 64; i++)
        assert(_gc_heap_free[i] == 0);
    return CharP{ret};
}
static CharP _gc_heap_base_s __attribute__((init_priority(101))) = _gc_os_init_heap();

#include <bit>

#if (__has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)) && !defined(GC_NO_PREFIX)
#include <sanitizer/asan_interface.h>
void _gc_asan_poison(void * a, size_t n) { ASAN_POISON_MEMORY_REGION(a, n); }
void _gc_asan_unpoison(void * a, size_t n) { ASAN_UNPOISON_MEMORY_REGION(a, n); }
#else
void _gc_asan_unpoison(void *, size_t) { }
void _gc_asan_poison(void *, size_t) { }
#endif

inline static void _gc_rawmal_inner_pages(char ** p, size_t * s)
{
    char * p2 = *p;
    char * end = *p + *s;
    p2 += _gc_os_page_size-1;
    p2 = (char *)(size_t(p2) >> _gc_os_page_size_log2);
    p2 = (char *)(size_t(p2) << _gc_os_page_size_log2);
    *p = 0;
    *s = 0;
    if (end < p2 + _gc_os_page_size)
        return;
    size_t byte_count = end-p2;
    size_t page_count = byte_count >> _gc_os_page_size_log2;
    *p = p2;
    *s = page_count << _gc_os_page_size_log2;
}

extern "C" void * _gc_raw_malloc(size_t n)
{
    #ifndef GC_CUSTOM_MALLOC
    return malloc(n);
    
    #else
    
    if (!n || n > 0x100000000000) return 0;
    //n = std::bit_ceil(n);
    
    if (n < 32) n = 32;
    n = 1ULL << (64-__builtin_clzll(n-1));
    int bin = __builtin_ctzll(n);
    
    // any data race on this read will merely result in a missed freedlist reusage
    if (_gc_heap_free[bin].load(std::memory_order_relaxed))
    {
        char * p;
        do {
            p = _gc_heap_free[bin].load(std::memory_order_acquire);
        } while (p && !_gc_heap_free[bin].compare_exchange_weak(p, (char*)GcAllocHeaderPtr(p)->next, std::memory_order_release, std::memory_order_relaxed));
        
        if (p)
        {
            _gc_asan_unpoison(p, GCOFFS);
            
            char * p2 = p+GCOFFS;
            size_t s = n;
            _gc_rawmal_inner_pages(&p2, &s);
            if (s)
                VirtualAlloc(p2, s, MEM_COMMIT, PAGE_READWRITE);
            
            memset(p, 0, n + GCOFFS);
            GcAllocHeaderPtr(p)->size = n;
            _gc_asan_poison(p, GCOFFS);
            return (void *)(p+GCOFFS);
        }
    }
    
    // n is basic allocation size (rounded up to a power of 2)
    auto n2 = n + GCOFFS;
    char * p = _gc_heap_cur.fetch_add(n2, std::memory_order_acq_rel);
    // any race on the calulation of "over" will only result in a slight excess of committed memory, no unsafety
    ptrdiff_t over = p + n2 - _gc_heap_top.load(std::memory_order_relaxed);
    if (over > 0)
    {
        over = (over + (_gc_os_page_size - 1))/_gc_os_page_size*_gc_os_page_size;
        assert(VirtualAlloc(_gc_heap_top.fetch_add(over, std::memory_order_acq_rel), over, MEM_COMMIT, PAGE_READWRITE));
    }
    
    GcAllocHeaderPtr(p)->size = n;
    // already zeroed by the OS
    
    _gc_asan_poison(p, GCOFFS);
    
    return (void *)(p+GCOFFS);
    #endif
}
inline static void * _gc_raw_calloc(size_t c, size_t n)
{
    #ifndef GC_CUSTOM_MALLOC
    return calloc(c, n);
    
    #else
    
    auto r = _gc_raw_malloc(c*n);
    return r;
    #endif
}
inline static void _gc_raw_free(void * _p)
{
    #ifndef GC_CUSTOM_MALLOC
    return free(_p);
    
    #else
    
    if (!_p) return;
    char * p = ((char *)_p)-GCOFFS;
    
    _gc_asan_unpoison(p, GCOFFS);
    
    GcAllocHeaderPtr(p)->size <<= 8;
    GcAllocHeaderPtr(p)->size >>= 8;
    
    size_t n = GcAllocHeaderPtr(p)->size;
    //n = std::bit_ceil(n);
    n = 1ULL << (64-__builtin_clzll(n-1));
    
    char * p2 = p+GCOFFS;
    size_t s = n;
    _gc_rawmal_inner_pages(&p2, &s);
    if (s)
        VirtualFree(p2, s, MEM_DECOMMIT);
    
    int bin = __builtin_ctzll(n);
    
    auto gp = GcAllocHeaderPtr(p);
    do
    {
        gp->next = (size_t **)_gc_heap_free[bin].load(std::memory_order_acquire);
    } while (!_gc_heap_free[bin].compare_exchange_weak(*(char **)&gp->next, (char *)gp, std::memory_order_release, std::memory_order_relaxed));
    
    _gc_asan_poison(p, GCOFFS);
    #endif
}

#else // WIN32 -> LINUX

///////
/////// platform-specific Linux stuff ///////
///////

#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <ucontext.h>

struct Context { struct ucontext_t ctx; };
static Context _gc_ctx = {};

static pid_t _current_pid;

static inline Context * _gc_suspend_main_and_get_context()
{
    // context acquisition handled by main thread
    // suspension handled by lock
    return &_gc_ctx;
}
static inline size_t _gc_context_get_rsp(Context * ctx)
{
    return (size_t)ctx->ctx.uc_mcontext.gregs[REG_RSP];
}
static inline size_t _gc_context_get_size()
{
    return sizeof(Context);
}
static inline void _gc_unsuspend_main()
{
    // handled by lock
}

static inline void _gc_safepoint_impl_os()
{
    while (!_gc_baton_atomic) { }
    safepoint_mutex.unlock();
    assert(!getcontext(&_gc_ctx.ctx));
    while (_gc_baton_atomic) { }
    safepoint_mutex.lock(); // this is the point at which the main thread gets """suspended"""
    // and unsuspended
}

static inline size_t _gc_get_heap_start();
static inline void _gc_get_data_sections()
{
    static int found = 0;
    if (found) return;
    found = 1;
    
    FILE * maps = fopen("/proc/self/maps", "r");
    assert(maps);
    char line[256] = {};
    size_t heap_start = _gc_get_heap_start();
    while (fgets(line, sizeof(line), maps))
    {
        size_t i = 0;
        while (line[i] != '\t' && line[i] != ' ' && i < 255) i++;
        if (i > 200) return;
        if (line[i+1] != 'r' || line[i+2] != 'w') continue;
        
        char * l = &line[0];
        size_t start = std::strtoull(l, &l, 16);
        l += 1;
        size_t end = std::strtoull(l, &l, 16);
        
        l += 20;
        std::strtoull(l, &l, 10);
        while (*l == ' ' || *l == '\t') l++;
        if (strncmp(l, "[heap]", 6) == 0)
            continue;
        if (strncmp(l, "[stack]", 7) == 0)
            continue;
        if (heap_start >= start && heap_start < end)
            continue;
        
        gc_add_custom_root_region((void **)start, (end-start) / sizeof(size_t));
        //printf("%s", line);
        //printf("%zd\n", (end-start) / sizeof(size_t));
    }
    //printf("NOTE: our heap starts at %zX\n", heap_start);
}

static inline void gc_run_startup()
{
    if (_main_thread != 0)
        return;
    
    _current_pid = getpid();
    _main_thread = gettid();
    
    printf("got main thread! %zd\n", _main_thread);
    
    size_t lo;
    size_t size;
    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, (void **)&lo, &size);
    _main_stack_hi = lo+size;
}

static inline void * _gc_loop_wrapper(void * x)
{
    _gc_loop(x);
    puts("exiting...");
    fflush(stdout);
    fence();
    pthread_exit(0);
    puts("exiting?");
    fflush(stdout);
    fence();
    return 0;
}

static pthread_t _gc_thread = 0;
extern "C" int gc_start()
{
    gc_run_startup();
    
    std::atomic_uint8_t dummy;
    if (!std::atomic_is_lock_free(&dummy))
        assert(((void)"atomic byte accesses must be lockfree", 0));
    
    wasted_seconds = 0.0;
    assert(!pthread_create(&_gc_thread, 0, &_gc_loop_wrapper, 0));
    return 0;
}
extern "C" int gc_end()
{
    safepoint_mutex.unlock();
    
    _gc_stop = 1;
    pthread_join(_gc_thread, 0);
    fence();
    _gc_thread = 0;
    
    fence();
    _gc_stop = 0;
    fence();
    
    printf("seconds wasted with GC thread blocking main thread: %.4f\n", wasted_seconds);
    printf("pause\tcmd\twhiten\troots\tmark\tsweep\thipause\thxtable_bits\n");
    printf("%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%zu\n", secs_pause, secs_cmd, secs_whiten, secs_roots, secs_mark, secs_sweep, max_pause_time, GC_TABLE_BITS);
    return 0;
}
struct GcCanary { GcCanary() { gc_start(); } ~GcCanary() { gc_end(); } };
static GcCanary _gc_canary = GcCanary();

static size_t _gc_os_page_size = 0;
static size_t _gc_os_page_size_log2 = 0;
static char * _gc_heap_base = 0;
static std::atomic<char *> _gc_heap_cur = 0;
static std::atomic<char *> _gc_heap_top = 0;
static std::atomic<char *> _gc_heap_free[64] = {};
struct CharP { char * p; };
static inline size_t _gc_get_heap_start()
{
    return (size_t)_gc_heap_base;
}
static inline CharP _gc_os_init_heap()
{
    _gc_os_page_size = (size_t)sysconf(_SC_PAGESIZE);
    _gc_os_page_size_log2 = 64-__builtin_clzll(_gc_os_page_size-1);
    printf("%zd %zd\n", _gc_os_page_size, _gc_os_page_size_log2);
    // 16 TiB should be enough for anybody
    auto ret = (char *)mmap(0, 0x100000000000, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    //auto ret = (char *)0x100000000000;
    assert((size_t)ret != (size_t)-1);
    _gc_heap_base = ret;
    _gc_heap_cur.store(ret);
    _gc_heap_top.store(ret);
    for (size_t i = 0; i < 64; i++)
        assert(_gc_heap_free[i] == 0);
    return CharP{ret};
}
static CharP _gc_heap_base_s __attribute__((init_priority(101))) = _gc_os_init_heap();

#include <bit>

#if (__has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)) && !defined(GC_NO_PREFIX)
#include <sanitizer/asan_interface.h>
void _gc_asan_poison(void * a, size_t n) { ASAN_POISON_MEMORY_REGION(a, n); }
void _gc_asan_unpoison(void * a, size_t n) { ASAN_UNPOISON_MEMORY_REGION(a, n); }
#else
void _gc_asan_unpoison(void *, size_t) { }
void _gc_asan_poison(void *, size_t) { }
#endif

inline static void _gc_rawmal_inner_pages(char ** p, size_t * s)
{
    char * p2 = *p;
    char * end = *p + *s;
    p2 += _gc_os_page_size-1;
    p2 = (char *)(size_t(p2) >> _gc_os_page_size_log2);
    p2 = (char *)(size_t(p2) << _gc_os_page_size_log2);
    *p = 0;
    *s = 0;
    if (end < p2 + _gc_os_page_size)
        return;
    size_t byte_count = end-p2;
    size_t page_count = byte_count >> _gc_os_page_size_log2;
    *p = p2;
    *s = page_count << _gc_os_page_size_log2;
}

extern "C" void * _gc_raw_malloc(size_t n)
{
    #ifndef GC_CUSTOM_MALLOC
    return malloc(n);
    
    #else
    
    if (!n || n > 0x100000000000) return 0;
    //n = std::bit_ceil(n);
    
    if (n < 32) n = 32;
    n = 1ULL << (64-__builtin_clzll(n-1));
    int bin = __builtin_ctzll(n);
    
    // any data race on this read will merely result in a missed freedlist reusage
    if (_gc_heap_free[bin].load(std::memory_order_relaxed))
    {
        char * p;
        do {
            p = _gc_heap_free[bin].load(std::memory_order_acquire);
        } while (p && !_gc_heap_free[bin].compare_exchange_weak(p, (char*)GcAllocHeaderPtr(p)->next, std::memory_order_release, std::memory_order_relaxed));
        
        if (p)
        {
            _gc_asan_unpoison(p, GCOFFS);
            
            char * p2 = p+GCOFFS;
            size_t s = n;
            _gc_rawmal_inner_pages(&p2, &s);
            
#ifndef GC_LINUX_NO_PROT_NONE
            if (s)
            {
                //if ((void *)(size_t)-1 == mmap(p2, s, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0))
                if (mprotect(p2, s, PROT_READ | PROT_WRITE))
                {
                    printf("%d\n", errno);
                    assert(0);
                }
            }
#endif
            
            GcAllocHeaderPtr(p)->size = n;
            _gc_asan_poison(p, GCOFFS);
            return (void *)(p+GCOFFS);
        }
    }
    
    // n is basic allocation size (rounded up to a power of 2)
    auto n2 = n + GCOFFS;
    char * p = _gc_heap_cur.fetch_add(n2, std::memory_order_acq_rel);
    // any race on the calulation of "over" will only result in a slight excess of committed memory, no unsafety
    ptrdiff_t over = p + n2 - _gc_heap_top.load(std::memory_order_relaxed);
    if (over > 0)
    {
        over = (over + (_gc_os_page_size - 1))/_gc_os_page_size*_gc_os_page_size;
        auto loc = _gc_heap_top.fetch_add(over, std::memory_order_acq_rel);
        //if ((void *)(size_t)-1 == mmap(loc, over, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0))
        if (mprotect(loc, over, PROT_READ | PROT_WRITE))
        {
            printf("%d\n", errno);
            assert(0);
        }
    }
    
    // already zeroed by the OS
    GcAllocHeaderPtr(p)->size = n;
    
    _gc_asan_poison(p, GCOFFS);
    
    return (void *)(p+GCOFFS);
    #endif
}
inline static void * _gc_raw_calloc(size_t c, size_t n)
{
    #ifndef GC_CUSTOM_MALLOC
    return calloc(c, n);
    
    #else
    
    auto r = _gc_raw_malloc(c*n);
    return r;
    #endif
}
inline static void _gc_raw_free(void * _p)
{
    #ifndef GC_CUSTOM_MALLOC
    return free(_p);
    
    #else
    
    if (!_p) return;
    char * p = ((char *)_p)-GCOFFS;
    
    _gc_asan_unpoison(p, GCOFFS);
    
    GcAllocHeaderPtr(p)->size <<= 8;
    GcAllocHeaderPtr(p)->size >>= 8;
    
    size_t n = GcAllocHeaderPtr(p)->size;
    //n = std::bit_ceil(n);
    n = 1ULL << (64-__builtin_clzll(n-1));
    int bin = __builtin_ctzll(n);
    
    
    char * p2 = p+GCOFFS;
    size_t s = n;
    _gc_rawmal_inner_pages(&p2, &s);
#ifdef GC_LINUX_NO_MADV_FREE
    memset(p, 0, n + GCOFFS);
#ifndef GC_LINUX_NO_PROT_NONE
    if (s) mprotect(p2, s, PROT_NONE);
#endif
#else
    if (s)
    {
        memset(p, 0, p2-p);
        memset(p2+s, 0, p+n+GCOFFS-(p2+s));
        
#ifndef GC_LINUX_NO_PROT_NONE
        if (s) mprotect(p2, s, PROT_NONE);
#endif
        madvise(p2, s, MADV_FREE);
        //mprotect(p2, s, PROT_READ | PROT_WRITE);
        //munmap(p2, s);
    }
    else
        memset(p, 0, n + GCOFFS);
#endif
    
    auto gp = GcAllocHeaderPtr(p);
    do
    {
        gp->next = (size_t **)_gc_heap_free[bin].load(std::memory_order_acquire);
    } while (!_gc_heap_free[bin].compare_exchange_weak(*(char **)&gp->next, (char *)gp, std::memory_order_release, std::memory_order_relaxed));
    
    _gc_asan_poison(p, GCOFFS);
    #endif
}

#endif // else of WIN32

#endif // _GC_OS_HPP
