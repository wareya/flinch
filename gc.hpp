#include <mutex>
#include <atomic>
#include <thread>
#include <assert.h>

#ifdef COLLECT_STATS
#include <chrono>
static inline double get_time() {
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


std::mutex safepoint_mutex;
static inline void fence() { std::atomic_thread_fence(std::memory_order_seq_cst); }
struct _GC_S { int _s; };
static _GC_S _gc_m_d_m_d() { fence(); safepoint_mutex.lock(); fence(); return {0}; }
static inline _GC_S __attribute__((init_priority(102))) _mutex_dummy = _gc_m_d_m_d();

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

struct GcAllocHeader
{
    size_t ** next;
    size_t size;
    GcTraceFunc tracefn;
    size_t tracefndat;
};

typedef GcAllocHeader * GcAllocHeaderPtr;

enum { GC_GREY, GC_WHITE, GC_BLACK, GC_RED };
#define GCOFFS_W ((sizeof(GcAllocHeader)+7)/8)
#define GCOFFS (sizeof(size_t *)*GCOFFS_W)

static inline void _gc_set_color(char * p, uint8_t color)
{
    std::atomic_ref(((char *)&(GcAllocHeaderPtr(p-GCOFFS)->size))[7]).store(color, std::memory_order_release);
}
static inline uint8_t _gc_get_color(char * p)
{
    auto ret = std::atomic_ref(((char *)&(GcAllocHeaderPtr(p-GCOFFS)->size))[7]).load(std::memory_order_acquire);
    return ret;
}

static inline void _gc_set_size(char * p, size_t size)
{
    GcAllocHeaderPtr(p-GCOFFS)->size = size;
}
static inline size_t _gc_get_size(char * p)
{
    auto ret = GcAllocHeaderPtr(p-GCOFFS)->size;
    ret <<= 8;
    ret >>= 8;
    return ret;
}

inline static void * _gc_raw_malloc(size_t n);
inline static void * _gc_raw_calloc(size_t c, size_t n);
inline static void _gc_raw_free(void * _p);

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
        newly = (GcListNode *)_gc_raw_malloc(sizeof(GcListNode));
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
    auto ret = _gc_raw_malloc(n);
    #ifndef GC_CUSTOM_MALLOC
    memset(ret, 0, n);
    #endif
    return ret;
    //return calloc(1, n);
}
static inline void _free(void * p)
{
    enforce_not_main_thread();
    //free(p);
    _gc_raw_free(p);
}

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

const static size_t GC_TABLE_BITS = 16ULL;
const static size_t GC_TABLE_SIZE = (1ULL<<GC_TABLE_BITS);
static std::atomic<size_t **>* gc_table = (std::atomic<size_t **>*)_gc_raw_calloc(GC_TABLE_SIZE, sizeof(std::atomic<size_t **>));

static inline void _gc_table_push(char * p)
{
    enforce_not_main_thread();
    size_t k = GC_TABLE_HASH((size_t)p);
    
    //GcAllocHeaderPtr(p-GCOFFS)->next = gc_table[k];
    //gc_table[k] = ((size_t **)p);

    auto gp = GcAllocHeaderPtr(p-GCOFFS);
    
    do
    {
        std::atomic_ref(gp->next).store(gc_table[k].load(std::memory_order_acquire), std::memory_order_release);
    } while (!gc_table[k].compare_exchange_weak(*(size_t ***)&gp->next, (size_t **)p, std::memory_order_release, std::memory_order_acquire));
}
static inline size_t ** _gc_table_get(char * p)
{
    enforce_not_main_thread();
    size_t k = GC_TABLE_HASH((size_t)p);
    size_t ** next = gc_table[k].load(std::memory_order_acquire);
    while (next && next != (size_t **)p)
        next = std::atomic_ref(GcAllocHeaderPtr(next-GCOFFS_W)->next).load(std::memory_order_acquire);
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
#define GC_MSG_QUEUE_SIZE 1000
#endif

#ifndef GC_INSENSITIVITY
#define GC_INSENSITIVITY 75000
#endif

struct _GcCmdlist {
    //char ** list = (char **)malloc(GC_MSG_QUEUE_SIZE * sizeof(char *));
    char ** list = (char **)_gc_raw_malloc(GC_MSG_QUEUE_SIZE * sizeof(char *));
    size_t len = 0;
};

thread_local static _GcCmdlist gc_cmd;

static std::atomic_uint32_t _gc_baton_atomic = 0;

// called by gc thread
static inline void _gc_safepoint_lock()
{
    fence();
    _gc_baton_atomic = 1;
    safepoint_mutex.lock();
    fence();
}
// called by gc thread
static inline void _gc_safepoint_unlock()
{
    fence();
    safepoint_mutex.unlock();
    _gc_baton_atomic = 0;
    fence();
}
// called by main thread

static void _gc_safepoint_impl_os();
static void _gc_safepoint_impl()
{
    _gc_safepoint_impl_os();
}
static inline void _gc_check_cmd()
{
    if (gc_cmd.len >= GC_MSG_QUEUE_SIZE)
    {
        #ifdef COLLECT_STATS
        double start = get_time();
        #endif
        
        for (ptrdiff_t i = gc_cmd.len; i > 0; i--)
        {
            _gc_set_color(gc_cmd.list[i-1], GC_GREY);
            _gc_table_push(gc_cmd.list[i-1]);
        }
        gc_cmd.len = 0;
    
        #ifdef COLLECT_STATS
        double pause_time = get_time() - start;
        secs_cmd += pause_time;
        if (pause_time > max_pause_time) max_pause_time = pause_time;
        #endif
    }
}

static inline void _gc_safepoint(size_t inc)
{
    static size_t n = 0;
    n = n+inc;
    if (n >= GC_INSENSITIVITY)
    {
        #ifdef COLLECT_STATS
        double start = get_time();
        #endif
        
        for (ptrdiff_t i = gc_cmd.len; i > 0; i--)
        {
            _gc_set_color(gc_cmd.list[i-1], GC_WHITE);
            _gc_table_push(gc_cmd.list[i-1]);
        }
        gc_cmd.len = 0;
        
        n = 0;
        _gc_safepoint_impl();
    
        #ifdef COLLECT_STATS
        double pause_time = get_time() - start;
        secs_pause += pause_time;
        if (pause_time > max_pause_time) max_pause_time = pause_time;
        #endif
    }
    else
        _gc_check_cmd();
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
    if (!p) return gc_malloc(n);
    if (n == 0) { gc_free(p); return 0; }
    
    auto x = (char *)gc_malloc(n);
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

static inline bool _gc_color_is_traceable(uint8_t c)
{
    return c != GC_BLACK && c != GC_RED;
}

static size_t _main_stack_hi;

static std::atomic_int _gc_stop = 0;

static size_t _gc_scan_word_count = 0;
#define _GC_SCAN(start, end, rootlist)\
{\
    while (start != end)\
    {\
        size_t sw = (size_t)*start;\
        if (sw < GCOFFS || (sw & 0x1f)) { start += 1; continue; } \
        char * v = (char *)_gc_table_get((char *)sw);\
        _gc_scan_word_count += 1;\
        if (v && _gc_color_is_traceable(_gc_get_color(v)))\
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
        
        double start_start_time = get_time();
        double start_time = get_time();
        
        if (!silent) puts("-- cmdlist updated");
        
        /////
        ///// root collection phase
        /////
        
        start_time = get_time();
        
        GcListNode * rootlist = 0;
        
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
        assert(stack);
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
                    if (v && _gc_color_is_traceable(_gc_get_color(v)))
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
        size_t n4 = 0;
        for (size_t k = 0; k < GC_TABLE_SIZE; k++)
        {
            size_t ** head = 0;
            head = gc_table[k].exchange(head, std::memory_order_acq_rel);
            if (!head) continue;
            
            size_t ** next = head;
            size_t ** prev = 0;
            while (next)
            {
                char * c = (char *)next;
                if (_gc_get_color(c) != GC_WHITE)
                    prev = next;
                next = std::atomic_ref(GcAllocHeaderPtr(c-GCOFFS)->next).load(std::memory_order_acquire);
                if (_gc_get_color(c) == GC_WHITE)
                {
                    if (prev) std::atomic_ref(GcAllocHeaderPtr(prev-GCOFFS_W)->next).store((size_t **)next, std::memory_order_release);
                    else head = next;
                    
                    #ifndef GC_NO_PREFIX
                    _free((void *)(c-GCOFFS));
                    #else
                    _free((void *)c);
                    #endif
                    n3 += 1;
                }
                else // initial coloring for next GC cycle
                    _gc_set_color(c, GC_WHITE);
                n4 += 1;
            }
            
            if (prev)
            {
                head = gc_table[k].exchange(head);
                std::atomic_ref(GcAllocHeaderPtr(prev-GCOFFS_W)->next).store(head, std::memory_order_release);
            }
        }
        secs_sweep += get_time() - start_time;
        
        if (!silent) printf("number of killed allocations: %zd (out of %zd)\n", n3, n4);
        if (!silent) fflush(stdout);
    }
    return 0;
}

#include "gc_os.hpp"
