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
    fence();
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

static inline size_t _gc_get_heap_start()
{
    return (size_t)_walloc_heap_base;
}
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

#endif // else of WIN32

#endif // _GC_OS_HPP
