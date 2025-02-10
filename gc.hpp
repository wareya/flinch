#include <mutex>
#include <atomic>
#include <thread>
#include <assert.h>

#ifndef NOINLINE
#define NOINLINE __attribute__((noinline))
#endif

static inline double _gc_run_growth_threshold = 1.5;

#ifdef COLLECT_STATS
#include <chrono>
static inline double get_time()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    double ret = std::chrono::duration<double>(duration).count();
    return ret;
}
#else
static inline double get_time() { return 0.0; };
#endif

#define GC_MALLOC_ATT __attribute__((__malloc__))

extern "C" GC_MALLOC_ATT void * gc_malloc(size_t n);
extern "C" GC_MALLOC_ATT void * gc_calloc(size_t c, size_t n);
extern "C" void gc_free(void *);
extern "C" GC_MALLOC_ATT void * gc_realloc(void * _p, size_t n);
extern "C" int gc_start();
extern "C" int gc_end();

// If provided by gc_set_trace_func, called during tracing. Return the address containing a GC-owned pointer.
// Dereferencing the returned pointer must result in a memory cell containing a GC-owned pointer.
// For the first call, "current" is 0. Each next call has "current" be the last-returned value.
// i is a counter starting at 0 and increasing by 1 for each consecutive call to the given trace function.
// When zero is returned, tracing on the given allocation stops, to resume on the next GC cycle.
// The alloc and userdata are those that were provided by a call of gc_set_trace_func.
// Each alloc can have at most one trace function assigned at once.
// Failing to return the address of any GC-owned pointer will result in spurious frees, and eventually UAFs.
// Returning addresses that do not point at GC-owned pointers will result in undefined behavior.
// The function must not have any side effects on GC-accessible data, must not lock any mutexes, must not allocate memory, etc.
typedef void **(*GcTraceFunc)(void * alloc, void ** current, size_t i, size_t userdata);

// Attaches a trace function to an allocation. Without one, every machine-word-sized chunk in the allocation is
// checked for being a pointer.
static inline void gc_set_trace_func(void * alloc, GcTraceFunc fn, size_t userdata);

// Do not trace inside of the given allocation.
static inline void gc_set_trace_none(void * alloc);

// Takes a pointer (void **) that points at a memory region (void *) that may or may not contain non-stack/register roots.
// Only 256 custom roots can be added. Custom roots cannot be removed. Every pointed-to word must be accessible.
// Size is in words, not bytes
static inline void gc_add_custom_root_region(void ** alloc, size_t size);

// Must be called once on startup from any thread that may uniquely hold GC'd pointers.
// Not needed for the main thread, is called automatically.
static inline void gc_add_current_thread();

// Inside of synchronization spinloops you must call this at least once per loop iteration with an argument of 0.
// (Or wrap the loop with the below "long" calls if the loop doesn't get/release/move GC'd memory.)
static inline void gc_safepoint(size_t inc);

// Must be called before and after doing anything that might take arbitrarily long, e.g. joining a thread, locking a mutex, etc.
// If you have a deadlock during some operation, you probably need to call these around it.
// While in a safepoint you cannot create or move any GC'd pointers on, into, or out of the safepointed thread.
static inline void _gc_safepoint_long_start();
static inline void _gc_safepoint_long_end();

static inline void _gc_fence() { std::atomic_thread_fence(std::memory_order_seq_cst); }

static inline void _gc_awake_long_yield();
static inline void _gc_long_yield();

#define WALLOC_CUSTOMHEADER
struct WAllocHeader
{
    size_t size;
    WAllocHeader * next;
    GcTraceFunc tracefn;
    size_t tracefndat;
};
typedef WAllocHeader GcAllocHeader;
typedef WAllocHeader * GcAllocHeaderPtr;
//#define WALLOC_PULL_OVERRIDE 256
#define WALLOC_FLUSH_OVERRIDE 10000000000
//#define WALLOC_FLUSH_KEEP_OVERRIDE 0
//#define WALLOC_CACHEHINT 64
#define WALLOC_FAST_SAFE
//#define WALLOC_NOZERO
//#define WALLOC_MAXIMUM_FAST
#include "wmalloc.hpp"

#if (!defined(_WIN32)) && defined(GC_USE_LAZY_SPINLOCK)
    // faster than std::mutex (linux only)
    #include <unistd.h>
    struct LazySpinLock
    {
        std::atomic_flag locked = ATOMIC_FLAG_INIT;
        void lock() { while (locked.test_and_set(std::memory_order_acq_rel)) { std::this_thread::yield(); } }
        void unlock() { locked.clear(std::memory_order_release); }
    };
    typedef LazySpinLock Mutex;
#elif 0//(defined(_WIN32)) && defined(GC_USE_LAZY_SPINLOCK)
    // slower than std::mutex
    #include <thread>
    struct LazySpinLock
    {
        std::atomic_flag locked = ATOMIC_FLAG_INIT;
        void lock() { while (locked.test_and_set(std::memory_order_acq_rel)) { std::this_thread::yield(); } }
        void unlock() { locked.clear(std::memory_order_release); }
    };
    typedef LazySpinLock Mutex;
#elif (!defined(_WIN32)) && defined(GC_USE_WAIT_LOCK)
    // very very slightly faster than std::mutex, but only on linux. on windows, catastrophically slower (3x program runtime!!!!)
    struct WaitLock
    {
        std::atomic_int locked = 0;
        void lock()
        {
            int expected = 0;
            if (!locked.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
            {
                do {
                    expected = 0;
                    locked.wait(1, std::memory_order_acquire);
                } while (!locked.compare_exchange_weak(expected, 1, std::memory_order_acq_rel));
            }
        }
        void unlock()
        {
            locked.store(0, std::memory_order_release);
            locked.notify_one();
        }
    };
    typedef WaitLock Mutex;
#else
    typedef std::mutex Mutex;
#endif

static inline int _gc_m_d_m_d() { _gc_fence(); return 0; }
static int _mutex_dummy = _gc_m_d_m_d();

static size_t _main_thread = 0;
static std::thread::id main_thread_id = std::this_thread::get_id();
static inline void enforce_not_main_thread()
{
    // only for debugging
    return;
    int x = main_thread_id != std::this_thread::get_id();
    if (!x)
    {
        printf("ERROR!!!!!!!!");
        fflush(stdout);
    }
    assert(x);
}
static inline void enforce_yes_main_thread()
{
    // only for debugging
    return;
    if (_main_thread != 0) return;
    
    int x = main_thread_id == std::this_thread::get_id();
    if (!x)
    {
        printf("error.............");
        fflush(stdout);
    }
    assert(x);
}

enum { GC_WHITE, GC_GREY, GC_BLACK, GC_RED };
#define GCOFFS_W ((sizeof(GcAllocHeader)+7)/8)
#define GCOFFS (sizeof(size_t *)*GCOFFS_W)

static inline void _gc_set_color(char * p, uint8_t color)
{
    std::atomic_ref(((char *)&(GcAllocHeaderPtr(p-GCOFFS)->size))[7]).store(color);
}
static inline uint8_t _gc_get_color(char * p)
{
    auto ret = std::atomic_ref(((char *)&(GcAllocHeaderPtr(p-GCOFFS)->size))[7]).load();
    return ret;
}

static inline void _gc_set_size(char * p, size_t size)
{
    std::atomic_ref(GcAllocHeaderPtr(p-GCOFFS)->size).store(size);
}
static inline size_t _gc_get_size(char * p)
{
    size_t ret = std::atomic_ref(GcAllocHeaderPtr(p-GCOFFS)->size).load();
    ret <<= 8;
    ret >>= 8;
    return ret;
}

extern "C" void * _walloc_raw_malloc(size_t n);
extern "C" void * _walloc_raw_calloc(size_t c, size_t n);
extern "C" void _walloc_raw_free(void * _p);

struct GcListNode {
    char * val = 0;
    GcListNode * next = 0;
};
static GcListNode * gc_gc_freelist = 0;
static inline void _gc_list_push(GcListNode ** table, char * val, GcListNode ** freelist)
{
    enforce_not_main_thread();
    GcListNode * newly;
    if (*freelist)
    {
        newly = *freelist;
        *freelist = (*freelist)->next;
    }
    else
        //newly = (GcListNode *)malloc(sizeof(GcListNode));
        newly = (GcListNode *)_walloc_raw_malloc(sizeof(GcListNode));
    newly->val = val;
    newly->next = *table;
    *table = newly;
}
static inline char * _gc_list_pop(GcListNode ** table, GcListNode ** freelist)
{
    enforce_not_main_thread();
    auto node = *table;
    *table = node->next;
    
    node->next = *freelist;
    *freelist = node;
    return node->val;
}

#ifdef GC_SYSTEM_MALLOC_HEADER
#include GC_SYSTEM_MALLOC_HEADER
#endif

#ifndef GC_SYSTEM_MALLOC_PREFIX
#define GC_SYSTEM_MALLOC_PREFIX(X) X
#endif

#define CONCAT(x, y) x ## y

static inline void * _malloc(size_t n)
{
    enforce_yes_main_thread();
    #ifdef GC_SYSTEM_MALLOC
    auto ret = GC_SYSTEM_MALLOC_PREFIX(calloc)(1, n);
    #else
    auto ret = _walloc_raw_malloc(n);
    #endif
    //auto h = GcAllocHeaderPtr(((char *)ret)-GCOFFS);
    //h->tracefn = 0;
    //h->tracefndat = 0;
    
    //memset(ret, 0, n);
    //#endif
    return ret;
    //return calloc(1, n);
}
static inline void _free(void * p)
{
    enforce_not_main_thread();
    
    #ifdef GC_SYSTEM_MALLOC
    GC_SYSTEM_MALLOC_PREFIX(free)(p);
    #else
    _walloc_raw_free(p);
    #endif
}

#define Ptr(X) X *
#define PtrCast(X, Y) (X *)(Y)
#define PtrBase(X) (X)
#define ISTEMPLATE 0


//#define GC_TABLE_HASH(X) (((((X)+1)*123454321)>>6)&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) (((((X)+1)*123454321)>>5)&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) (((((X)+1)*123454321)>>8)&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) ((((X)>>6)^(X))&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) ((((X)>>6)^(X))&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) (((X)>>6)&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) ((((X)>>8)^(X))&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) (((X)>>4)&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) (((X)>>5)&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) (((X)>>6)&(GC_TABLE_SIZE - 1))
#define GC_TABLE_HASH(X) (((X)>>7)&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) (((X)>>8)&(GC_TABLE_SIZE - 1))

static size_t GC_TABLE_BITS = 16ULL;
static size_t GC_TABLE_SIZE = (1ULL<<GC_TABLE_BITS);
static size_t *** gc_table = 0;
static size_t gc_table_count = 0;
static size_t gc_table_bytes = 0;

static Mutex _gc_table_mutex;

static inline int _gc_table_push(char * p)
{
    enforce_not_main_thread();
    size_t k = GC_TABLE_HASH((size_t)p);
    
    int ret = !!gc_table[k];
    GcAllocHeaderPtr(p-GCOFFS)->next = (GcAllocHeaderPtr)gc_table[k];
    gc_table[k] = ((size_t **)p);
    gc_table_count += 1;
    gc_table_bytes += _gc_get_size(p);
    return ret;
}
static inline size_t ** _gc_table_get(char * p)
{
    enforce_not_main_thread();
    size_t k = GC_TABLE_HASH((size_t)p);
    size_t ** next = gc_table[k];
    while (next && next != (size_t **)p)
        next = (size_t **)GcAllocHeaderPtr(next-GCOFFS_W)->next;
    return next;
}

static double max_pause_time = 0.0;
static double wasted_seconds = 0.0;
static double secs_pause = 0.0;
static double secs_cmd = 0.0;
static double secs_whiten = 0.0;
static double secs_roots = 0.0;
static double secs_mark = 0.0;
static double secs_sweep = 0.0;

#ifndef GC_MSG_QUEUE_SIZE
// number of allocations (size-adjusted) before GC happens
// larger number = more hash table collisions, better use of context switches, greater passive memory usage
// lower number = more context switches and worse cache usage, shorter individual pauses
// default: 4000
#define GC_MSG_QUEUE_SIZE 4000
#endif

struct _GcCmdlist
{
    char * list[GC_MSG_QUEUE_SIZE] = {};
    size_t len = 0;
    _GcCmdlist * dummy = 0;
};

static std::atomic_int _gc_stop = 0;
static std::atomic_uint32_t _gc_threads_must_wait_for_gc = 0;
static thread_local _GcCmdlist gc_cmd; // write

struct GcThreadRegInfo;
static Mutex _thread_info_mutex;
static GcThreadRegInfo * _thread_info_list = 0;
static thread_local GcThreadRegInfo * _thread_info = 0;

static std::atomic_uint32_t _thread_count = 0;

static inline void _gc_apply_cmds(_GcCmdlist * gc_cmd_arg)
{
    _gc_table_mutex.lock();
    for (ptrdiff_t i = gc_cmd_arg->len; i > 0; i--)
    {
        char * c = gc_cmd_arg->list[i-1];
        _gc_table_push(c);
    }
    gc_cmd_arg->len = 0;
    _gc_table_mutex.unlock();
}

// called by main thread
static void _gc_safepoint_setup_os();
static inline void _gc_safepoint_impl_lock();
static NOINLINE void _gc_safepoint_impl()
{
    /*
    #ifdef COLLECT_STATS
    double start = get_time();
    #endif
    */
    //printf("before safepoint (threads: %d)\n", _thread_count.load());
    // gc should lock and unlock here
        
    _gc_fence();
    _gc_safepoint_setup_os();
    _gc_safepoint_impl_lock();
    _gc_fence();
    
    //printf("after safepoint (threads: %d)\n", _thread_count.load());
    /*
    #ifdef COLLECT_STATS
    double pause_time = get_time() - start;
    secs_pause += pause_time;
    if (pause_time > max_pause_time) max_pause_time = pause_time;
    #endif
    */
}

static inline void gc_safepoint(size_t inc)
{
    static thread_local size_t n = 0;
    n = n+inc;
    if (!_gc_threads_must_wait_for_gc.load())
    {
        if (n >= GC_MSG_QUEUE_SIZE)
        {
            _gc_apply_cmds(&gc_cmd);
            n = 0;
        }
    }
    else
    {
        _gc_apply_cmds(&gc_cmd);
        n = 0;
        
        //retry:
        //puts("into safepoint:");
        _gc_safepoint_impl();
    }
}


static bool _gc_debug_spew = 0;

//#define GC_NO_PREFIX

extern "C" NOINLINE GC_MALLOC_ATT void * gc_malloc(size_t n)
{
    if (_gc_threads_must_wait_for_gc.load()) gc_safepoint(0);
    if (!n) return 0;
    #ifndef GC_NO_PREFIX
    //n = (n+(GCOFFS-1))/GCOFFS*GCOFFS;
    n += GCOFFS;
    #endif
    
    auto i = (n + 0xFF) / 0x100; // size adjustment so very large allocations act like multiple w/r/t GC timing
    gc_safepoint(i);
    
    char * p = (char *)_malloc(n);
    if (!p) return 0;
    
    #ifndef GC_NO_PREFIX
    p += GCOFFS;
    _gc_set_size(p, n-GCOFFS);
    #endif
    
    //_gc_set_color((char *)p, GC_WHITE);
    
    assert(gc_cmd.len < GC_MSG_QUEUE_SIZE);
    gc_cmd.list[gc_cmd.len++] = p;
    
    return (void *)(p);
}

extern "C" GC_MALLOC_ATT void * gc_calloc(size_t c, size_t n)
{
    return gc_malloc(c*n);
}

extern "C" void gc_free(void * p)
{
    if (!p) return;
    _gc_set_color((char *)p, GC_RED);
}

extern "C" GC_MALLOC_ATT void * gc_realloc(void * _p, size_t n)
{
    char * p = (char *)_p;
    #if ISTEMPLATE
    if (!p) return PtrCast(char, gc_malloc(n));
    #else
    if (!p) return PtrCast(char, gc_malloc(n));
    #endif
    if (n == 0) { gc_free(p); return PtrCast(char, 0); }
    
    auto x = PtrCast(char, gc_malloc(n));
    assert(x);
    size_t size2 = _gc_get_size(p);
    memcpy(x, p, n < size2 ? n : size2);
    gc_free(p);
    
    return x;
}

static void ** os_root[256];
static size_t os_root_size[256]; // in words, not bytes
static size_t os_roots_i = 0;
static void ** custom_root[256];
static size_t custom_root_size[256]; // in words, not bytes
static size_t custom_roots_i = 0;
static inline void _gc_add_os_root_reset()
{
    os_roots_i = 0;
}
static inline void _gc_add_os_root_region(void ** alloc, size_t size); // size in words, not bytes
static inline void gc_add_custom_root_region(void ** alloc, size_t size) // size in words, not bytes
{
    custom_root_size[custom_roots_i] = size;
    custom_root[custom_roots_i++] = alloc;
    assert(custom_roots_i <= 256);
}
static inline void gc_set_trace_func(void * alloc, GcTraceFunc fn, size_t userdata)
{
    size_t * p = ((size_t *)alloc)-GCOFFS_W;
    
    GcAllocHeaderPtr(p)->tracefn = fn;
    GcAllocHeaderPtr(p)->tracefndat = userdata;
}
static inline void gc_set_trace_none(void * alloc)
{
    size_t * p = ((size_t *)alloc)-GCOFFS_W;
    GcAllocHeaderPtr(p)->tracefn = (GcTraceFunc)(void *)(size_t)-1;
}

struct Context;

struct GcThreadRegInfo
{
    size_t stack_hi = 0;
    size_t stack_lo = 0;
    std::thread::id id;
    size_t alt_id = 0;
    GcThreadRegInfo * prev = 0;
    GcThreadRegInfo * next = 0;
    Context * context = 0;
    _GcCmdlist * gc_cmd = 0;
    
    Mutex mtx;
    std::atomic_int baton = 0;
    std::atomic_int dead = 0;
    
    /*
    bool is_locked()
    {
        auto ret = mtx.try_lock();
        if (ret) mtx.unlock();
        return ret;
    }
    */
    void lock_from_gc()
    {
        if (_gc_debug_spew) puts("m.a");
        //if (_gc_debug_spew) printf("gc: trying to lock %zd\n", alt_id);
        //printf("gc: trying to lock %zd\n", alt_id);
        mtx.lock();
        if (baton == 0)
            baton.store(1, std::memory_order_release);
        if (_gc_debug_spew) puts("m.b");
    }
    void unlock_from_gc()
    {
        if (_gc_debug_spew) printf("gc: trying to unlock %zd\n", alt_id);
        mtx.unlock();
        
        while (baton.load(std::memory_order_acquire) == 1)
            std::this_thread::yield();
        
        if (_gc_debug_spew) printf("gc: unlocked %zd\n", alt_id);
    }
};

static inline NOINLINE void _gc_safepoint_long_start()
{
    _gc_fence();
    _gc_safepoint_setup_os();
    _thread_info->baton.fetch_add(5, std::memory_order_acq_rel);
    _thread_info->mtx.unlock();
}
static inline NOINLINE void _gc_safepoint_long_end()
{
    _thread_info->mtx.lock();
    _thread_info->baton.store(0);
    _gc_fence();
}
static inline void _gc_safepoint_impl_lock()
{
    if (!_thread_info || !_gc_threads_must_wait_for_gc.load())
        return;
    
    auto id = _thread_info->alt_id;
    if (_gc_debug_spew) printf("thread %zd unlocking\n", id);
    _thread_info->baton.store(0, std::memory_order_release);
    
    _thread_info->mtx.unlock();
    // vvvvv printing or allocating in here is FORBIDDEN vvvvv
    
    while (!_thread_info->baton.load(std::memory_order_acquire))
        std::this_thread::yield();
    
    // ^^^^^ printing or allocating in here is FORBIDDEN ^^^^^
    _thread_info->mtx.lock();
    if (_gc_debug_spew) printf("thread %zd locked! returning baton\n", id);
    _thread_info->baton.store(0, std::memory_order_release);
}
    
#include <mutex>

static inline Context * _gc_get_threadlocal_ctx();
static inline size_t _gc_get_stack_hi();
static inline size_t _gc_get_stack_lo();
static inline size_t _gc_thread_id_acquire();
static inline void _gc_thread_id_release(size_t h);

static inline void gc_add_current_thread()
{
    struct GcThreadRegistrationCanary
    {
        bool initialized = false;
        
        void init()
        {
            if (initialized) return;
            initialized = true;
            
            auto info = new(_malloc(sizeof(GcThreadRegInfo)))GcThreadRegInfo();
            info->stack_hi = _gc_get_stack_hi();
            info->stack_lo = _gc_get_stack_lo();
            info->id = std::this_thread::get_id();
            info->alt_id = _gc_thread_id_acquire();
            //printf("stack top for %zd is %zX\n", info->alt_id, info->stack_hi);
            //printf("thread id: %zu (main: %zu)\n", info->alt_id, _main_thread);
            info->gc_cmd = &gc_cmd;
            
            _thread_info = info;
            _thread_info->context = _gc_get_threadlocal_ctx();
            _thread_info->baton.store(0);
            _thread_info->dead.store(0);
            _thread_info->mtx.lock();
            
            _thread_info_mutex.lock();
            if (_gc_debug_spew) printf("adding %zd\n", info->alt_id);
            _gc_fence();
            if (_thread_info_list != 0)
                _thread_info->next = _thread_info_list;
            if (_thread_info->next)
                _thread_info->next->prev = _thread_info;
            _thread_info_list = _thread_info;
            _thread_count.fetch_add(1);
            
            _gc_fence();
            _thread_info_mutex.unlock();
        }
        
        ~GcThreadRegistrationCanary()
        {
            if (!initialized) return;
            
            //printf("trying to destruct thread %zd....\n", _thread_info->alt_id);
            //printf("destructing: %zd\n", gc_cmd.len);
            
            if (gc_cmd.len > 0)
            {
                _thread_info->gc_cmd = (_GcCmdlist *)_malloc(sizeof(_GcCmdlist));
                *_thread_info->gc_cmd = gc_cmd;
                gc_cmd = {};
            }
            
            if (_gc_debug_spew) puts("x");
            if (_gc_debug_spew) fflush(stdout);
            _thread_info->baton.store(5);
            
            if (_gc_debug_spew) puts("y");
            if (_gc_debug_spew) fflush(stdout);
            //gc_safepoint(0);
            if (_gc_debug_spew) puts("z");
            if (_gc_debug_spew) fflush(stdout);
            
            _thread_info->dead.store(1);
            
            _gc_fence();
            _thread_info->mtx.unlock();
            
            //puts("unlocked our mutex");
            
            //printf("!!!! thread %zd destructed !!!!\n", _thread_info->alt_id);
            /*
            _gc_fence();
            if (_thread_info->next)
                _thread_info->next->prev = _thread_info->prev;
            if (_thread_info->prev)
                _thread_info->prev->next = _thread_info->next;
            else 
                _thread_info_list = _thread_info->next;
            _thread_count.fetch_sub(1);
            _gc_fence();
            
            _thread_info_mutex.unlock();
            
            if (_gc_debug_spew) fflush(stdout);
            
            _thread_info->~GcThreadRegInfo();
            _free(_thread_info);
            */
            
            
            initialized = false;
        }
    };
    
    static thread_local GcThreadRegistrationCanary canary;
    
    canary.init();
}

static size_t _gc_scan_word_count = 0;
#define _GC_SCAN(start, end, rootlist)\
{\
    while (start != end)\
    {\
        size_t sw = (size_t)*start;\
        if (sw < GCOFFS || (sw & 0x7)) { start += 1; continue; } \
        char * v = (char *)_gc_table_get((char *)sw);\
        _gc_scan_word_count += 1;\
        if (v && _gc_get_color(v) < GC_BLACK)\
        {\
            _gc_set_color(v, GC_GREY);\
            _gc_list_push(rootlist, v, &gc_gc_freelist);\
        }\
        start += 1;\
    }\
}

static inline void _gc_scan(size_t * start, size_t * end, GcListNode ** rootlist)
{
    _GC_SCAN(start, end, rootlist)
}

static inline __attribute__((no_sanitize_address))
void _gc_scan_unsanitary(size_t * stack, size_t * stack_top, GcListNode ** rootlist)
{
    _GC_SCAN(stack, stack_top, rootlist)
}

static inline size_t _gc_context_size;
static inline size_t _gc_context_get_rsp(Context *);
static inline size_t _gc_context_get_rip(Context *);
static inline size_t _gc_context_get_size();
static inline void _gc_get_data_sections();
static inline void _gc_thread_suspend(GcThreadRegInfo * info);
static inline void _gc_thread_unsuspend(GcThreadRegInfo * info);

static size_t run_count = 0;
        
static inline void sweeper(
    std::atomic_size_t * filled_num, std::atomic_size_t * size, std::atomic_size_t * n3,
    size_t start, size_t end)
{
    if (_thread_info)
        printf("sweeper in thread %zd\n", _thread_info->alt_id);
    size_t n3_2 = 0;
    size_t size_2 = 0;
    for (size_t k = start; k < end; k++)
    {
        if (_gc_stop.load()) return;
        size_t ** next = gc_table[k];
        size_t ** prev = 0;
        while (next)
        {
            char * c = (char *)next;
            auto color = _gc_get_color(c);
            if (color != GC_WHITE && color != GC_RED)
                prev = next;
            next = (size_t **)GcAllocHeaderPtr(c-GCOFFS)->next;
            if (color == GC_WHITE || color == GC_RED)
            {
                if (prev) GcAllocHeaderPtr(prev-GCOFFS_W)->next = (GcAllocHeaderPtr)next;
                else gc_table[k] = next;
                
                size_2 += _gc_get_size(c);
                
                //auto s = _gc_get_size(c);
                
                #ifndef GC_NO_PREFIX
                //memset((void *)c-GCOFFS, 0xA5, s+GCOFFS);
                _free((void *)(c-GCOFFS));
                #else
                //memset((void *)c, 0xA5, s);
                _free((void *)c);
                #endif
                n3_2 += 1;
            }
            else // set color for next cycle
                _gc_set_color(c, GC_WHITE);
        }
        if (gc_table[k])
            filled_num->fetch_add(1);
    }
    n3->fetch_add(n3_2);
    size->fetch_add(size_2);
    //_walloc_flush_freelists();
}

static inline void _gc_add_os_root_region(void ** alloc, size_t size) // size in words, not bytes
{
    auto top = _thread_info_list;
    // skip the stacks of registered threads
    while (top)
    {
        //printf("stack lo is: %p\n", top->stack_lo);
        if (alloc == (void **)top->stack_lo)
            break;
        top = top->next;
    }
    if (top) return;
    
    os_root_size[os_roots_i] = size;
    os_root[os_roots_i++] = alloc;
    assert(os_roots_i <= 256);
}

static inline unsigned long int _gc_loop(void *)
{
    bool silent = 1;
    //size_t consec_noruns = 0;
    //size_t asdcadf = 0;
    
    size_t threads_made = 0;
    
    while (1)
    {
        //asdcadf++;
        //if (_gc_debug_spew) printf("!x %zd\n", asdcadf);
        
        run_count += 1;
        if (_gc_stop.load()) break;
        
        //if (_gc_debug_spew) printf("!y %zd\n", consec_noruns);
        
        static size_t prev_size = 0; // after sweep
        static size_t test_size = 0;
        if (test_size < prev_size * _gc_run_growth_threshold)
        {
            _gc_table_mutex.lock();
            test_size = gc_table_bytes;
            _gc_table_mutex.unlock();
            
            if (test_size < prev_size * _gc_run_growth_threshold)
            {
                _gc_long_yield();
                continue;
            }
        }
        //consec_noruns = 0;
        
        if (!silent) puts("-- starting GC cycle");
        if (!silent) fflush(stdout);
        
        if (_gc_debug_spew) puts("!z");
        
        
        _gc_threads_must_wait_for_gc.store(1);
        
        if (_gc_debug_spew) puts("!a");
        
        //_gc_table_mutex.lock();
        
        
        if (_gc_debug_spew) puts("!b");
        if (_gc_debug_spew) fflush(stdout);
        _thread_info_mutex.lock();
        auto top = _thread_info_list;
        //puts("starting suspension...");
        while (top)
        {
            //printf("trying to suspend %zd\n", top->alt_id);
            
            top->lock_from_gc(); // acquires context data on linux
            
            if (!top->dead.load())
            {
                _gc_thread_suspend(top); // acquires context data on windows
                _gc_thread_unsuspend(top);
                
                //puts("succeeded");
                if (_gc_debug_spew) fflush(stdout);
                top = top->next;
            }
            else
            {
                //printf("trying to suspend %zd.....aaaand it's dead.\n", top->alt_id);
                if (top->next)
                    top->next->prev = top->prev;
                if (top->prev)
                    top->prev->next = top->next;
                else 
                    _thread_info_list = top->next;
                _thread_count.fetch_sub(1);
                
                if (top->gc_cmd)
                    _gc_apply_cmds(top->gc_cmd);
                
                _gc_thread_id_release(top->alt_id);
                
                auto f = top;
                top = top->next;
                
                f->unlock_from_gc();
                f->~GcThreadRegInfo();
                _free(f);
                //puts("buried!");
            }
        }
        
        // lock hashtable in advance so rogue threads that started up after we locked _thread_info_mutex aren't trouble
        _gc_table_mutex.lock();
        
        _gc_fence();
        
        //puts("ending suspension");
        if (_gc_debug_spew) puts("!d");
        if (_gc_debug_spew) fflush(stdout);
        //puts("a3");
        
        //_gc_table_mutex.unlock();
        
        
        double start_start_time = get_time();
        
        double start_time;
        
        /////
        ///// receive hashtable update commands from main thread phase
        /////
        
        /*
        start_time = get_time();
        
        top = _thread_info_list;
        while (top)
        {
            _gc_apply_cmds(top->gc_cmd);
            top = top->next;
        }
        
        secs_cmd += get_time() - start_time;
        
        if (!silent) puts("-- cmdlist updated");
        */
        
        /*
        _gc_table_mutex.lock();
        for (size_t k = 0; k < GC_TABLE_SIZE; k++)
        {
            size_t ** next = gc_table[k];
            size_t ** prev = 0;
            while (next)
            {
                _gc_set_color((char *)next, GC_WHITE);
                next = GcAllocHeaderPtr(next-GCOFFS_W)->next;
            }
        }
        _gc_table_mutex.unlock();
        */
        
        /////
        ///// root collection phase
        /////
        
        start_time = get_time();
        
        GcListNode * rootlist = 0;
        _gc_scan_word_count = 0;
        
        if (_gc_debug_spew) puts("!f");
        if (_gc_debug_spew) fflush(stdout);
        
        _gc_get_data_sections();
        
        if (_gc_debug_spew) puts("!g");
        if (_gc_debug_spew) fflush(stdout);
        
        for (size_t i = 0; i < os_roots_i; i++)
        {
            size_t * c = (size_t *)(os_root[i]);
            size_t c_size = os_root_size[i];
            _gc_scan_unsanitary(c, c+c_size, &rootlist);
        }
        
        for (size_t i = 0; i < custom_roots_i; i++)
        {
            size_t * c = (size_t *)(custom_root[i]);
            size_t c_size = custom_root_size[i];
            _gc_scan_unsanitary(c, c+c_size, &rootlist);
        }
        
        if (_gc_debug_spew) puts("!h");
        if (_gc_debug_spew) fflush(stdout);
        
        top = _thread_info_list;
        while (top)
        {
            if (top->dead.load()) continue;
            
            size_t * c = (size_t *)top->context;
            size_t c_size = _gc_context_get_size()/sizeof(size_t);
            _gc_scan(c, c+c_size, &rootlist);
            
            size_t * stack = (size_t *)(_gc_context_get_rsp((Context *)top->context) / 8 * 8);
            
            //stack -= 100;
            
            //printf("INFO: thread %zd at RIP %zX RSP %zX (ctx ptr %p)\n",
            //    top->alt_id, _gc_context_get_rip((Context *)top->context),
            //    _gc_context_get_rsp((Context *)top->context), (void *)top->context);
            
            //printf("GC thread RSP is near... %p\n", &stack);
            
            size_t * stack_top = (size_t *)top->stack_hi;
            //printf("%zu: %zX %zX %zX\n", top->alt_id, top->stack_lo, (size_t)stack, top->stack_hi);
            //if ((size_t)stack >= top->stack_hi)
            //    stack = (size_t *)top->stack_hi;
            assert(stack < stack_top);
            //if ((size_t)stack < top->stack_lo)
            //    stack = (size_t *)top->stack_lo;
            //assert(stack >= (size_t *)top->stack_lo);
            //    printf("into: %zu: %zX %zX %zX\n", top->alt_id, top->stack_lo, (size_t)stack, top->stack_hi);
            _gc_scan_unsanitary(stack, stack_top, &rootlist);
            
            top = top->next;
        }
        if (_gc_debug_spew) puts("!i");
        if (_gc_debug_spew) fflush(stdout);
        
        //puts("f");
        
        if (!silent) printf("found roots: %zd\n", _gc_scan_word_count);
        if (!silent) fflush(stdout);
        
        secs_roots += get_time() - start_time;
        
        /////
        ///// mark phase
        /////
        
        _gc_scan_word_count = 0;
        
        start_time = get_time();
        size_t n = 0;
        while (rootlist)
        {
            char * ptr = _gc_list_pop(&rootlist, &gc_gc_freelist);
            
            GcAllocHeaderPtr base = GcAllocHeaderPtr(ptr-GCOFFS);
            size_t size = _gc_get_size(ptr) / sizeof(size_t);
            
            size_t * start = (size_t *)ptr;
            size_t * end = (size_t *)ptr + size;
            
            if (_gc_get_color(ptr) == GC_RED)
            {
                _gc_set_color(ptr, GC_WHITE);
                continue;
            }
            
            if (base->tracefn && (size_t)(void *)base->tracefn != (size_t)-1)
            {
                GcTraceFunc f = base->tracefn;
                auto userdata = base->tracefndat;
                size_t i = 0;
                void ** v_ = f(ptr, 0, i++, userdata);
                while (v_)
                {
                    char * v = (char *)*v_;
                    _gc_scan_word_count += 1;
                    if (v && _gc_get_color(v) < GC_BLACK)
                    {
                        _gc_set_color(v, GC_GREY);
                        _gc_list_push(&rootlist, v, &gc_gc_freelist);
                    }
                    v_ = f(ptr, v_, i++, userdata);
                }
            }
            else if ((size_t)(void *)base->tracefn != (size_t)-1)
                _gc_scan(start, end, &rootlist);
            
            _gc_set_color(ptr, GC_BLACK);
            
            n += 1;
        }
        
        if (_gc_debug_spew) puts("!j");
        if (_gc_debug_spew) fflush(stdout);
        
        secs_mark += get_time() - start_time;
        
        top = _thread_info_list;
        while (top)
        {
            top->unlock_from_gc();
            top = top->next;
        }
        
        if (_gc_stop.load()) break;
        
        double pause_time = get_time() - start_start_time;
        wasted_seconds += pause_time;
        
        if (_gc_debug_spew) puts("!k");
        if (_gc_debug_spew) fflush(stdout);
        
        /////
        ///// sweep phase
        /////
        
        
        _thread_info_mutex.unlock();
        
        _gc_threads_must_wait_for_gc.store(0);
        
        if (_gc_debug_spew) puts("!l");
        if (_gc_debug_spew) fflush(stdout);
        
        //if (!silent) printf("number of found allocations: %zd over %zd words\n", n, _gc_scan_word_count);
        if (!silent) printf("number of found allocations: %zd over %zd words\n", n, _gc_scan_word_count);
        if (!silent) fflush(stdout);
        start_time = get_time();
        
        std::atomic_size_t n3 = 0;
        std::atomic_size_t filled_num = 0;
        std::atomic_size_t size = 0;
        
        //puts("starting sweep...");
        
        if (GC_TABLE_BITS >= 16)
        {
            size_t threadcount = std::thread::hardware_concurrency();
            threadcount = threadcount > 1 ? threadcount - 1 : threadcount ? threadcount : 1;
            if (threadcount > 16) threadcount = 16;
            
            std::vector<std::thread> threads;
            for (size_t i = 0; i < threadcount; i++)
            {
                size_t start = GC_TABLE_SIZE * i / threadcount;
                size_t end = GC_TABLE_SIZE * (i+1) / threadcount;
                threads.emplace_back(sweeper, &filled_num, &size, &n3, start, end);
                threads_made += 1;
            }
            for (size_t i = 0; i < threadcount; i++)
                threads[i].join();
        }
        else
            sweeper(&filled_num, &size, &n3, 0, GC_TABLE_SIZE);
        
        //puts("done");
        
        if (_gc_stop.load()) break;
        
        if (!silent) printf("freed %zd bytes from %zd allocations\n", size.load(), n3.load());
        gc_table_bytes -= size.load();
        if (!silent) printf("end size %zd bytes\n", gc_table_bytes);
        prev_size = gc_table_bytes;
        test_size = gc_table_bytes;
        
        gc_table_count -= n3.load();
        
        secs_sweep += get_time() - start_time;
        
        if (!silent) printf("number of killed allocations: %zd\n", n3.load());
        if (!silent) fflush(stdout);
        
        if (_gc_stop.load()) break;
        
        /////
        ///// hashtable growth phase
        /////
        
        double fillrate = filled_num/double(GC_TABLE_SIZE);
        /*
        size_t cap = GC_TABLE_SIZE;
        double usage = (double)(gc_table_count) / (double)cap;
        double collisions = (double)(gc_table_count-filled_num) / (double)cap;
        printf("%f %f %f\n", fillrate, usage, collisions);
        */
        
        if (fillrate > 0.9 && GC_TABLE_BITS < 60)
        {
            auto oldsize = GC_TABLE_SIZE;
            GC_TABLE_BITS += 1;
            if (!silent) printf("! growing hashtable to %zd bits........\n", GC_TABLE_BITS);
            GC_TABLE_SIZE = (1ULL<<GC_TABLE_BITS);
            size_t *** old_table = gc_table;
            gc_table = (size_t ***)_walloc_raw_calloc(GC_TABLE_SIZE, sizeof(size_t **));
            
            for (size_t k = 0; k < oldsize; k++)
            {
                size_t ** next = old_table[k];
                while (next)
                {
                    auto next2 = (size_t **)GcAllocHeaderPtr(next-GCOFFS_W)->next;
                    GcAllocHeaderPtr(next-GCOFFS_W)->next = 0;
                    _gc_table_push((char *)next);
                    next = next2;
                }
            }
            
            _walloc_raw_free(old_table);
        }
        _gc_fence();
        
        _gc_table_mutex.unlock();
        
        if (_gc_debug_spew) puts("!m");
        if (_gc_debug_spew) fflush(stdout);
    }
    printf("GC exiting. threads made: %zd\n", threads_made);
    fflush(stdout);
    return 0;
}

#include "gc_os.hpp"
