// Conservative mark & sweep garbage collector.
// Roots: the main thread stack (scanned conservatively), explicitly registered
// root ranges (static fields), and pinned objects. All GC memory is scanned
// conservatively, so any GC pointer stored inside GC memory keeps its target alive.
// GC pointers must never be stored only in non-GC memory (malloc / std containers)
// unless that memory is registered as a root or the target is pinned.
#pragma once
#include <cstddef>
#include <cstdint>

namespace sys {

typedef void (*Finalizer)(void* obj);

void gc_init(void* stackTop);
void* gc_alloc(size_t size);                 // zeroed memory
void* gc_alloc_atomic(size_t size);          // zeroed memory that never contains GC pointers (not scanned)
void gc_set_finalizer(void* p, Finalizer f); // called before the memory is reclaimed
void gc_pin(void* p);                        // never collect p (and keep its referents alive)
void gc_unpin(void* p);
void gc_root(void* addr, size_t size);       // register a root memory range
void gc_collect();
void gc_maybe_collect();
size_t gc_heap_bytes();
size_t gc_live_bytes();
bool gc_is_heap_pointer(const void* p);
// diagnostics: when enabled, live objects are counted by tag after each collection
extern bool g_gc_census;
void gc_tag(void* p, const char* name);
void gc_enable_census(bool on);

} // namespace sys
