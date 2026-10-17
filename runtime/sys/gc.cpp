#include "gc.h"
#include <cstdlib>
#include <cstring>
#include <csetjmp>
#include <cstdio>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#if defined(__WIIU__)
#include <malloc.h>
#endif

namespace plat { int64_t now_ns(); void log(const char*); }

namespace sys {

static const uintptr_t PAGE_SHIFT = 16;
static const size_t PAGE_SIZE = (size_t)1 << PAGE_SHIFT;
static const size_t LARGE_LIMIT = 16384;

static const uint32_t kSizeClasses[] = { 16, 32, 48, 64, 80, 96, 128, 160, 192, 256, 320, 384, 512, 640, 768, 1024, 1280, 1536, 2048, 3072, 4096, 5440, 8192, 10912, 16384 };
static const int kNumClasses = sizeof(kSizeClasses) / sizeof(kSizeClasses[0]);

struct Page {
    uint8_t* base;
    uint32_t cellSize;
    uint32_t nCells;
    uint32_t nFree;
    int sizeClass;
    void* freeList;
    std::vector<uint8_t> allocated;
    std::vector<uint8_t> marked;
    bool large;
    size_t largeSize;
    bool inFreePages;
    bool atomic;    // contents contain no GC pointers: marked but never scanned
};

// Page lookup by (address >> 16). Flat table on 32-bit targets (Wii U), open-addressed hash on 64-bit.
struct PageMap
{
#if UINTPTR_MAX == 0xFFFFFFFFu
    Page* table[1u << 16] = {};
    Page* get(uintptr_t k) const { return table[k & 0xFFFF]; }
    void set(uintptr_t k, Page* p) { table[k & 0xFFFF] = p; }
    void erase(uintptr_t k) { table[k & 0xFFFF] = nullptr; }
#else
    std::vector<uintptr_t> keys;
    std::vector<Page*> vals;
    size_t count = 0, tomb = 0;
    static constexpr uintptr_t EMPTY = 0, TOMB = 1;
    PageMap() { keys.assign(1024, EMPTY); vals.assign(1024, nullptr); }
    static size_t H(uintptr_t k) { k ^= k >> 17; k *= 0x9E3779B97F4A7C15ull; return (size_t)(k >> 20); }
    Page* get(uintptr_t k) const
    {
        k += 2;
        size_t m = keys.size() - 1;
        for (size_t i = H(k) & m;; i = (i + 1) & m)
        {
            uintptr_t c = keys[i];
            if (c == k) return vals[i];
            if (c == EMPTY) return nullptr;
        }
    }
    void grow()
    {
        std::vector<uintptr_t> ok; std::vector<Page*> ov;
        ok.swap(keys); ov.swap(vals);
        keys.assign(ok.size() * 2, EMPTY); vals.assign(ok.size() * 2, nullptr);
        count = 0; tomb = 0;
        for (size_t i = 0; i < ok.size(); i++) if (ok[i] > TOMB) insert(ok[i], ov[i]);
    }
    void insert(uintptr_t k2, Page* p)
    {
        size_t m = keys.size() - 1;
        for (size_t i = H(k2) & m;; i = (i + 1) & m)
            if (keys[i] <= TOMB || keys[i] == k2) { if (keys[i] <= TOMB) count++; keys[i] = k2; vals[i] = p; return; }
    }
    void set(uintptr_t k, Page* p)
    {
        if ((count + tomb + 1) * 2 > keys.size()) grow();
        insert(k + 2, p);
    }
    void erase(uintptr_t k)
    {
        k += 2;
        size_t m = keys.size() - 1;
        for (size_t i = H(k) & m;; i = (i + 1) & m)
        {
            if (keys[i] == k) { keys[i] = TOMB; vals[i] = nullptr; count--; tomb++; return; }
            if (keys[i] == EMPTY) return;
        }
    }
#endif
};
static PageMap* g_pageMapPtr = new PageMap();
#define g_pageMap (*g_pageMapPtr)
static uintptr_t g_heapMin = UINTPTR_MAX, g_heapMax = 0;
static std::vector<Page*> g_pages;
static std::vector<Page*> g_freePagesBy[2][kNumClasses];

static std::unordered_map<void*, Finalizer> g_finalizers;
static std::unordered_set<void*> g_pinned;
static std::vector<std::pair<void*, size_t>> g_roots;
static uintptr_t g_stackTop = 0;
static size_t g_allocatedSince = 0;
static size_t g_liveBytes = 0;
static size_t g_heapBytes = 0;
static size_t g_threshold = 48u << 20;
static bool g_collecting = false;
static std::vector<std::pair<uint8_t*, size_t>> g_markStack;

void gc_init(void* stackTop)
{
    g_stackTop = (uintptr_t)stackTop;
}

static int ClassFor(size_t size)
{
    for (int i = 0; i < kNumClasses; i++)
        if (size <= kSizeClasses[i]) return i;
    return -1;
}

static void* AlignedAlloc(size_t size)
{
    void* p = nullptr;
#if defined(__WIIU__)
    p = memalign(PAGE_SIZE, size);
#else
    if (posix_memalign(&p, PAGE_SIZE, size) != 0) p = nullptr;
#endif
    if (!p)
    {
        // try to free memory and retry
        gc_collect();
#if defined(__WIIU__)
        p = memalign(PAGE_SIZE, size);
#else
        if (posix_memalign(&p, PAGE_SIZE, size) != 0) p = nullptr;
#endif
        if (!p) { fprintf(stderr, "GC: out of memory (%zu bytes)\n", size); abort(); }
    }
    return p;
}

static Page* NewPage(int sc, bool atomic)
{
    Page* pg = new Page();
    pg->atomic = atomic;
    pg->base = (uint8_t*)AlignedAlloc(PAGE_SIZE);
    pg->cellSize = kSizeClasses[sc];
    pg->nCells = (uint32_t)(PAGE_SIZE / pg->cellSize);
    pg->nFree = pg->nCells;
    pg->sizeClass = sc;
    pg->allocated.assign(pg->nCells, 0);
    pg->marked.assign(pg->nCells, 0);
    pg->large = false;
    pg->largeSize = 0;
    pg->freeList = nullptr;
    // build free list (in address order)
    for (int i = (int)pg->nCells - 1; i >= 0; i--)
    {
        void* cell = pg->base + (size_t)i * pg->cellSize;
        *(void**)cell = pg->freeList;
        pg->freeList = cell;
    }
    g_pageMap.set((uintptr_t)pg->base >> PAGE_SHIFT, pg); g_heapMin = std::min(g_heapMin, (uintptr_t)pg->base); g_heapMax = std::max(g_heapMax, (uintptr_t)pg->base + PAGE_SIZE);
    g_pages.push_back(pg);
    g_freePagesBy[atomic][sc].push_back(pg);
    pg->inFreePages = true;
    g_heapBytes += PAGE_SIZE;
    return pg;
}

static void* AllocImpl(size_t size, bool atomic);
void* gc_alloc(size_t size) { return AllocImpl(size, false); }
void* gc_alloc_atomic(size_t size) { return AllocImpl(size, true); }
static void* AllocImpl(size_t size, bool atomic)
{
    if (size == 0) size = 1;
    if (!g_collecting && g_allocatedSince > g_threshold) gc_collect();
    int sc = ClassFor(size);
    if (sc < 0)
    {
        size_t total = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        Page* pg = new Page();
        pg->atomic = atomic;
        pg->base = (uint8_t*)AlignedAlloc(total);
        memset(pg->base, 0, total);
        pg->cellSize = 0;
        pg->nCells = 1;
        pg->nFree = 0;
        pg->sizeClass = -1;
        pg->allocated.assign(1, 1);
        pg->marked.assign(1, 0);
        pg->large = true;
        pg->largeSize = total;
        pg->freeList = nullptr;
        pg->inFreePages = false;
        for (size_t off = 0; off < total; off += PAGE_SIZE)
            g_pageMap.set(((uintptr_t)pg->base + off) >> PAGE_SHIFT, pg);
        g_heapMin = std::min(g_heapMin, (uintptr_t)pg->base);
        g_heapMax = std::max(g_heapMax, (uintptr_t)pg->base + total);
        g_pages.push_back(pg);
        g_heapBytes += total;
        g_allocatedSince += total;
        return pg->base;
    }
    auto& fl = g_freePagesBy[atomic][sc];
    Page* pg = nullptr;
    while (!fl.empty())
    {
        pg = fl.back();
        if (pg->nFree > 0) break;
        pg->inFreePages = false;
        fl.pop_back();
        pg = nullptr;
    }
    if (!pg) pg = NewPage(sc, atomic);
    void* cell = pg->freeList;
    pg->freeList = *(void**)cell;
    pg->nFree--;
    size_t idx = ((uint8_t*)cell - pg->base) / pg->cellSize;
    pg->allocated[idx] = 1;
    memset(cell, 0, pg->cellSize);
    g_allocatedSince += pg->cellSize;
    return cell;
}

void gc_set_finalizer(void* p, Finalizer f)
{
    if (f) g_finalizers[p] = f;
    else g_finalizers.erase(p);
}

void gc_pin(void* p) { if (p) g_pinned.insert(p); }
void gc_unpin(void* p) { g_pinned.erase(p); }
void gc_root(void* addr, size_t size) { g_roots.push_back({ addr, size }); }

static inline void MarkCandidate(uintptr_t w)
{
    if (w < g_heapMin || w >= g_heapMax) return;
    Page* pg = g_pageMap.get(w >> PAGE_SHIFT);
    if (!pg) return;
    if (pg->large)
    {
        if (w < (uintptr_t)pg->base || w >= (uintptr_t)pg->base + pg->largeSize) return;
        if (pg->marked[0]) return;
        pg->marked[0] = 1;
        if (!pg->atomic) g_markStack.push_back({ pg->base, pg->largeSize });
        return;
    }
    size_t idx = (w - (uintptr_t)pg->base) / pg->cellSize;
    if (idx >= pg->nCells || !pg->allocated[idx] || pg->marked[idx]) return;
    pg->marked[idx] = 1;
    if (!pg->atomic) g_markStack.push_back({ pg->base + idx * pg->cellSize, pg->cellSize });
}

bool gc_is_heap_pointer(const void* p)
{
    uintptr_t w = (uintptr_t)p;
    Page* pg = g_pageMap.get(w >> PAGE_SHIFT);
    if (!pg) return false;
    if (pg->large) return w >= (uintptr_t)pg->base && w < (uintptr_t)pg->base + pg->largeSize;
    size_t idx = (w - (uintptr_t)pg->base) / pg->cellSize;
    return idx < pg->nCells && pg->allocated[idx];
}

static void ScanRange(const void* from, const void* to)
{
    uintptr_t a = ((uintptr_t)from + sizeof(void*) - 1) & ~(uintptr_t)(sizeof(void*) - 1);
    uintptr_t b = (uintptr_t)to;
    for (; a + sizeof(void*) <= b; a += sizeof(void*))
        MarkCandidate(*(uintptr_t*)a);
}

static void Drain()
{
    while (!g_markStack.empty())
    {
        auto top = g_markStack.back();
        g_markStack.pop_back();
        ScanRange(top.first, top.first + top.second);
    }
}

static void __attribute__((noinline)) ScanStack()
{
    jmp_buf regs;
    setjmp(regs);
    __builtin_unwind_init();
    volatile uintptr_t marker = 0;
    uintptr_t lo = (uintptr_t)&marker;
    ScanRange(&regs, (uint8_t*)&regs + sizeof(regs));
    if (g_stackTop > lo) ScanRange((void*)lo, (void*)g_stackTop);
    Drain();
}

bool g_gc_census = false;
static std::unordered_map<void*, const char*>* g_tags = nullptr;
void gc_enable_census(bool on) { g_gc_census = on; if (on && !g_tags) g_tags = new std::unordered_map<void*, const char*>(); }
void gc_tag(void* p, const char* name) { if (g_tags) (*g_tags)[p] = name; }

static bool IsMarked(void* p)
{
    uintptr_t w = (uintptr_t)p;
    Page* pg = g_pageMap.get(w >> PAGE_SHIFT);
    if (!pg) return false;
    return pg->large ? pg->marked[0] : pg->marked[(w - (uintptr_t)pg->base) / pg->cellSize];
}

static void Census()
{
    if (!g_tags) return;
    std::unordered_map<const char*, std::pair<size_t, size_t>> counts;
    for (auto it = g_tags->begin(); it != g_tags->end();)
    {
        if (!IsMarked(it->first)) { it = g_tags->erase(it); continue; }
        uintptr_t w = (uintptr_t)it->first;
        Page* pg = g_pageMap.get(w >> PAGE_SHIFT);
        auto& c = counts[it->second];
        c.first++;
        c.second += pg->large ? pg->largeSize : pg->cellSize;
        ++it;
    }
    std::vector<std::pair<const char*, std::pair<size_t, size_t>>> v(counts.begin(), counts.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second.second > b.second.second; });
    for (size_t i = 0; i < v.size() && i < 25; i++)
    {
        char b[256];
        snprintf(b, sizeof b, "[census] %8zu KB %7zu x %s", v[i].second.second / 1024, v[i].second.first, v[i].first);
        plat::log(b);
    }
}

void gc_collect()
{
    if (g_collecting) return;
    g_collecting = true;
    int64_t tStart = plat::now_ns();
    for (Page* pg : g_pages) std::fill(pg->marked.begin(), pg->marked.end(), 0);
    for (void* p : g_pinned) MarkCandidate((uintptr_t)p);
    Drain();
    for (auto& r : g_roots) { ScanRange(r.first, (uint8_t*)r.first + r.second); Drain(); }
    ScanStack();
    if (g_gc_census) Census();
    // phase 1: finalizers of unreachable objects
    std::vector<std::pair<void*, Finalizer>> dead;
    for (auto& kv : g_finalizers)
    {
        uintptr_t w = (uintptr_t)kv.first;
        Page* pg = g_pageMap.get(w >> PAGE_SHIFT);
        bool marked = pg->large ? pg->marked[0] : pg->marked[(w - (uintptr_t)pg->base) / pg->cellSize];
        if (!marked) dead.push_back(kv);
    }
    for (auto& d : dead) { g_finalizers.erase(d.first); d.second(d.first); }
    // phase 2: sweep
    size_t live = 0;
    std::vector<Page*> keep;
    keep.reserve(g_pages.size());
    for (Page* pg : g_pages)
    {
        if (pg->large)
        {
            if (pg->marked[0]) { live += pg->largeSize; keep.push_back(pg); continue; }
            for (size_t off = 0; off < pg->largeSize; off += PAGE_SIZE)
                g_pageMap.erase(((uintptr_t)pg->base + off) >> PAGE_SHIFT);
            g_heapBytes -= pg->largeSize;
            free(pg->base);
            delete pg;
            continue;
        }
        for (uint32_t i = 0; i < pg->nCells; i++)
        {
            if (!pg->allocated[i]) continue;
            if (pg->marked[i]) { live += pg->cellSize; continue; }
            pg->allocated[i] = 0;
            void* cell = pg->base + (size_t)i * pg->cellSize;
            *(void**)cell = pg->freeList;
            pg->freeList = cell;
            pg->nFree++;
        }
        if (pg->nFree == pg->nCells && g_heapBytes > (64u << 20))
        {
            // release completely empty pages when the heap is large
            g_pageMap.erase((uintptr_t)pg->base >> PAGE_SHIFT);
            auto& fl = g_freePagesBy[pg->atomic][pg->sizeClass];
            fl.erase(std::remove(fl.begin(), fl.end(), pg), fl.end());
            g_heapBytes -= PAGE_SIZE;
            free(pg->base);
            delete pg;
            continue;
        }
        if (pg->nFree > 0 && !pg->inFreePages) { g_freePagesBy[pg->atomic][pg->sizeClass].push_back(pg); pg->inFreePages = true; }
        keep.push_back(pg);
    }
    g_pages.swap(keep);
    g_liveBytes = live;
    g_allocatedSince = 0;
    g_threshold = std::max<size_t>(48u << 20, live);
    g_collecting = false;
    {
        char b[128];
        snprintf(b, sizeof b, "[gc] %.1f ms, live %zu KB, heap %zu KB, range %p-%p", (plat::now_ns() - tStart) / 1e6, live / 1024, g_heapBytes / 1024, (void*)g_heapMin, (void*)g_heapMax);
        plat::log(b);
    }
}

void gc_maybe_collect()
{
    if (g_allocatedSince > g_threshold) gc_collect();
}

size_t gc_heap_bytes() { return g_heapBytes; }
size_t gc_live_bytes() { return g_liveBytes + g_allocatedSince; }

} // namespace sys
