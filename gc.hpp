#include <mutex>
#include <atomic>
#include <thread>
#include <assert.h>

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <windows.h>

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

static inline void * gc_malloc(size_t n);
static inline void * gc_calloc(size_t c, size_t n);
static void gc_free(void *);
static void * gc_realloc(void * _p, size_t n);
static int gc_start();
static int gc_end();

static inline void gc_set_trace_func(void * alloc, GcTraceFunc fn, size_t userdata);

std::mutex safepoint_mutex;
static inline void fence0() { /*std::atomic_thread_fence(std::memory_order_seq_cst);*/ }
static inline void fence() { /*std::atomic_thread_fence(std::memory_order_seq_cst);*/ }
static inline void fence2() { /*std::atomic_thread_fence(std::memory_order_seq_cst);*/ }
static inline void fence3() { std::atomic_thread_fence(std::memory_order_seq_cst); }
int _gc_m_d_m_d() { fence3(); safepoint_mutex.lock(); fence3(); return 0; }
int _mutex_dummy = _gc_m_d_m_d();

static size_t _main_thread = 0;
static std::thread::id main_thread_id = std::this_thread::get_id();
static inline void enforce_not_main_thread()
{
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
    return;
    int x = main_thread_id == std::this_thread::get_id();
    if (!x)
    {
        printf("error.............");
        fflush(stdout);
    }
    assert(x);
}

enum { GC_COLOR, GC_SIZE, GC_NEXT, GC_TRACEFN, GC_TRACEFNDAT, GC_USERDATA };
enum { GC_GREY, GC_WHITE, GC_BLACK, GC_RED };
#define GCOFFS_W 8
#define GCOFFS (sizeof(size_t *)*GCOFFS_W)

static inline void _gc_set_color(char * p, uint8_t color)
{
    //assert(((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].is_lock_free());
    fence0();
    
    //((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].store(color, std::memory_order_release);
    ((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].store(color, std::memory_order_seq_cst);
    //((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].store(color);
    //((uint8_t *)(p-GCOFFS))[GC_COLOR] = color;
    fence0();
    
}
static inline uint8_t _gc_get_color(char * p)
{
    //assert(((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].is_lock_free());
    fence0();
    
    //auto ret = ((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].load(std::memory_order_acquire);
    auto ret = ((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].load(std::memory_order_seq_cst);
    //auto ret = ((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].load();
    //auto ret = ((uint8_t *)(p-GCOFFS))[GC_COLOR];
    fence0();
    
    return ret;
}

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
        newly = (GcListNode *)malloc(sizeof(GcListNode));
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
    if (_main_thread != 0)
        enforce_yes_main_thread();
    return malloc(n);
}
static inline void _free(void * p)
{
    enforce_not_main_thread();
    free(p);
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
#define GC_TABLE_HASH(X) (((X)>>5)&(GC_TABLE_SIZE - 1))

static size_t GC_TABLE_BITS = 16ULL;
static size_t GC_TABLE_SIZE = (1ULL<<GC_TABLE_BITS);
static size_t *** gc_table = (size_t ***)calloc(GC_TABLE_SIZE, sizeof(size_t **));

static inline void _gc_table_push(char * _p)
{
    enforce_not_main_thread();
    char * p = _p+GCOFFS;
    size_t k = GC_TABLE_HASH((size_t)p);
    
    ((size_t ***)_p)[GC_NEXT] = gc_table[k];
    gc_table[k] = ((size_t **)p);
}
static inline size_t ** _gc_table_get(char * p)
{
    enforce_not_main_thread();
    size_t k = GC_TABLE_HASH((size_t)p);
    size_t ** next = gc_table[k];
    while (next && next != (size_t **)p)
        next = ((size_t ***)(next-GCOFFS_W))[GC_NEXT];
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

// called by gc thread
static inline void _gc_safepoint_lock()
{
    safepoint_mutex.lock();
    //fence3();
}
// called by gc thread
static inline void _gc_safepoint_unlock()
{
    //fence3();
    safepoint_mutex.unlock();
}
// called by main thread
static inline void _gc_safepoint()
{
    //fence3();  // ???????? why are these ones NOT needed??????
    safepoint_mutex.unlock();
    fence3(); // ????????????? why is this needed to not hit address sanitizer errors  FIXME: figure this out
    safepoint_mutex.lock();
    //fence3(); // ???????? why are these ones NOT needed??????
}

NOINLINE static inline void * gc_malloc(size_t n)
{
    _gc_safepoint();
    
    if (!n) return 0;
    fence();
    gc_cmdlist_mtx.lock();
    fence();
        char * p = (char *)_malloc(n+GCOFFS);
        
        if (!p)
        {
            fence();
            gc_cmdlist_mtx.unlock();
            fence();
            return 0;
        }
        
        memset(p, 0, n+GCOFFS);
        ((size_t *)p)[GC_SIZE] = n;
        _gc_set_color(p+GCOFFS, GC_GREY);
        if (gc_cmd.len >= gc_cmd.cap)
        {
            gc_cmd.cap <<= 1;
            gc_cmd.list = (char **)realloc(gc_cmd.list, gc_cmd.cap * sizeof(char *));
        }
        gc_cmd.list[gc_cmd.len] = p;
        gc_cmd.len += 1;
        
        auto ret = (void *)(p+GCOFFS);
    fence();
    gc_cmdlist_mtx.unlock();
    fence();
    
    return ret;
}

static inline void * gc_calloc(size_t c, size_t n)
{
    return gc_malloc(c*n);
}

static inline void gc_free(void * p)
{
    if (!p) return;
    _gc_set_color((char *)p, GC_RED);
}

static inline void * gc_realloc(void * _p, size_t n)
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
    size_t size2 = ((size_t *)(p-GCOFFS))[GC_SIZE];
    memcpy(x, p, n < size2 ? n : size2);
    gc_free(p);
    
    return x;
}

static inline void gc_set_trace_func(void * alloc, GcTraceFunc fn, size_t userdata)
{
    size_t * p = ((size_t *)alloc)-GCOFFS_W;
    
    //fence3();
    p[GC_TRACEFN] = (size_t)(void *)(fn);
    p[GC_TRACEFNDAT] = userdata;
    //fence3();
}

static size_t _main_stack_hi = 0;
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

static std::atomic_int _gc_stop = 0;

static size_t _gc_scan_word_count = 0;
#define _GC_SCAN(start, end, rootlist)\
{\
    while (start != end)\
    {\
        char * v = (char *)_gc_table_get((char *)(*start));\
        _gc_scan_word_count += 1;\
        if (v && _gc_get_color(v) != GC_BLACK)\
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
void _gc_scan_stack(size_t * stack, size_t * stack_top, GcListNode ** rootlist)
{
    _GC_SCAN(stack, stack_top, rootlist)
}


#include <chrono>
static inline double get_time() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    double ret = std::chrono::duration<double>(duration).count();
    return ret;
}

std::vector<void *> _disposal_list;
std::mutex _disposal_list_mtx;

static inline unsigned long int _disposal_loop(void *)
{
    while (1)
    {
        Sleep(1);
        if (_gc_stop)
            return 0;
        
        fence3();
        
        _disposal_list_mtx.lock();
        gc_cmdlist_mtx.lock();
        fence();
        while (_disposal_list.size())
        {
            free(_disposal_list.back());
            _disposal_list.pop_back();
        }
        _disposal_list = {};
        fence();
        gc_cmdlist_mtx.unlock();
        _disposal_list_mtx.unlock();
    }
}

static double max_pause_time = 0.0;
static double wasted_seconds = 0.0;
static double secs_cmdswap = 0.0;
static double secs_cmd = 0.0;
static double secs_whiten = 0.0;
static double secs_roots = 0.0;
static double secs_mark = 0.0;
static double secs_sweep = 0.0;
static inline unsigned long int _gc_loop(void *)
{
    bool silent = true;
    while (1)
    {
        Sleep(50);
        if (_gc_stop)
            return 0;
        
        fence3();
        
        if (!silent) puts("-- starting GC cycle");
        if (!silent) fflush(stdout);
        
        /////
        ///// receive hashtable update commands from main thread phase
        /////
        
        _gc_safepoint_lock();
        
        double start_start_time = get_time();
        
        // swap cmd buffers, while preventing main thread from accessing them
        fence();
        gc_cmdlist_mtx.lock();
        fence();
        double start_time = get_time();
            auto temp = gc_cmd;
            gc_cmd = gc_cmd2;
            gc_cmd2 = temp;
        double pause_time = get_time() - start_time;
        fence();
        gc_cmdlist_mtx.unlock();
        fence();
        
        wasted_seconds += pause_time;
        secs_cmdswap += pause_time;
        
        if (pause_time > max_pause_time) max_pause_time = pause_time;
        
        /////
        ///// hashtable pre-processing phase
        /////
        
        // gc_safepoint_unlock();
        
        // process received cmd buffer
        start_time = get_time();
        for (size_t i = 0; i < gc_cmd2.len; i++)
        {
            char * c = gc_cmd2.list[i];
            _gc_table_push(c);
            _gc_set_color(c+GCOFFS, GC_GREY);
        }
        gc_cmd2.len = 0;
        if (!silent) puts("commands processed");
        secs_cmd += get_time() - start_time;
        //wasted_seconds += get_time() - start_time;
        
        // gc_safepoint_lock();
        
        /////
        ///// initial coloring phase
        /////
        
        // whitening
        size_t filled_num = 0;
        size_t n2 = 0;
        for (size_t k = 0; k < GC_TABLE_SIZE; k++)
        {
            size_t ** next = gc_table[k];
            if (next)
                filled_num += 1;
            while (next)
            {
                _gc_set_color((char *)next, GC_WHITE);
                next = ((size_t ***)(next-GCOFFS_W))[GC_NEXT];
                n2 += 1;
            }
        }
        if (!silent) puts("whitening done");
        
        secs_whiten += get_time() - start_time;
        //wasted_seconds += get_time() - start_time;
        
        auto cr = (n2-filled_num)/double(filled_num);
        if (!silent) printf("collision rate: %.04f%%\n", cr*100.0);
        auto tm = (n2 > GC_TABLE_SIZE ? n2-GC_TABLE_SIZE : 0)/double(GC_TABLE_SIZE);
        if (!silent) printf("(theoretical min): %.04f%%\n", tm*100.0);
        auto fillrate = filled_num/double(GC_TABLE_SIZE);
        if (!silent) printf("bin fill rate: %.04f%%\n", fillrate*100.0);
        
        //gc_safepoint_unlock();
        
        if (!silent) puts("-- swapped");
        
        /////
        ///// root collection phase
        /////
        
        //gc_safepoint_lock();
        
        // suspend main thread to read its registers and stack
        fence();
        gc_cmdlist_mtx.lock();
        fence();
            DWORD rval = SuspendThread((HANDLE)_main_thread);
            int nx = 100000;
            while (rval == (DWORD)-1 && nx-- > 0)
                rval = SuspendThread((HANDLE)_main_thread);
            assert(rval != (DWORD)-1);
        fence();
        gc_cmdlist_mtx.unlock();
        fence();
        
        start_time = get_time();
        
        GcListNode * rootlist = 0;
        
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
        assert(GetThreadContext((HANDLE)_main_thread, &ctx));
        size_t * c = (size_t *)&ctx;
        size_t c_size = sizeof(CONTEXT)/sizeof(size_t);
        _gc_scan(c, c+c_size, &rootlist);
        
        size_t * stack = (size_t *)(ctx.Rsp / 8 * 8);
        size_t * stack_top = (size_t *)_main_stack_hi;
        _gc_scan_stack(stack, stack_top, &rootlist);
        
        if (!silent) printf("found roots\n");
        if (!silent) fflush(stdout);
        
        pause_time = get_time() - start_time;
        //wasted_seconds += pause_time;
        secs_roots += pause_time;
        if (pause_time > max_pause_time) max_pause_time = pause_time;
        
        /////
        ///// mark phase
        /////
        
        _gc_scan_word_count = 0;
        
        start_time = get_time();
        size_t n = 0;
        //fence3();
        while (rootlist)
        {
            char * ptr = _gc_list_pop(&rootlist, &gc_gc_freelist);
            size_t * base = (size_t *)(ptr-GCOFFS);
            size_t size = base[GC_SIZE] / sizeof(size_t);
            
            size_t * start = (size_t *)ptr;
            size_t * end = (size_t *)ptr + size;
            
            if (_gc_get_color(ptr) == GC_RED)
            {
                _gc_set_color(ptr, GC_WHITE);
                continue;
            }
            
            //auto old_rootlist = rootlist;
            
            if (base[GC_TRACEFN])
            {
                GcTraceFunc f = (GcTraceFunc)(void *)(base[GC_TRACEFN]);
                size_t userdata = base[GC_TRACEFNDAT];
                size_t i = 0;
                void ** v_ = f(ptr, 0, i++, userdata);
                while (v_)
                {
                    char * v = (char *)*v_;
                    //char * v = (char *)_gc_table_get((char *)(*v_));
                    _gc_scan_word_count += 1;
                    if (v && _gc_get_color(v) != GC_BLACK)
                    {
                        _gc_set_color(v, GC_GREY);
                        _gc_list_push(&rootlist, v, &gc_gc_freelist);
                    }
                    v_ = f(ptr, v_, i++, userdata);
                }
            }
            else
                _gc_scan(start, end, &rootlist);
            
            //if (old_rootlist == rootlist) { }
            
            _gc_set_color(ptr, GC_BLACK);
            
            n += 1;
        }
        //fence3();
        
        fence();
        _gc_safepoint_unlock();
        fence();
        ResumeThread((HANDLE)_main_thread);
        fence();
        
        pause_time = get_time() - start_time;
        //wasted_seconds += pause_time;
        secs_mark += pause_time;
        
        //pause_time += pause_time2;
        
        pause_time = get_time() - start_start_time;
        wasted_seconds += pause_time;
        if (pause_time > max_pause_time) max_pause_time = pause_time;
        
        /////
        ///// sweep phase
        /////
        
        //#define USE_DISPOSAL_THREAD
        
        if (!silent) printf("number of found allocations: %zd over %zd words\n", n, _gc_scan_word_count);
        if (!silent) fflush(stdout);
        #ifdef USE_DISPOSAL_THREAD
        fence();
        _disposal_list_mtx.lock();
        fence();
        #endif
        start_time = get_time();
        size_t n3 = 0;
        for (size_t k = 0; k < GC_TABLE_SIZE; k++)
        {
            size_t ** next = gc_table[k];
            size_t ** prev = 0;
        #ifndef USE_DISPOSAL_THREAD
            fence();
            bool locked = false;
            fence();
        #endif
            while (next)
            {
                char * c = (char *)next;
                if (_gc_get_color(c) != GC_WHITE)
                    prev = next;
                next = ((size_t ***)(c-GCOFFS))[GC_NEXT];
                if (_gc_get_color(c) == GC_WHITE)
                {
                    if (prev) (prev-GCOFFS_W)[GC_NEXT] = (size_t *)next;
                    else gc_table[k] = next;
                    
        #ifdef USE_DISPOSAL_THREAD
                    _disposal_list.push_back((void *)(c-GCOFFS));
        #else
                    if (!locked)
                    {
                        locked = true;
                        gc_cmdlist_mtx.lock(); // block main thread malloc from returning
                    }
                    _free((void *)(c-GCOFFS));
        #endif
                    n3 += 1;
                }
            }
        #ifndef USE_DISPOSAL_THREAD
            fence();
            if (locked)
                gc_cmdlist_mtx.unlock();
            fence();
        #endif
        }
        #ifdef USE_DISPOSAL_THREAD
        fence();
        _disposal_list_mtx.unlock();
        fence();
        #endif
        secs_sweep += get_time() - start_time;
        
        if (!silent) printf("number of killed allocations: %zd\n", n3);
        if (!silent) fflush(stdout);
        
        /////
        ///// hashtable growth phase
        /////
        
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
            size_t *** old_table = gc_table;
            gc_table = (size_t ***)calloc(GC_TABLE_SIZE, sizeof(size_t **));
            
            for (size_t k = 0; k < oldsize; k++)
            {
                size_t ** next = old_table[k];
                while (next)
                {
                    _gc_table_push(((char *)next)-GCOFFS);
                    next = ((size_t ***)(next-GCOFFS_W))[GC_NEXT];
                }
            }
            
            free(old_table);
        }
    }
    return 0;
}

static size_t _gc_thread = 0;
static size_t _disposal_thread = 0;
static inline int gc_start()
{
    fence3();
    gc_run_startup();
    
    std::atomic_uint8_t dummy;
    if (!std::atomic_is_lock_free(&dummy))
        assert(((void)"atomic byte accesses must be lockfree", 0));
    
    wasted_seconds = 0.0;
    _gc_thread = (size_t)CreateThread(0, 0, &_gc_loop, 0, 0, 0);
    assert(_gc_thread);
    #ifdef USE_DISPOSAL_THREAD
    _disposal_thread = (size_t)CreateThread(0, 0, &_disposal_loop, 0, 0, 0);
    assert(_disposal_thread);
    #endif
    return 0;
}
static inline int gc_end()
{
    _gc_stop = 1;
    WaitForSingleObject((HANDLE)_gc_thread, 0);
    CloseHandle((HANDLE)_gc_thread);
    _gc_thread = 0;
    _gc_stop = 0;
    #ifdef USE_DISPOSAL_THREAD
    WaitForSingleObject((HANDLE)_disposal_thread, 0);
    CloseHandle((HANDLE)_disposal_thread);
    _disposal_thread = 0;
    #endif
    
    printf("seconds wasted with GC thread blocking main thread: %.4f\n", wasted_seconds);
    printf("cmdswap\tcmd\twhiten\troots\tmark\tsweep\thipause\thxtable_bits\n");
    printf("%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%zu\n", secs_cmdswap, secs_cmd, secs_whiten, secs_roots, secs_mark, secs_sweep, max_pause_time, GC_TABLE_BITS);
    return 0;
}

