#include <mutex>
#include <atomic>
#include <assert.h>

static std::mutex _gc_mutex;

enum { GC_COLOR, GC_SIZE, GC_P_START, GC_P_END };
enum { GC_GREY, GC_WHITE, GC_BLACK };
#define GCOFFS 64

static inline void _gc_set_color(char * p, uint8_t color)
{
    //((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].store(color, std::memory_order_release);
    ((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].store(color);
    //((uint8_t *)(p-GCOFFS))[GC_COLOR] = color;
}
static inline uint8_t _gc_get_color(char * p)
{
    //return ((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].load(std::memory_order_acquire);
    return ((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].load();
    //return ((uint8_t *)(p-GCOFFS))[GC_COLOR];
}

struct GcListNode {
    char * val = 0;
    GcListNode * next = 0;
};
static GcListNode * gc_gc_freelist = 0;
static inline void _gc_list_push(GcListNode ** table, char * val, GcListNode ** freelist)
{
    GcListNode * newly;
    if (*freelist)
    {
        newly = *freelist;
        *freelist = (*freelist)->next;
    }
    else
        newly = (GcListNode *)malloc(sizeof(GcListNode));
    newly->val = val;
    newly->next = *table;
    *table = newly;
}
static inline char * _gc_list_pop(GcListNode ** table, GcListNode ** freelist)
{
    auto node = *table;
    *table = node->next;
    
    node->next = *freelist;
    *freelist = node;
    return node->val;
}

template<typename T>
struct GcPtr {
    T * p;
    
    template <typename U, typename = std::enable_if_t<!std::is_same_v<T, U>>>
    GcPtr(const GcPtr<U> & y) : p((T *)y.p)
    {
        colorize();
    }
    GcPtr(const GcPtr<T> & y) : p(y.p)
    {
        colorize();
    }
    GcPtr() = default;
    ~GcPtr() = default;
    
    GcPtr(void * x) { p = (T *)x; }
    
    GcPtr<T> & operator=(void * x) { p = (T *)x; return *this; }
    
    template<typename U>
    T * operator+(U x) const { return p + x; }
    
    ptrdiff_t operator-(GcPtr<T> x) const { return p - x.p; }

    T & operator*() { return *p; }
    T * operator->() { return p; }

    const T & operator*() const { return *p; }
    const T * operator->() const { return p; }
    
    const T & operator[](const size_t i) const { return p[i]; }
    T & operator[](const size_t i) { return p[i]; }
    
    bool operator==(const GcPtr<T> & other) const { return p == other.p; }
    
    void colorize()
    {
        //if (p) _gc_set_color((char *)p, GC_GREY);
    }
    GcPtr<T> & operator=(const GcPtr<T> & y)
    {
        p = y.p;
        colorize();
        return *this;
    }
    GcPtr<T> & operator=(GcPtr<T> && y)
    {
        p = y.p;
        colorize();
        return *this;
    }
    operator bool() const { return !!p; }
    //operator T*() const { return p; }
    //operator void*() const { return (void *)p; }
};
  
template<typename U, typename T>
ptrdiff_t operator-(U a, GcPtr<T> b) { return a - b.p; }

template<typename T>
GcPtr<T> memcpy(GcPtr<T> ptr, const void * source, size_t num)
{
    memcpy(ptr.p, source, num);
    return ptr;
}
template<typename T>
GcPtr<T> memcpy(GcPtr<T> ptr, GcPtr<T> source, size_t num)
{
    memcpy(ptr.p, source.p, num);
    return ptr;
}

template<typename T>
GcPtr<T> memset(GcPtr<T> ptr, int value, size_t num)
{
    memset((void *)ptr.p, value, num);
    return ptr;
}

#include <bit>

#define RAWMALLOC

static GcListNode * _malloc_freelist_freelist = 0;
static GcListNode * _malloc_freelists[64] = {};
static size_t _malloc_freelist_lens[64] = {};
static std::mutex _malloc_mutex;
static inline void * _malloc(size_t n)
{
#ifdef RAWMALLOC
    return malloc(n);
#else
    if (n >= 0x8000000000000000UL)
        return 0;
    n = std::bit_ceil(n);
    size_t l2 = std::countr_zero(n);
    _malloc_mutex.lock();
    if (_malloc_freelists[l2])
    {
        _malloc_freelist_lens[l2] -= 1;
        auto ret = _gc_list_pop(_malloc_freelists + l2, &_malloc_freelist_freelist);
        _malloc_mutex.unlock();
        return ret;
    }
    _malloc_mutex.unlock();
    return malloc(n);
#endif
}
static inline void _free(void * p)
{
#ifdef RAWMALLOC
    free(p);
#else
    size_t n = ((size_t *)p)[GC_SIZE];
    n = std::bit_ceil(n);
    size_t l2 = std::countr_zero(n);
    _malloc_mutex.lock();
    if ((_malloc_freelist_lens[l2]+1) << l2 > 0x4000)
    {
        _malloc_mutex.unlock();
        free(p);
    }
    else
    {
        _malloc_freelist_lens[l2] += 1;
        _gc_list_push(_malloc_freelists + l2, (char *)p, &_malloc_freelist_freelist);
        _malloc_mutex.unlock();
    }
#endif
}

//#define Ptr(X) GcPtr<X>
//#define PtrCast(X, Y) GcPtr<X>{Y}
//#define PtrBase(X) X.p
//#define ISTEMPLATE 1

#define Ptr(X) X *
#define PtrCast(X, Y) (X *)(Y)
#define PtrBase(X) (X)
#define ISTEMPLATE 0

#if !ISTEMPLATE
#define T_TYPE_M void
#endif

static inline void _gc_lock() { _gc_mutex.lock(); }
static inline void _gc_unlock() { _gc_mutex.unlock(); }

//#define GC_TABLE_HASH(X) (((((X)+1)*123454321)>>6)&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) (((((X)+1)*123454321)>>5)&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) (((((X)+1)*123454321)>>8)&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) ((((X)>>6)^(X))&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) ((((X)>>6)^(X))&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) (((X)>>6)&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) ((((X)>>8)^(X))&(GC_TABLE_SIZE - 1))
#define GC_TABLE_HASH(X) (((X)>>5)&(GC_TABLE_SIZE - 1))

//static size_t GC_TABLE_SIZE = (1ULL<<22ULL);
static size_t GC_TABLE_BITS = 16ULL;
static size_t GC_TABLE_SIZE = (1ULL<<GC_TABLE_BITS);
static GcListNode ** gc_table = (GcListNode **)calloc(GC_TABLE_SIZE, sizeof(GcListNode *));

static inline void _gc_table_push(char * p)
{
    p += GCOFFS;
    //printf("adding %p...\n", (void *)p);
    size_t k = GC_TABLE_HASH((uintptr_t)p);
    auto table = &gc_table[k];
    _gc_list_push(table, p, &gc_gc_freelist);
}
static inline GcListNode * _gc_table_get(char * p)
{
    size_t k = GC_TABLE_HASH((uintptr_t)p);
    GcListNode * next = gc_table[k];
    while (next && next->val != p)
        next = next->next;
    return next;
}

struct _GcCmdlist {
    char ** list = (char **)malloc(256 * sizeof(char *));
    size_t len = 0;
    size_t cap = 256;
};

_GcCmdlist gc_cmd; // write
_GcCmdlist gc_cmd2; // read

static std::mutex gc_cmdlist_mtx;

NOINLINE static inline Ptr(char) gc_malloc(size_t n)
{
    if (!n) return PtrCast(char, 0);
    char * p = (char *)_malloc(n+GCOFFS);
    //printf("got %p from malloc\n", p);
    if (!p) return PtrCast(char, 0);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    gc_cmdlist_mtx.lock();
    std::atomic_thread_fence(std::memory_order_seq_cst);
        memset(p, 0, n+GCOFFS);
        ((size_t *)p)[GC_SIZE] = n;
        _gc_set_color(p+GCOFFS, GC_GREY);
        if (gc_cmd.len >= gc_cmd.cap)
        {
            gc_cmd.cap <<= 1;
            std::atomic_thread_fence(std::memory_order_seq_cst);
            gc_cmd.list = (char **)realloc(gc_cmd.list, gc_cmd.cap * sizeof(char *));
            std::atomic_thread_fence(std::memory_order_seq_cst);
        }
        gc_cmd.list[gc_cmd.len] = p;
        gc_cmd.len += 1;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    gc_cmdlist_mtx.unlock();
    std::atomic_thread_fence(std::memory_order_seq_cst);
    
    return PtrCast(char, p+GCOFFS);
}


static inline Ptr(char) gc_calloc(size_t c, size_t n)
{
    return gc_malloc(c*n);
}

#if ISTEMPLATE
template<typename T_TYPE_M>
#endif
//static inline void gc_free(Ptr(T_TYPE_M) _p)
static inline void gc_free(Ptr(T_TYPE_M))
{
    return;
    /*
    char * p = (char *)PtrBase(_p);
    if (!p) return;
    p -= GCOFFS;
    
    _gc_lock();
        _gc_table_remove(p);
        free((char *)p);
    _gc_unlock();
    */
}

#if ISTEMPLATE
template<typename T_TYPE_M>
#endif
static inline Ptr(T_TYPE_M) gc_realloc(Ptr(T_TYPE_M) _p, size_t n)
{
    char * p = (char *)PtrBase(_p);
    #if ISTEMPLATE
    if (!p) return PtrCast(T_TYPE_M, gc_malloc(n));
    #else
    if (!p) return PtrCast(T_TYPE_M, gc_malloc(n));
    #endif
    if (n == 0) { gc_free(_p); return PtrCast(T_TYPE_M, 0); }
    
    p -= GCOFFS;
    
    auto x = PtrCast(T_TYPE_M, gc_malloc(n));
    assert(x);
    size_t size2 = ((size_t *)p)[GC_SIZE];
    memcpy(x, _p, n < size2 ? n : size2);
    return x;
    /*
    _gc_lock();
        _gc_set_color(p+GCOFFS, GC_GREY);
        char * p2 = (char *)realloc(p, n+GCOFFS);
        if (!p2) return _gc_table_remove(p), _gc_unlock(), PtrCast(T_TYPE_M, 0);
        ((size_t *)p2)[GC_SIZE] = n;
        _gc_table_replace(p, p2);
    _gc_unlock();
    
    if (!p2) return PtrCast(T_TYPE_M, 0);
    return PtrCast(T_TYPE_M, p2+GCOFFS);
    */
}

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <windows.h>

static size_t _main_thread = 0;
static size_t _main_stack_hi = 0;
static inline int gc_run_startup()
{
    HANDLE h;
    auto cprc = GetCurrentProcess();
    auto cthd = GetCurrentThread();
    auto retcode = DuplicateHandle(cprc, cthd, cprc, &h, 0, FALSE, DUPLICATE_SAME_ACCESS);
    assert(retcode);
    _main_thread = (size_t)h;
    printf("got main thread! %zd\n", _main_thread);
    
    ULONGLONG lo;
    ULONGLONG hi;
    GetCurrentThreadStackLimits(&lo, &hi);
    _main_stack_hi = (size_t)hi;
    
    return 0;
}
static int _gc_unused_dummy = gc_run_startup();

static std::atomic_int _gc_stop = 0;
static size_t _gc_thread = 0;

static inline void _gc_acquire();
static inline void _gc_release();

static size_t _gc_scan_word_count = 0;
#define _GC_SCAN(start, end, rootlist)\
{\
    while (start != end)\
    {\
        auto v = _gc_table_get((char *)(*start));\
        _gc_scan_word_count += 1;\
        if (v && _gc_get_color(v->val) != GC_BLACK)\
        {\
            _gc_set_color(v->val, GC_GREY);\
            _gc_list_push(rootlist, v->val, &gc_gc_freelist);\
        }\
        start += 1;\
    }\
}

static inline void _gc_scan(uintptr_t * start, uintptr_t * end, GcListNode ** rootlist)
{
    _GC_SCAN(start, end, rootlist)
}

static inline __attribute__((no_sanitize_address))
void _gc_scan_stack(uintptr_t * stack, uintptr_t * stack_top, GcListNode ** rootlist)
{
    _GC_SCAN(stack, stack_top, rootlist)
}


#include <chrono>
static inline double get_time() {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration<double>(duration).count();
}

static double max_pause_time = 0.0;
static double wasted_seconds = 0.0;
static double secs_cmd = 0.0;
static double secs_whiten = 0.0;
static double secs_roots = 0.0;
static double secs_mark = 0.0;
static double secs_sweep = 0.0;
static inline unsigned long int _gc_loop(void *)
{
    bool silent = false;
    while (1)
    {
        Sleep(1);
        if (_gc_stop)
            return 0;
        
        if (!silent) puts("-- starting GC cycle");
        if (!silent) fflush(stdout);
        
        // swap cmd buffers, while preventing main thread from accessing them
        double start_time = get_time();
        std::atomic_thread_fence(std::memory_order_seq_cst);
        gc_cmdlist_mtx.lock();
        std::atomic_thread_fence(std::memory_order_seq_cst);
            auto temp = gc_cmd;
            gc_cmd = gc_cmd2;
            gc_cmd2 = temp;
        std::atomic_thread_fence(std::memory_order_seq_cst);
        gc_cmdlist_mtx.unlock();
        std::atomic_thread_fence(std::memory_order_seq_cst);
        wasted_seconds += get_time() - start_time;
        
        // process received cmd buffer
        for (size_t i = 0; i < gc_cmd2.len; i++)
            _gc_table_push(gc_cmd2.list[i]);
        gc_cmd2.len = 0;
        if (!silent) puts("commands processed");
        
        secs_cmd += get_time() - start_time;
        
        // suspend main thread
        std::atomic_thread_fence(std::memory_order_seq_cst);
        gc_cmdlist_mtx.lock();
        std::atomic_thread_fence(std::memory_order_seq_cst);
            DWORD rval = SuspendThread((HANDLE)_main_thread);
            assert(rval != (DWORD)-1);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        gc_cmdlist_mtx.unlock();
        std::atomic_thread_fence(std::memory_order_seq_cst);
        
        // whitening
        start_time = get_time();
        
        size_t filled_num = 0;
        size_t n2 = 0;
        for (size_t k = 0; k < GC_TABLE_SIZE; k++)
        {
            GcListNode * next = gc_table[k];
            if (next)
                filled_num += 1;
            while (next)
            {
                _gc_set_color(next->val, GC_WHITE);
                next = next->next;
                n2 += 1;
            }
        }
        secs_whiten += get_time() - start_time;
        
        auto cr = (n2-filled_num)/double(filled_num);
        if (!silent) printf("collision rate: %.04f%%\n", cr*100.0);
        auto tm = (n2 > GC_TABLE_SIZE ? n2-GC_TABLE_SIZE : 0)/double(GC_TABLE_SIZE);
        if (!silent) printf("(theoretical min): %.04f%%\n", tm*100.0);
        auto fillrate = filled_num/double(GC_TABLE_SIZE);
        if (!silent) printf("bin fill rate: %.04f%%\n", fillrate*100.0);
        
        std::atomic_thread_fence(std::memory_order_seq_cst);
        
        start_time = get_time();
        
        GcListNode * rootlist = 0;
        
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
        assert(GetThreadContext((HANDLE)_main_thread, &ctx));
        uintptr_t * c = (uintptr_t *)&ctx;
        uintptr_t c_size = sizeof(CONTEXT)/sizeof(uintptr_t);
        _gc_scan(c, c+c_size, &rootlist);
        
        std::atomic_thread_fence(std::memory_order_seq_cst);
        
        uintptr_t * stack = (uintptr_t *)(ctx.Rsp / 8 * 8);
        uintptr_t * stack_top = (uintptr_t *)_main_stack_hi;
        _gc_scan_stack(stack, stack_top, &rootlist);
        
        /*
        size_t n = 0;
        while (rootlist)
        {
            _gc_list_pop(&rootlist);
        }
        printf("number of found roots: %zd\n", n);
        */
        if (!silent) printf("found roots\n");
        if (!silent) fflush(stdout);
        
        std::atomic_thread_fence(std::memory_order_seq_cst);
        
        auto pause_time = get_time() - start_time;
        wasted_seconds += pause_time;
        secs_roots += pause_time;
        if (pause_time > max_pause_time) pause_time = max_pause_time;
        
        std::atomic_thread_fence(std::memory_order_seq_cst);
        
        _gc_scan_word_count = 0;
        
        start_time = get_time();
        size_t n = 0;
        while (rootlist)
        {
            char * ptr = _gc_list_pop(&rootlist, &gc_gc_freelist);
            uintptr_t * base = (uintptr_t *)(ptr-GCOFFS);
            size_t size = base[GC_SIZE] / sizeof(uintptr_t);
            
            uintptr_t * start = (uintptr_t *)ptr;
            uintptr_t * end = (uintptr_t *)ptr + size;
            
            _gc_scan(start, end, &rootlist);
            
            _gc_set_color(ptr, GC_BLACK);
            
            n += 1;
        }
        
        std::atomic_thread_fence(std::memory_order_seq_cst);
        ResumeThread((HANDLE)_main_thread);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        
        wasted_seconds += get_time() - start_time;
        secs_mark += get_time() - start_time;
        if (!silent) printf("number of found allocations: %zd over %zd words\n", n, _gc_scan_word_count);
        if (!silent) fflush(stdout);
        
        start_time = get_time();
        size_t n3 = 0;
        for (size_t k = 0; k < GC_TABLE_SIZE; k++)
        {
            GcListNode * next = gc_table[k];
            GcListNode * prev = 0;
            while (next)
            {
                GcListNode * c = next;
                if (_gc_get_color(c->val) != GC_WHITE)
                    prev = next;
                next = c->next;
                if (_gc_get_color(c->val) == GC_WHITE)
                {
                    if (prev) prev->next = next;
                    else gc_table[k] = next;
                    
                    c->next = gc_gc_freelist;
                    gc_gc_freelist = c;
                    
                    _free(c->val-GCOFFS);
                    n3 += 1;
                }
            }
        }
        secs_sweep += get_time() - start_time;
        
        //_gc_unlock();
        if (!silent) printf("number of killed allocations: %zd\n", n3);
        if (!silent) fflush(stdout);
        
        std::atomic_thread_fence(std::memory_order_seq_cst);
        
        if (fillrate > 0.5 && GC_TABLE_BITS < 60)
        {
            auto oldsize = GC_TABLE_SIZE;
            if (fillrate > 0.95)
                GC_TABLE_BITS += 3;
            else if (fillrate > 0.85)
                GC_TABLE_BITS += 2;
            else
                GC_TABLE_BITS += 1;
            if (!silent) printf("! growing hashtable to %zd bits........\n", GC_TABLE_BITS);
            GC_TABLE_SIZE = (1ULL<<GC_TABLE_BITS);
            GcListNode ** old_table = gc_table;
            gc_table = (GcListNode **)calloc(GC_TABLE_SIZE, sizeof(GcListNode *));
            
            for (size_t k = 0; k < oldsize; k++)
            {
                GcListNode * next = old_table[k];
                while (next)
                {
                    _gc_table_push(next->val-GCOFFS);
                    next = next->next;
                }
            }
            
            free(old_table);
        }
    }
    return 0;
}
static inline int gc_start()
{
    std::atomic_uint8_t dummy;
    if (!std::atomic_is_lock_free(&dummy))
        assert(((void)"atomic byte accesses must be lockfree", 0));
    
    wasted_seconds = 0.0;
    _gc_thread = (size_t)CreateThread(0, 0, &_gc_loop, 0, 0, 0);
    assert(_gc_thread);
    return 0;
}

static inline int gc_end()
{
    _gc_stop = 1;
    WaitForSingleObject((HANDLE)_gc_thread, 0);
    CloseHandle((HANDLE)_gc_thread);
    _gc_thread = 0;
    _gc_stop = 0;
    
    printf("seconds wasted with GC thread blocking main thread: %.4f\n", wasted_seconds);
    printf("cmd\twhiten\troots\tmark\tsweep\tmaxpause\n");
    printf("%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\n", secs_cmd, secs_whiten, secs_roots, secs_mark, secs_sweep, max_pause_time);
    return 0;
}

