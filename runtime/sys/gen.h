// C# iterator methods (yield return) implemented with C++20 coroutines.
// Coroutine frames are allocated from the GC heap and never explicitly destroyed;
// the GC reclaims them once the enumerator becomes unreachable.
#pragma once
#include "collections.h"
#include <coroutine>
#include <exception>

namespace sys {

template<class T> struct Gen
{
    struct promise_type
    {
        T current{};
        static void* operator new(size_t n) { void* p = gc_alloc(n); if (g_gc_census) gc_tag(p, "coroutine frame"); return p; }
        static void operator delete(void*) {}
        Gen get_return_object() { return Gen{ std::coroutine_handle<promise_type>::from_promise(*this) }; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        template<class U> std::suspend_always yield_value(U&& v) { current = static_cast<T>(std::forward<U>(v)); return {}; }
        void return_void() {}
        void unhandled_exception() { throw; }
    };
    std::coroutine_handle<promise_type> h;
};

template<class T> struct CoroEnumerator final : ::System::Object, ::System::Collections::IEnumerator
{
    std::coroutine_handle<typename Gen<T>::promise_type> h;
    bool done = false;
    explicit CoroEnumerator(Gen<T> g) : h(g.h) {}
    bool MoveNext() override
    {
        if (done) return false;
        h.resume();
        if (h.done()) { done = true; return false; }
        return true;
    }
    ::System::Object* get_Current() override
    {
        if constexpr (std::is_same_v<T, ::System::Object*>) return h.promise().current;
        else return box_any<T>(h.promise().current);
    }
    T get_Current_T() { return h.promise().current; }
    void Reset() override {}
    void Dispose() {}
};

// IEnumerator_<T>::get_Current must return T; we route through a thin adapter
template<class T> struct CoroEnumeratorT final : ::System::Object, ::System::Collections::Generic::IEnumerator_<T>
{
    CoroEnumerator<T>* e;
    explicit CoroEnumeratorT(CoroEnumerator<T>* x) : e(x) {}
    bool MoveNext() override { return e->MoveNext(); }
    T get_Current() override { return e->get_Current_T(); }
};

inline ::System::Collections::IEnumerator* MakeEnumerator(Gen<::System::Object*> g)
{
    return gcnew<CoroEnumerator<::System::Object*>>(g);
}

template<class T> struct CoroEnumerable final : ::System::Object, ::System::Collections::Generic::IEnumerable<T>, ::System::Collections::IEnumerable
{
    Fn<Gen<T>()>* factory;
    explicit CoroEnumerable(Fn<Gen<T>()>* f) : factory(f) {}
    ::System::Collections::Generic::IEnumerator_<T>* GetEnumerator__I() override
    {
        return gcnew<CoroEnumeratorT<T>>(gcnew<CoroEnumerator<T>>(factory->Invoke()));
    }
    ::System::Collections::IEnumerator* GetEnumerator() override { return gcnew<CoroEnumerator<T>>(factory->Invoke()); }
};

template<class T> CoroEnumerable<T>* MakeEnumerable(Fn<Gen<T>()>* f) { return gcnew<CoroEnumerable<T>>(f); }

} // namespace sys
