#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <thread>
#include <assert.h>

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

extern "C" void * gc_malloc(size_t n);
extern "C" void * gc_calloc(size_t c, size_t n);
extern "C" void gc_free(void *);
extern "C" void * gc_realloc(void * _p, size_t n);
extern "C" int gc_start();
extern "C" int gc_end();

// If provided, called during tracing. Return the address containing a GC-owned pointer.
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
static inline void gc_set_trace_func(void * alloc, GcTraceFunc fn, size_t userdata);

// Takes a pointer (void **) that points at a memory region (void *) that may or may not contain non-stack/register roots.
// Only 256 custom roots can be added. Custom roots cannot be removed. Every pointed-to word must be accessible.
// Size is in words, not bytes
static inline void gc_add_custom_root_region(void ** alloc, size_t size);

static inline void fence() { std::atomic_thread_fence(std::memory_order_seq_cst); }

#define WALLOC_CUSTOMHEADER
struct WAllocHeader
{
    size_t size;
    size_t ** next;
    GcTraceFunc tracefn;
    size_t tracefndat;
};
typedef WAllocHeader GcAllocHeader;
typedef WAllocHeader * GcAllocHeaderPtr;
#include "wmalloc.hpp"

#if (!defined(_WIN32)) && defined(GC_USE_LAZY_SPINLOCK)
    // faster than std::mutex (linux only)
    #include <unistd.h>
    struct LazySpinLock
    {
        std::atomic_flag locked = ATOMIC_FLAG_INIT;
        void lock() { while (locked.test_and_set(std::memory_order_acq_rel)) { sleep(0); } }
        void unlock() { locked.clear(std::memory_order_release); }
    };
    typedef LazySpinLock Mutex;
#elif (defined(_WIN32)) && defined(GC_USE_LAZY_SPINLOCK)
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

Mutex safepoint_mutex;
static inline int _gc_m_d_m_d() { fence(); safepoint_mutex.lock(); fence(); return 0; }
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
    ((char *)&(GcAllocHeaderPtr(p-GCOFFS)->size))[7] = color;
}
static inline uint8_t _gc_get_color(char * p)
{
    auto ret = ((char *)&(GcAllocHeaderPtr(p-GCOFFS)->size))[7];
    return ret;
}

static inline void _gc_set_size(char * p, size_t size)
{
    memcpy(&GcAllocHeaderPtr(p-GCOFFS)->size, &size, sizeof(size_t));
}
static inline size_t _gc_get_size(char * p)
{
    size_t ret;
    memcpy(&ret, &GcAllocHeaderPtr(p-GCOFFS)->size, sizeof(size_t));
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

static inline void * _malloc(size_t n)
{
    enforce_yes_main_thread();
    #ifdef GC_CUSTOM_MALLOC
    auto ret = _walloc_raw_malloc(n);
    #else
    auto ret = calloc(1, n);
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
    
    #ifdef GC_CUSTOM_MALLOC
    _walloc_raw_free(p);
    #else
    free(p);
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
static size_t *** gc_table = (size_t ***)_walloc_raw_calloc(GC_TABLE_SIZE, sizeof(size_t **));

static inline int _gc_table_push(char * p)
{
    enforce_not_main_thread();
    size_t k = GC_TABLE_HASH((size_t)p);
    
    int ret = !!gc_table[k];
    GcAllocHeaderPtr(p-GCOFFS)->next = gc_table[k];
    gc_table[k] = ((size_t **)p);
    return ret;
}
static inline size_t ** _gc_table_get(char * p)
{
    enforce_not_main_thread();
    size_t k = GC_TABLE_HASH((size_t)p);
    size_t ** next = gc_table[k];
    while (next && next != (size_t **)p)
        next = GcAllocHeaderPtr(next-GCOFFS_W)->next;
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
// larger number = more hash table collisions, better use of context switches
// lower number = more context switches and worse cache usage, shorter individual pauses
// default: 75000
#define GC_MSG_QUEUE_SIZE 75000
#endif

struct _GcCmdlist {
    //char ** list = (char **)malloc(GC_MSG_QUEUE_SIZE * sizeof(char *));
    char ** list = (char **)_walloc_raw_malloc(GC_MSG_QUEUE_SIZE * sizeof(char *));
    size_t len = 0;
};

_GcCmdlist gc_cmd; // write

std::atomic_uint32_t _gc_baton_atomic = 0;

// called by gc thread
static inline void _gc_safepoint_lock()
{
    fence();
    _gc_baton_atomic = 1;
    safepoint_mutex.lock();
    fence();
}
// called by gc thread
static inline void _gc_safepoint_confirm()
{
    _gc_baton_atomic = 0;
}
// called by gc thread
static inline void _gc_safepoint_unlock()
{
    fence();
    safepoint_mutex.unlock();
    fence();
}

// called by main thread
static void _gc_safepoint_impl_os();
static void _gc_safepoint_impl()
{
    #ifdef COLLECT_STATS
    double start = get_time();
    #endif
    
    _gc_safepoint_impl_os();

    #ifdef COLLECT_STATS
    double pause_time = get_time() - start;
    secs_pause += pause_time;
    if (pause_time > max_pause_time) max_pause_time = pause_time;
    #endif
}

static inline void _gc_safepoint(size_t inc)
{
    static size_t n = 0;
    n = n+inc;
    if (n >= GC_MSG_QUEUE_SIZE)
    {
        n = 0;
        _gc_safepoint_impl();
    }
}


//#define GC_NO_PREFIX

extern "C" void * gc_malloc(size_t n)
{
    if (!n) return 0;
    #ifndef GC_NO_PREFIX
    n = (n+(GCOFFS-1))/GCOFFS*GCOFFS;
    n += GCOFFS;
    #endif
    
    auto i = (n + 0xFF) / 0x100; // size adjustment so very large allocations act like multiple w/r/t GC timing
    _gc_safepoint(i);
    
    char * p = (char *)_malloc(n);
    if (!p) return 0;
    
    #ifndef GC_NO_PREFIX
    p += GCOFFS;
    _gc_set_size(p, n-GCOFFS);
    #endif
    gc_cmd.list[gc_cmd.len++] = p;
    
    return (void *)(p);
}

extern "C" void * gc_calloc(size_t c, size_t n)
{
    return gc_malloc(c*n);
}

extern "C" void gc_free(void * p)
{
    if (!p) return;
    _gc_set_color((char *)p, GC_RED);
}

extern "C" void * gc_realloc(void * _p, size_t n)
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

void ** custom_root[256];
size_t custom_root_size[256]; // in words, not bytes
size_t custom_roots_i = 0;
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

static size_t _main_stack_hi = 0;

static std::atomic_int _gc_stop = 0;

static size_t _gc_scan_word_count = 0;
#define _GC_SCAN(start, end, rootlist)\
{\
    while (start != end)\
    {\
        size_t sw = (size_t)*start;\
        /* if (sw < GCOFFS || (sw & 0x1f)) { start += 1; continue; } */ \
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

struct Context;
static inline size_t _gc_context_size;
static inline Context * _gc_suspend_main_and_get_context();
static inline size_t _gc_context_get_rsp(Context *);
static inline size_t _gc_context_get_size();
static inline void _gc_unsuspend_main();
static inline void _gc_get_data_sections();

static inline unsigned long int _gc_loop(void *)
{
    bool silent = true;
    while (1)
    {
        if (_gc_stop)
            return 0;
        
        if (!silent) puts("-- starting GC cycle");
        if (!silent) fflush(stdout);
        
        _gc_safepoint_lock();
        Context * ctx = _gc_suspend_main_and_get_context();
        _gc_safepoint_confirm();
        fence();
        
        double start_start_time = get_time();
        
        double start_time;
        
        /////
        ///// receive hashtable update commands from main thread phase
        /////
        
        start_time = get_time();
        for (ptrdiff_t i = gc_cmd.len; i > 0; i--)
        {
            char * c = gc_cmd.list[i-1];
            // will be either white (default) or red (freed)
            _gc_table_push(c);
        }
        gc_cmd.len = 0;
        secs_cmd += get_time() - start_time;
        
        if (!silent) puts("-- cmdlist updated");
        
        /////
        ///// root collection phase
        /////
        
        start_time = get_time();
        
        GcListNode * rootlist = 0;
        
        _gc_get_data_sections();
        
        for (size_t i = 0; i < custom_roots_i; i++)
        {
            size_t * c = (size_t *)(custom_root[i]);
            size_t c_size = custom_root_size[i];
            _gc_scan_unsanitary(c, c+c_size, &rootlist);
        }
        
        size_t * c = (size_t *)ctx;
        size_t c_size = _gc_context_get_size()/sizeof(size_t);
        _gc_scan(c, c+c_size, &rootlist);
        
        size_t * stack = (size_t *)(_gc_context_get_rsp(ctx) / 8 * 8);
        size_t * stack_top = (size_t *)_main_stack_hi;
        _gc_scan_unsanitary(stack, stack_top, &rootlist);
        
        if (!silent) printf("found roots\n");
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
            
            if (base->tracefn)
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
            else
                _gc_scan(start, end, &rootlist);
            
            _gc_set_color(ptr, GC_BLACK);
            
            n += 1;
        }
        
        secs_mark += get_time() - start_time;
        
        _gc_safepoint_unlock();
        _gc_unsuspend_main();
        
        double pause_time = get_time() - start_start_time;
        wasted_seconds += pause_time;
        
        /////
        ///// sweep phase
        /////
        
        if (!silent) printf("number of found allocations: %zd over %zd words\n", n, _gc_scan_word_count);
        if (!silent) fflush(stdout);
        start_time = get_time();
        size_t n3 = 0;
        size_t filled_num = 0;
        for (size_t k = 0; k < GC_TABLE_SIZE; k++)
        {
            if (gc_table[k])
                filled_num += 1;
            size_t ** next = gc_table[k];
            size_t ** prev = 0;
            while (next)
            {
                char * c = (char *)next;
                if (_gc_get_color(c) != GC_WHITE)
                    prev = next;
                next = GcAllocHeaderPtr(c-GCOFFS)->next;
                if (_gc_get_color(c) == GC_WHITE)
                {
                    if (prev) GcAllocHeaderPtr(prev-GCOFFS_W)->next = (size_t **)next;
                    else gc_table[k] = next;
                    
                    #ifndef GC_NO_PREFIX
                    _free((void *)(c-GCOFFS));
                    #else
                    _free((void *)c);
                    #endif
                    n3 += 1;
                }
                else // set color for next cycle
                    _gc_set_color(c, GC_WHITE);
            }
        }
        secs_sweep += get_time() - start_time;
        
        if (!silent) printf("number of killed allocations: %zd\n", n3);
        if (!silent) fflush(stdout);
        
        /////
        ///// hashtable growth phase
        /////
        
        double fillrate = filled_num/double(GC_TABLE_SIZE);
        
        if (fillrate > 0.95 && GC_TABLE_BITS < 60)
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
                    _gc_table_push((char *)next);
                    next = GcAllocHeaderPtr(next-GCOFFS_W)->next;
                }
            }
            
            _walloc_raw_free(old_table);
        }
    }
    return 0;
}

#include "gc_os.hpp"
