#include <mutex>
#include <atomic>
#include <thread>
#include <assert.h>

#include <chrono>
static inline double get_time() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    double ret = std::chrono::duration<double>(duration).count();
    return ret;
}
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <windows.h>
#include <psapi.h>

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

// Takes a pointer (void **) that points at a memory region (void *) that may or may not contain non-stack/register roots.
// Only 256 custom roots can be added. Custom roots cannot be removed. Every pointed-to word must be accessible.
// Size is in words, not bytes
static inline void gc_add_custom_root_region(void ** alloc, size_t size);

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
    if (_main_thread != 0) return;
    
    int x = main_thread_id == std::this_thread::get_id();
    if (!x)
    {
        printf("error.............");
        fflush(stdout);
    }
    assert(x);
}

enum { GC_NEXT, GC_COLOR, GC_SIZE, GC_TRACEFN, GC_TRACEFNDAT, GC_USERDATA };
enum { GC_GREY, GC_WHITE, GC_BLACK, GC_RED };
#define GCOFFS_W 8
#define GCOFFS (sizeof(size_t *)*GCOFFS_W)

static inline void _gc_set_color(char * p, uint8_t color)
{
    //assert(((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].is_lock_free());
    fence0();
    
    //((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].store(color, std::memory_order_release);
    //((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].store(color, std::memory_order_seq_cst);
    //((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].store(color);
    ((uint8_t *)(p-GCOFFS))[GC_COLOR*8] = color;
    fence0();
    
}
static inline uint8_t _gc_get_color(char * p)
{
    //assert(((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].is_lock_free());
    fence0();
    
    //auto ret = ((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].load(std::memory_order_acquire);
    //auto ret = ((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].load(std::memory_order_seq_cst);
    //auto ret = ((std::atomic_uint8_t *)(p-GCOFFS))[GC_COLOR].load();
    auto ret = ((uint8_t *)(p-GCOFFS))[GC_COLOR*8];
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
//#define GC_TABLE_HASH(X) (((X)>>4)&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) (((X)>>5)&(GC_TABLE_SIZE - 1))
//#define GC_TABLE_HASH(X) (((X)>>6)&(GC_TABLE_SIZE - 1))
#define GC_TABLE_HASH(X) (((X)>>7)&(GC_TABLE_SIZE - 1))

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

static double max_pause_time = 0.0;
static double wasted_seconds = 0.0;
static double secs_cmd = 0.0;
static double secs_whiten = 0.0;
static double secs_roots = 0.0;
static double secs_mark = 0.0;
static double secs_sweep = 0.0;

constexpr int msg_queue_size = 200000; // number of allocations (size-adjusted) before GC happens

struct _GcCmdlist {
    char ** list = (char **)malloc(msg_queue_size * sizeof(char *));
    size_t len = 0;
};

_GcCmdlist gc_cmd; // write

std::atomic_uint8_t _gc_baton_atomic = 0;

// called by gc thread
static inline void _gc_safepoint_lock()
{
    _gc_baton_atomic = 1;
    safepoint_mutex.lock();
}
// called by gc thread
static inline void _gc_safepoint_unlock()
{
    safepoint_mutex.unlock();
    _gc_baton_atomic = 0;
}
// called by main thread

static inline void _gc_safepoint(size_t inc)
{
    static size_t n = 0;
    n = n+inc;
    if (n >= msg_queue_size)
    {
        n = 0;
        
        double start = get_time();
        for (ptrdiff_t i = gc_cmd.len - 1; i >= 0; i--)
        {
            char * c = gc_cmd.list[i];
            _gc_table_push(c);
        }
        gc_cmd.len = 0;
        
        if (_gc_baton_atomic)
        {
            safepoint_mutex.unlock();
            while (_gc_baton_atomic) { }
            safepoint_mutex.lock();
        }
        secs_cmd += get_time() - start;
    }
}

static inline void * gc_malloc(size_t n)
{
    if (!n) return 0;
    auto i = (n + 0xFFF) / 0x1000; // size adjustment so very large allocations act like multiple w/r/t GC timing
    _gc_safepoint(i);
    
    char * p = (char *)_malloc(n+GCOFFS);
    if (!p) return 0;
    
    memset(p, 0, n+GCOFFS);
    ((size_t *)p)[GC_SIZE] = n;
    gc_cmd.list[gc_cmd.len++] = p;
    return (void *)(p+GCOFFS);
}

static inline void * gc_calloc(size_t c, size_t n)
{
    return gc_malloc(c*n);
}

static inline void gc_free(void * p)
{
    if (!p) return;
    //_gc_set_color((char *)p, GC_RED);
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
    
    p[GC_TRACEFN] = (size_t)(void *)(fn);
    p[GC_TRACEFNDAT] = userdata;
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
void _gc_scan_unsanitary(size_t * stack, size_t * stack_top, GcListNode ** rootlist)
{
    _GC_SCAN(stack, stack_top, rootlist)
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
        
        _disposal_list_mtx.lock();
        size_t i = 0;
        while (_disposal_list.size())
        {
            free(_disposal_list.back());
            _disposal_list.pop_back();
            i++;
            if ((i & 0x3FFF) == 0)
            {
                _disposal_list_mtx.unlock();
                Sleep(0);
                _disposal_list_mtx.lock();
            }
        }
        _disposal_list = {};
        _disposal_list_mtx.unlock();
    }
}

static inline unsigned long int _gc_loop(void *)
{
    bool silent = true;
    while (1)
    {
        Sleep(1);
        if (_gc_stop)
            return 0;
        
        if (!silent) puts("-- starting GC cycle");
        if (!silent) fflush(stdout);
        
        /////
        ///// receive hashtable update commands from main thread phase
        /////
        
        _gc_safepoint_lock();
        
        // suspend main thread to read its registers and stack
        fence();
        fence();
            CONTEXT ctx = {};
            //ctx.ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
            ctx.ContextFlags = CONTEXT_INTEGER | CONTEXT_CONTROL | CONTEXT_FLOATING_POINT | CONTEXT_DEBUG_REGISTERS;
            DWORD rval = SuspendThread((HANDLE)_main_thread);
            int nx = 100000;
            while (rval == (DWORD)-1 && nx-- > 0)
                rval = SuspendThread((HANDLE)_main_thread);
            assert(rval != (DWORD)-1);
            
            assert(GetThreadContext((HANDLE)_main_thread, &ctx));
            //printf("rbp:%p\trsp:%p\trip:%p\tstack height:%zu\n", (void *)ctx.Rbp, (void *)ctx.Rsp, (void *)ctx.Rip, _main_stack_hi-ctx.Rsp);
        fence();
        fence();
        
        double start_start_time = get_time();
        
        // gc_safepoint_lock();
        
        /////
        ///// initial coloring phase
        /////
        
        // whitening
        double start_time = get_time();
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
                next = ((size_t ** *)(next-GCOFFS_W))[GC_NEXT];
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
        
        start_time = get_time();
        
        GcListNode * rootlist = 0;
        
        for (size_t i = 0; i < custom_roots_i; i++)
        {
            size_t * c = (size_t *)(custom_root[i]);
            size_t c_size = custom_root_size[i];
            _gc_scan_unsanitary(c, c+c_size, &rootlist);
        }
        
        size_t * c = (size_t *)&ctx;
        size_t c_size = sizeof(CONTEXT)/sizeof(size_t);
        _gc_scan(c, c+c_size, &rootlist);
        
        size_t * stack = (size_t *)(ctx.Rsp / 8 * 8);
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
        
        secs_mark += get_time() - start_time;
        
        /////
        ///// sweep phase
        /////
        
        #define USE_DISPOSAL_THREAD
        
        if (!silent) printf("number of found allocations: %zd over %zd words\n", n, _gc_scan_word_count);
        if (!silent) fflush(stdout);
        #ifdef USE_DISPOSAL_THREAD
        fence3();
        _disposal_list_mtx.lock();
        fence3();
        #endif
        start_time = get_time();
        size_t n3 = 0;
        filled_num = 0;
        for (size_t k = 0; k < GC_TABLE_SIZE; k++)
        {
            size_t ** next = gc_table[k];
            size_t ** prev = 0;
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
                    _free((void *)(c-GCOFFS));
        #endif
                    n3 += 1;
                }
            }
            if (gc_table[k])
                filled_num += 1;
        }
        #ifdef USE_DISPOSAL_THREAD
        fence3();
        _disposal_list_mtx.unlock();
        fence3();
        #endif
        secs_sweep += get_time() - start_time;
        
        if (!silent) printf("number of killed allocations: %zd\n", n3);
        if (!silent) fflush(stdout);
        
        /////
        ///// hashtable growth phase
        /////
        
        //fillrate = filled_num/double(GC_TABLE_SIZE);
        
        if (fillrate > 0.95 && GC_TABLE_BITS < 60)
        {
            auto oldsize = GC_TABLE_SIZE;
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
        
        _gc_safepoint_unlock();
        ResumeThread((HANDLE)_main_thread);
        
        double pause_time = get_time() - start_start_time;
        wasted_seconds += pause_time;
        if (pause_time > max_pause_time) max_pause_time = pause_time;
    }
    return 0;
}

void _gc_get_data_sections()
{
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

static size_t _gc_thread = 0;
#ifdef USE_DISPOSAL_THREAD
static size_t _disposal_thread = 0;
#endif
static inline int gc_start()
{
    _gc_get_data_sections();
    
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
    fence3();
    CloseHandle((HANDLE)_gc_thread);
    _gc_thread = 0;
    #ifdef USE_DISPOSAL_THREAD
    WaitForSingleObject((HANDLE)_disposal_thread, 0);
    _disposal_list_mtx.lock();
    fence3();
    _disposal_list_mtx.unlock();
    CloseHandle((HANDLE)_disposal_thread);
    _disposal_thread = 0;
    #endif
    
    _gc_stop = 0;
    
    printf("seconds wasted with GC thread blocking main thread: %.4f\n", wasted_seconds);
    printf("cmd\twhiten\troots\tmark\tsweep\thipause\thxtable_bits\n");
    printf("%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%zu\n", secs_cmd, secs_whiten, secs_roots, secs_mark, secs_sweep, max_pause_time, GC_TABLE_BITS);
    return 0;
}

