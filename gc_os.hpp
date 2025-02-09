#ifndef _GC_OS_HPP
#define _GC_OS_HPP

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <windows.h>
#include <psapi.h>
#include <processthreadsapi.h>

struct Context { CONTEXT ctx; };
static inline Context * _gc_get_threadlocal_ctx()
{
    static thread_local Context ctx = {};
    return &ctx;
}
static inline void _gc_thread_suspend(GcThreadRegInfo * info)
{
    auto & ctx = *info->context;
    ctx.ctx.ContextFlags = CONTEXT_INTEGER | CONTEXT_CONTROL | CONTEXT_FLOATING_POINT | CONTEXT_DEBUG_REGISTERS;
    
    DWORD rval = SuspendThread((HANDLE)info->alt_id);
    int nx = 1000;
    while (rval == (DWORD)-1 && nx-- > 0)
    {
        std::this_thread::yield();
        Sleep(0);
        rval = SuspendThread((HANDLE)info->alt_id);
    }
    assert(rval != (DWORD)-1);
    _gc_fence();
    assert(GetThreadContext((HANDLE)info->alt_id, &ctx.ctx));
}

static inline void _gc_thread_unsuspend(GcThreadRegInfo * info)
{
    DWORD rval = ResumeThread((HANDLE)info->alt_id);
    int nx = 1000;
    while (rval == (DWORD)-1 && nx-- > 0)
    {
        std::this_thread::yield();
        Sleep(0);
        rval = ResumeThread((HANDLE)info->alt_id);
    }
    assert(rval != (DWORD)-1);
}
static inline size_t _gc_context_get_rsp(Context * ctx)
{
    return (size_t)ctx->ctx.Rsp;
}
static inline size_t _gc_context_get_rip(Context * ctx)
{
    return (size_t)ctx->ctx.Rip;
}
static inline size_t _gc_context_get_size()
{
    return sizeof(CONTEXT);
}
static inline void _gc_safepoint_setup_os()
{
    _gc_awake_long_yield();
}
static inline void _gc_get_data_sections()
{
    //_gc_add_os_root_reset();
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
                    _gc_add_os_root_region((void **)memInfo.BaseAddress, memInfo.RegionSize / sizeof(size_t));
                address += memInfo.RegionSize;
            }
            else
                break;
        }
    }
}

static inline size_t _gc_get_stack_hi()
{
    ULONGLONG lo;
    ULONGLONG hi;
    GetCurrentThreadStackLimits(&lo, &hi);
    return (size_t)hi;
}
static inline size_t _gc_get_stack_lo()
{
    ULONGLONG lo;
    ULONGLONG hi;
    GetCurrentThreadStackLimits(&lo, &hi);
    return (size_t)lo;
}
static inline size_t _gc_thread_id_acquire()
{
    HANDLE h;
    auto cprc = GetCurrentProcess();
    auto cthd = GetCurrentThread();
    assert(DuplicateHandle(cprc, cthd, cprc, &h, 0, FALSE, DUPLICATE_SAME_ACCESS));
    return (size_t)h;
}
static inline void _gc_thread_id_release(size_t h)
{
    CloseHandle((HANDLE)h);
}
static inline void gc_run_startup()
{
    gc_add_current_thread();
}

static inline HANDLE _gc_long_yield_wake_event = 0;

static size_t _gc_thread = 0;
extern "C" int gc_start()
{
    _gc_long_yield_wake_event = CreateEvent(0, false, false, 0);
    
    gc_table = (size_t ***)_walloc_raw_calloc(GC_TABLE_SIZE, sizeof(size_t **));
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
    puts("waiting for GC thread to stop...");
    auto r = WaitForSingleObject((HANDLE)_gc_thread, INFINITE);
    printf("??? %ld\n", r);
    _gc_fence();
    CloseHandle((HANDLE)_gc_thread);
    _gc_thread = 0;
    
    _gc_fence();
    _gc_stop = 0;
    _gc_fence();
    
    CloseHandle(_gc_long_yield_wake_event);
    
    printf("seconds wasted with GC thread blocking main thread: %.4f\n", wasted_seconds);
    printf("pause\tcmd\twhiten\troots\tmark\tsweep\thipause\thxtable_bits\n");
    printf("%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%zu\n", secs_pause, secs_cmd, secs_whiten, secs_roots, secs_mark, secs_sweep, max_pause_time, GC_TABLE_BITS);
    return 0;
}
struct GcCanary { GcCanary() { gc_start(); } ~GcCanary() { gc_end(); } };
static GcCanary _gc_canary = GcCanary();

static inline std::atomic_int _gc_yield_counter = 0;
static inline void _gc_awake_long_yield()
{
    _gc_yield_counter = 0;
    SetEvent(_gc_long_yield_wake_event);
}
static inline void _gc_long_yield()
{
    HANDLE thread = GetCurrentThread();
    int priority = GetThreadPriority(thread);
    SetThreadPriority(thread, THREAD_PRIORITY_LOWEST);
    
    _gc_yield_counter++;
    std::this_thread::yield();
    Sleep(0);
    if (_gc_yield_counter > 1000)
    {
        _gc_yield_counter = 0;
        WaitForSingleObject(_gc_long_yield_wake_event, 1);
    }
    
    SetThreadPriority(thread, priority);
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
static thread_local Context ctx = {};

static inline Context * _gc_get_threadlocal_ctx()
{
    return &ctx;
}
static inline void _gc_thread_suspend(GcThreadRegInfo * info)
{
    // not done on linux
    (void)info;
}
static inline void _gc_thread_unsuspend(GcThreadRegInfo * info)
{
    // handled in _gc_safepoint_setup_os
    (void)info;
}
static inline size_t _gc_context_get_rsp(Context * ctx)
{
    return (size_t)ctx->ctx.uc_mcontext.gregs[REG_RSP] - (128 / sizeof(size_t));
}
static inline size_t _gc_context_get_rip(Context * ctx)
{
    return (size_t)ctx->ctx.uc_mcontext.gregs[REG_RIP];
}
static inline size_t _gc_context_get_size()
{
    return sizeof(Context);
}
static inline void _gc_unsuspend_main()
{
    // handled by lock
}
static inline void _gc_thread_id_release(size_t id)
{
    (void)id;
    // not needed on linux
}

static inline void _gc_safepoint_setup_os()
{
    assert(!getcontext(&ctx.ctx));
    //puts("got context!");
}

static inline size_t _gc_get_heap_start()
{
    return (size_t)_walloc_heap_base;
}
static inline void _gc_get_data_sections()
{
    //_gc_add_os_root_reset();
    static int found = 0;
    if (found) return;
    found = 1;
    
    FILE * maps = fopen("/proc/self/maps", "r");
    assert(maps);
    char line[256] = {};
    size_t heap_start = _gc_get_heap_start();
    //size_t prev_end = 0;
    //bool prev_noperm = false;
    while (fgets(line, sizeof(line), maps))
    {
        size_t i = 0;
        while (line[i] != '\t' && line[i] != ' ' && i < 255) i++;
        if (i > 200) return;
        //prev_noperm = line[i+1] == '-' && line[i+2] == '-' && line[i+3] == '-';
        
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
        
        //prev_end = end;
        
        /*
        _gc_add_os_root_region((void **)start, (end-start) / sizeof(size_t));
        printf("%s  ", line);
        printf("%zd\n", (end-start) / sizeof(size_t));
        
        if ((end-start) / sizeof(size_t) > 100000)
        {
            for (size_t j = 0; j < 8192; j++)
            {
                for (size_t i = 0; i < 16; i++)
                    printf("%02X ", ((uint8_t *)start)[i+j]);
                puts("");
            }
            puts("");
        }
        */
    }
    printf("NOTE: our heap starts at %zX\n", heap_start);
}

static inline size_t _gc_get_stack_hi()
{
    size_t lo;
    size_t size;
    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, (void **)&lo, &size);
    pthread_attr_destroy(&attr);
    return lo + size;
}
static inline size_t _gc_get_stack_lo()
{
    size_t lo;
    size_t size;
    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, (void **)&lo, &size);
    pthread_attr_destroy(&attr);
    return lo;
}
static inline size_t _gc_thread_id_acquire()
{
    return gettid();
}
static inline void gc_run_startup()
{
    _gc_get_data_sections();
    gc_add_current_thread();
}

static inline void * _gc_loop_wrapper(void * x)
{
    puts("entering!");
    _gc_loop(x);
    puts("exiting...");
    fflush(stdout);
    _gc_fence();
    pthread_exit(0);
    puts("exiting?");
    fflush(stdout);
    _gc_fence();
    return 0;
}

static pthread_t _gc_thread = 0;
extern "C" int gc_start()
{
    gc_table = (size_t ***)_walloc_raw_calloc(GC_TABLE_SIZE, sizeof(size_t **));
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
    puts("going to try to exit gc");
    _gc_stop = 1;
    //safepoint_mutex.unlock();
    puts("mutex unlocked...?");
    
    puts("trying to join");
    pthread_join(_gc_thread, 0);
    _gc_fence();
    _gc_thread = 0;
    puts("got out");
    
    _gc_fence();
    _gc_stop = 0;
    _gc_fence();
    
    printf("seconds wasted with GC thread blocking main thread: %.4f\n", wasted_seconds);
    printf("pause\tcmd\twhiten\troots\tmark\tsweep\thipause\thxtable_bits\n");
    printf("%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%zu\n", secs_pause, secs_cmd, secs_whiten, secs_roots, secs_mark, secs_sweep, max_pause_time, GC_TABLE_BITS);
    return 0;
}
struct GcCanary { GcCanary() { gc_start(); } ~GcCanary() { gc_end(); } };
static GcCanary _gc_canary = GcCanary();

static inline void _gc_long_yield() { sleep(0); }

#endif // else of WIN32

#endif // _GC_OS_HPP
