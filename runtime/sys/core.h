// Core runtime: Object, String, boxing, arrays, casts, delegates, enums.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <new>
#include <utility>
#include <type_traits>
#include <typeinfo>
#include <typeindex>
#include <initializer_list>
#include <string>
#include <vector>
#include <limits>
#include "gc.h"

namespace System {
struct Object;
struct String;
struct Type;
struct Interface;
struct Exception;
template<class T> struct Array;
struct ArrayBase;
namespace Collections { namespace Generic { template<class T> struct IEnumerable; } }
}

namespace sys {

// ---------------------------------------------------------------- allocation
template<class T> struct NeedsFinalizer : std::false_type {};
struct Finalizable {}; // derive from this to get the destructor run when collected

template<class T> void destroy_obj(void* p) { static_cast<T*>(p)->~T(); }

template<class T, class... A>
T* gcnew(A&&... args)
{
    void* mem = gc_alloc(sizeof(T));
    if (g_gc_census) gc_tag(mem, typeid(T).name());
    T* obj = new (mem) T(std::forward<A>(args)...);
    if constexpr (std::is_base_of_v<Finalizable, T> || NeedsFinalizer<T>::value)
        gc_set_finalizer(mem, &destroy_obj<T>);
    return obj;
}

// Types whose values never hold GC pointers: arrays of them are allocated "atomic" (not scanned).
// Important on 32-bit targets, where random data (audio, pixels, text) often looks like heap addresses.
template<class T> struct pointer_free : std::integral_constant<bool, std::is_arithmetic_v<T> || std::is_enum_v<T>> {};

// Ref box for captured & mutated locals
template<class T> struct Ref { T v{}; Ref() {} Ref(const T& x) : v(x) {} };

// finally helper
template<class F> struct Finally
{
    F f; bool active = true;
    Finally(F&& fn) : f(std::move(fn)) {}
    Finally(Finally&& o) : f(std::move(o.f)) { o.active = false; }
    ~Finally() noexcept(false) { if (active) f(); }
};
template<class F> Finally<F> finally(F&& f) { return Finally<F>(std::forward<F>(f)); }

inline float nanf() { return std::numeric_limits<float>::quiet_NaN(); }
inline float inff() { return std::numeric_limits<float>::infinity(); }
inline double nan() { return std::numeric_limits<double>::quiet_NaN(); }
inline double inf() { return std::numeric_limits<double>::infinity(); }
inline float fmod(float a, float b) { return std::fmod(a, b); }
inline double fmod(double a, double b) { return std::fmod(a, b); }
inline double fmod(double a, float b) { return std::fmod(a, (double)b); }
inline double fmod(float a, double b) { return std::fmod((double)a, b); }
inline float fmod(int32_t a, float b) { return std::fmod((float)a, b); }
inline float fmod(float a, int32_t b) { return std::fmod(a, (float)b); }

template<class T> T& discard() { static T d{}; return d; }

[[noreturn]] void throw_null();
template<class T> inline T* nn(T* p) { if (__builtin_expect(p == nullptr, 0)) throw_null(); return p; }
[[noreturn]] void throw_invalid_cast(const char* to);
[[noreturn]] void throw_index();
[[noreturn]] void throw_switch();
[[noreturn]] void throw_msg(const char* type, const char* msg);
template<class T, class E> [[noreturn]] T throw_(E e) { throw e; }
struct ThrowAny { template<class T> operator T() const { throw_switch(); } };
template<class E> ThrowAny throw_any(E e) { throw e; return {}; }

// enum metadata
struct EnumEntry { int64_t value; const char16_t* name; };
struct EnumMeta
{
    const char* typeName;
    bool flags;
    std::vector<EnumEntry> entries;
};
template<class T> struct EnumInfo { static constexpr bool defined = false; };
inline void enum_meta_add(EnumMeta&) {}
template<class E, class... R> void enum_meta_add(EnumMeta& m, E v, const char16_t* n, R... rest)
{
    m.entries.push_back({ (int64_t)v, n });
    enum_meta_add(m, rest...);
}
template<class T, class... R> EnumMeta make_enum_meta(const char* name, bool flags, R... rest)
{
    EnumMeta m{ name, flags, {} };
    enum_meta_add(m, rest...);
    return m;
}
::System::String* enum_to_string(const EnumMeta& m, int64_t v);
bool enum_parse(const EnumMeta& m, ::System::String* s, int64_t& out, bool ignoreCase = false);

} // namespace sys

#define SYS_ENUM_INFO(T, F, ...) \
    template<> struct sys::EnumInfo<T> { static constexpr bool defined = true; \
        static const ::sys::EnumMeta& get() { static ::sys::EnumMeta m = ::sys::make_enum_meta<T>(#T, F, ##__VA_ARGS__); return m; } };

namespace System {

struct Object
{
    virtual ~Object() {}
    virtual String* ToString();
    virtual bool Equals(Object* o) { return o == this; }
    virtual int32_t GetHashCode() { return (int32_t)(((uintptr_t)this >> 3) * 2654435761u); }
};

// Base of all interfaces (no Object base, so classes keep a single Object)
struct Interface
{
    virtual ~Interface() {}
};

struct String final : Object
{
    int32_t length = 0;
    char16_t* data = nullptr;

    static String* Alloc(int32_t len);
    static String* Lit(const char16_t* s, size_t len);
    static String* FromUtf8(const char* s, size_t len = (size_t)-1);
    static String* FromU16(const char16_t* s, size_t len);
    static String* FromStd(const std::u16string& s) { return FromU16(s.data(), s.size()); }
    std::string ToUtf8() const;
    std::u16string ToU16() const { return std::u16string(data, length); }

    String* ToString() override { return this; }
    bool Equals(Object* o) override;
    int32_t GetHashCode() override;

    // C# API
    int32_t get_Length() const { return length; }
    char16_t get_Chars(int32_t i) const { if ((uint32_t)i >= (uint32_t)length) ::sys::throw_index(); return data[i]; }
    char16_t get_Item(int32_t i) const { return get_Chars(i); }
    static String* Empty();
    static bool op_Equality(String* a, String* b);
    static bool op_Inequality(String* a, String* b) { return !op_Equality(a, b); }
    static bool IsNullOrEmpty(String* s) { return !s || s->length == 0; }
    static String* Concat(String* a, String* b);
    static String* Concat(Array<Object*>* parts);
    static String* Copy(String* s) { return s ? FromU16(s->data, s->length) : nullptr; }
    static String* Join(String* sep, Array<String*>* parts);
    template<class T> static String* Join(String* sep, Collections::Generic::IEnumerable<T>* enumerable);
    bool Equals(String* o) { return op_Equality(this, o); }
    int32_t CompareTo(String* o);
    bool Contains(String* s) { return IndexOf(s) >= 0; }
    bool StartsWith(String* s);
    bool EndsWith(String* s);
    int32_t IndexOf(char16_t c) { return IndexOf(c, 0); }
    int32_t IndexOf(char16_t c, int32_t start);
    int32_t IndexOf(String* s);
    int32_t IndexOfAny(Array<char16_t>* chars);
    int32_t LastIndexOf(char16_t c);
    String* Insert(int32_t i, String* s);
    String* Remove(int32_t start);
    String* Remove(int32_t start, int32_t count);
    String* Replace(char16_t a, char16_t b);
    String* Replace(String* a, String* b);
    Array<String*>* Split(Array<char16_t>* seps);
    Array<String*>* Split(Array<char16_t>* seps, int32_t options);
    template<class O> Array<String*>* Split(Array<char16_t>* seps, O options) { return Split(seps, (int32_t)options); }
    String* Substring(int32_t start);
    String* Substring(int32_t start, int32_t len);
    Array<char16_t>* ToCharArray();
    String* ToLower();
    String* ToUpper();
    template<class C> String* ToLower(C*) { return ToLower(); }
    template<class C> String* ToUpper(C*) { return ToUpper(); }
    String* ToLowerInvariant() { return ToLower(); }
    String* ToUpperInvariant() { return ToUpper(); }
    String* Trim();
    String* TrimStart();
    String* TrimEnd();
    String* PadLeft(int32_t n) { return PadLeft(n, u' '); }
    String* PadLeft(int32_t n, char16_t c);
    String* PadRight(int32_t n) { return PadRight(n, u' '); }
    String* PadRight(int32_t n, char16_t c);
    const char16_t* begin() const { return data; }
    const char16_t* end() const { return data + length; }
};

struct ArrayBase : Object
{
    int32_t length = 0;
    int32_t get_Length() const { return length; }
};

} // namespace System

#define TF_S(x) ([]() -> ::System::String* { static ::System::String* __s = ::System::String::Lit(x, sizeof(x) / sizeof(char16_t) - 1); return __s; }())

namespace sys {

// ---------------------------------------------------------------- boxing
template<class T> struct Boxed final : ::System::Object
{
    T value;
    explicit Boxed(const T& v) : value(v) {}
    ::System::String* ToString() override;
    bool Equals(::System::Object* o) override;
    int32_t GetHashCode() override;
};

template<class T> struct is_pointer_like : std::is_pointer<T> {};

template<class T> ::System::Object* box(const T& v);

template<class T> ::System::Object* to_object(T* p)
{
    if constexpr (std::is_base_of_v<::System::Object, T>) return p;
    else if constexpr (std::is_polymorphic_v<T>) return p ? dynamic_cast<::System::Object*>(p) : nullptr;
    else return (::System::Object*)p;
}
inline ::System::Object* to_object(std::nullptr_t) { return nullptr; }

// dynamic cast that works for classes and interfaces
template<class T, class U> T dyn(U* p)
{
    if (!p) return nullptr;
    using TT = std::remove_pointer_t<T>;
    if constexpr (std::is_convertible_v<U*, T>) return static_cast<T>(p);
    else if constexpr (std::is_polymorphic_v<U>) return dynamic_cast<T>(p);
    else return nullptr;
}

template<class T, class U> bool is(U* p)
{
    if (!p) return false;
    if constexpr (std::is_pointer_v<T>) return dyn<T>(p) != nullptr;
    else return dynamic_cast<Boxed<T>*>(to_object(p)) != nullptr;
}
template<class T, class U> bool is(const U& v)
{
    if constexpr (std::is_pointer_v<T>) return false;
    else return std::is_same_v<T, U>;
}

template<class T, class U> T as(U* p)
{
    if constexpr (std::is_pointer_v<T>) return dyn<T>(p);
    else { static_assert(sizeof(T) == 0, "as<T> needs pointer"); }
}

template<class T, class U> T cast(U* p)
{
    if (!p) return nullptr;
    T r = dyn<T>(p);
    if (!r) throw_invalid_cast(typeid(T).name());
    return r;
}
template<class T> T cast(std::nullptr_t) { return nullptr; }

template<class T, class U> T upcast(U* p)
{
    if constexpr (std::is_convertible_v<U*, T>) return static_cast<T>(p);
    else return dyn<T>(p);
}
template<class T, class U> T upcast(const U& v)
{
    if constexpr (std::is_pointer_v<T> && !std::is_pointer_v<U>) return static_cast<T>(box(v));
    else return static_cast<T>(v);
}

template<class T, class U, class V> bool is_as(U* p, V& out)
{
    if constexpr (std::is_pointer_v<T>)
    {
        T r = dyn<T>(p);
        if (!r) return false;
        out = r;
        return true;
    }
    else
    {
        auto* b = p ? dynamic_cast<Boxed<T>*>(to_object(p)) : nullptr;
        if (!b) return false;
        out = b->value;
        return true;
    }
}
template<class T, class U, class V> bool is_as(const U& v, V& out)
{
    if constexpr (std::is_same_v<T, U>) { out = v; return true; }
    else return false;
}

template<class T> T unbox(::System::Object* o);

// pointer -> object passthrough, value -> box
template<class T> ::System::Object* box_any(const T& v)
{
    if constexpr (std::is_pointer_v<T>) return to_object(v);
    else return box<T>(v);
}
template<class T> T unbox_any(::System::Object* o)
{
    if constexpr (std::is_pointer_v<T>) return cast<T>(o);
    else return unbox<T>(o);
}

// ---------------------------------------------------------------- ToString & friends
::System::String* ToString(int32_t v);
::System::String* ToString(uint32_t v);
::System::String* ToString(int64_t v);
::System::String* ToString(uint64_t v);
inline ::System::String* ToString(int16_t v) { return ToString((int32_t)v); }
inline ::System::String* ToString(uint16_t v) { return ToString((int32_t)v); }
inline ::System::String* ToString(uint8_t v) { return ToString((int32_t)v); }
inline ::System::String* ToString(int8_t v) { return ToString((int32_t)v); }
::System::String* ToString(float v);
::System::String* ToString(double v);
::System::String* ToString(bool v);
::System::String* ToString(char16_t v);
::System::String* ToString(::System::String* v);
::System::String* ToString(int32_t v, ::System::String* fmt);
::System::String* ToString(int64_t v, ::System::String* fmt);
::System::String* ToString(uint64_t v, ::System::String* fmt);
::System::String* ToString(float v, ::System::String* fmt);
::System::String* ToString(double v, ::System::String* fmt);
template<class C> ::System::String* ToString(float v, ::System::String* fmt, C*) { return ToString(v, fmt); }
template<class C> ::System::String* ToString(double v, ::System::String* fmt, C*) { return ToString(v, fmt); }
template<class C> ::System::String* ToString(int32_t v, ::System::String* fmt, C*) { return ToString(v, fmt); }
template<class C, std::enable_if_t<std::is_class_v<C>, int> = 0> ::System::String* ToString(float v, C*) { return ToString(v); }
template<class C, std::enable_if_t<std::is_class_v<C>, int> = 0> ::System::String* ToString(int32_t v, C*) { return ToString(v); }
template<class C, std::enable_if_t<std::is_class_v<C>, int> = 0> ::System::String* ToString(double v, C*) { return ToString(v); }

template<class T> ::System::String* ToString(T* p)
{
    if (!p) throw_null();
    return to_object(p)->ToString();
}
template<class T, std::enable_if_t<std::is_enum_v<T>, int> = 0> ::System::String* ToString(T v)
{
    if constexpr (EnumInfo<T>::defined) return enum_to_string(EnumInfo<T>::get(), (int64_t)v);
    else return ToString((int64_t)v);
}
::System::String* type_name_string(const char* mangled);
template<class T, std::enable_if_t<std::is_class_v<T>, int> = 0> ::System::String* ToString(const T& v)
{
    if constexpr (requires(T& x) { x.ToString(); }) return const_cast<T&>(v).ToString();
    else return type_name_string(typeid(T).name());
}

// "safe" ToString used by string concatenation (null -> "")
inline ::System::String* ToStr(::System::String* s) { return s ? s : ::System::String::Empty(); }
template<class T> ::System::String* ToStr(T* p) { return p ? to_object(p)->ToString() : ::System::String::Empty(); }
inline ::System::String* ToStr(std::nullptr_t) { return ::System::String::Empty(); }
template<class T, std::enable_if_t<!std::is_pointer_v<T>, int> = 0> ::System::String* ToStr(const T& v) { return ToString(v); }

template<class A, class B> ::System::String* concat(const A& a, const B& b)
{
    return ::System::String::Concat(ToStr(a), ToStr(b));
}
template<class... A> ::System::String* concat_all(const A&... a)
{
    ::System::String* r = ::System::String::Empty();
    ((r = ::System::String::Concat(r, ToStr(a))), ...);
    return r;
}

// Equals / GetHashCode
template<class T> bool Equals(T* a, ::System::Object* b) { if (!a) throw_null(); return to_object(a)->Equals(b); }
inline bool Equals(::System::String* a, ::System::String* b) { if (!a) throw_null(); return ::System::String::op_Equality(a, b); }
template<class T, class U, std::enable_if_t<!std::is_pointer_v<T>, int> = 0> bool Equals(const T& a, const U& b)
{
    if constexpr (std::is_pointer_v<U>)
    {
        auto* bx = b ? dynamic_cast<Boxed<T>*>(to_object(b)) : nullptr;
        if (!bx) return false;
        return a == bx->value;
    }
    else return a == b;
}
template<class T> int32_t GetHashCode(T* a) { if (!a) throw_null(); return to_object(a)->GetHashCode(); }
int32_t hash_float(float f);
int32_t hash_double(double d);
template<class T, std::enable_if_t<!std::is_pointer_v<T>, int> = 0> int32_t GetHashCode(const T& v)
{
    if constexpr (std::is_same_v<T, float>) return hash_float(v);
    else if constexpr (std::is_same_v<T, double>) return hash_double(v);
    else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) return (int32_t)(int64_t)v ^ (int32_t)((int64_t)v >> 32);
    else if constexpr (requires(T& x) { x.GetHashCode(); }) return const_cast<T&>(v).GetHashCode();
    else
    {
        uint32_t h = 2166136261u; const uint8_t* p = (const uint8_t*)&v;
        for (size_t i = 0; i < sizeof(T); i++) { h ^= p[i]; h *= 16777619u; }
        return (int32_t)h;
    }
}
template<class T> ::System::Type* GetType(T* p);

// CompareTo
template<class T, std::enable_if_t<!std::is_pointer_v<T>, int> = 0> int32_t CompareTo(const T& a, const T& b)
{
    if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) return a < b ? -1 : (b < a ? 1 : 0);
    else return const_cast<T&>(a).CompareTo(b);
}
template<class T, class U, std::enable_if_t<!std::is_pointer_v<T> && !std::is_same_v<T, U>, int> = 0> int32_t CompareTo(const T& a, const U& b)
{
    if constexpr (std::is_pointer_v<U>) return CompareTo(a, unbox<T>(to_object(b)));
    else return CompareTo(a, (T)b);
}
inline int32_t CompareTo(::System::String* a, ::System::String* b) { return a->CompareTo(b); }
template<class T> int32_t CompareTo(T* a, T* b) { return a->CompareTo(b); }

// generic equality/hash used by collections
template<class T> inline bool eq(const T& a, const T& b)
{
    if constexpr (std::is_same_v<T, ::System::String*>) return ::System::String::op_Equality(a, b);
    else if constexpr (std::is_pointer_v<T>)
    {
        if (a == b) return true;
        if (!a || !b) return false;
        if constexpr (std::is_base_of_v<::System::Object, std::remove_pointer_t<T>>) return a->Equals(b);
        else return to_object(a)->Equals(to_object(b));
    }
    else if constexpr (std::is_class_v<T>)
    {
        if constexpr (requires { a == b; }) return a == b;
        else return memcmp(&a, &b, sizeof(T)) == 0;
    }
    else return a == b;
}
template<class T> inline int32_t hash(const T& v)
{
    if constexpr (std::is_pointer_v<T>) { if (!v) return 0; return to_object(v)->GetHashCode(); }
    else return GetHashCode(v);
}

// allocate + run the C#-style constructor (__ctor) when the type has one
template<class U> U* gcnew_ctor()
{
    U* p = gcnew<U>();
    if constexpr (requires(U* x) { x->__ctor(); }) p->__ctor();
    return p;
}
template<class T> T New()
{
    if constexpr (std::is_pointer_v<T>) return gcnew_ctor<std::remove_pointer_t<T>>();
    else return T{};
}

} // namespace sys

namespace System { namespace Collections {
struct IEnumerator : Interface
{
    virtual bool MoveNext() = 0;
    virtual Object* get_Current() = 0;
    virtual void Reset() {}
};
struct IEnumerable : Interface
{
    virtual IEnumerator* GetEnumerator() = 0;
};
namespace Generic {
template<class T> struct IEnumerator_ : Interface
{
    virtual bool MoveNext() = 0;
    virtual T get_Current() = 0;
};
template<class T> struct IEnumerable : Interface
{
    virtual IEnumerator_<T>* GetEnumerator__I() = 0;
};
template<class T> struct ArrayEnumObj;
}}}

namespace System {

template<class T> struct Nullable
{
    T value{};
    bool hasValue = false;
    Nullable() {}
    Nullable(const T& v) : value(v), hasValue(true) {}
    Nullable(std::nullptr_t) {}
    bool get_HasValue() const { return hasValue; }
    T get_Value() const { if (!hasValue) ::sys::throw_msg("InvalidOperationException", "Nullable object must have a value."); return value; }
    T GetValueOrDefault() const { return hasValue ? value : T{}; }
    T GetValueOrDefault(const T& d) const { return hasValue ? value : d; }
    friend bool operator==(const Nullable& a, const Nullable& b) { return a.hasValue == b.hasValue && (!a.hasValue || a.value == b.value); }
    friend bool operator!=(const Nullable& a, const Nullable& b) { return !(a == b); }
    friend bool operator==(const Nullable& a, const T& b) { return a.hasValue && a.value == b; }
    friend bool operator!=(const Nullable& a, const T& b) { return !(a == b); }
    friend bool operator<(const Nullable& a, const Nullable& b) { return a.hasValue && b.hasValue && a.value < b.value; }
    friend bool operator>(const Nullable& a, const Nullable& b) { return a.hasValue && b.hasValue && a.value > b.value; }
    friend bool operator<=(const Nullable& a, const Nullable& b) { return a.hasValue && b.hasValue && a.value <= b.value; }
    friend bool operator>=(const Nullable& a, const Nullable& b) { return a.hasValue && b.hasValue && a.value >= b.value; }
    friend Nullable operator+(const Nullable& a, const Nullable& b) { if (a.hasValue && b.hasValue) return Nullable(a.value + b.value); return Nullable(); }
    friend Nullable operator-(const Nullable& a, const Nullable& b) { if (a.hasValue && b.hasValue) return Nullable(a.value - b.value); return Nullable(); }
    friend Nullable operator*(const Nullable& a, const Nullable& b) { if (a.hasValue && b.hasValue) return Nullable(a.value * b.value); return Nullable(); }
    String* ToString() { return hasValue ? ::sys::ToString(value) : String::Empty(); }
    int32_t GetHashCode() { return hasValue ? ::sys::GetHashCode(value) : 0; }
};

template<class T> struct Array final : ArrayBase, Collections::Generic::IEnumerable<T>
{
    Collections::Generic::IEnumerator_<T>* GetEnumerator__I() override;
    T* data = nullptr;
    // elements follow the object in the same allocation
    static Array* New(int32_t n)
    {
        if (n < 0) ::sys::throw_msg("OverflowException", "negative array size");
        size_t hdr = (sizeof(Array) + alignof(T) - 1) & ~(alignof(T) - 1);
        void* mem = ::sys::pointer_free<T>::value ? ::sys::gc_alloc_atomic(hdr + sizeof(T) * (size_t)n + 1) : ::sys::gc_alloc(hdr + sizeof(T) * (size_t)n + 1);
        if (::sys::g_gc_census) ::sys::gc_tag(mem, typeid(Array).name());
        Array* a = new (mem) Array();
        a->length = n;
        a->data = reinterpret_cast<T*>((uint8_t*)mem + hdr);
        for (int32_t i = 0; i < n; i++) new (&a->data[i]) T();
        return a;
    }
    static Array* Of(std::initializer_list<T> items)
    {
        Array* a = New((int32_t)items.size());
        int32_t i = 0;
        for (auto& x : items) a->data[i++] = x;
        return a;
    }
    T& operator[](int32_t i) { if ((uint32_t)i >= (uint32_t)length) ::sys::throw_index(); return data[i]; }
    const T& operator[](int32_t i) const { if ((uint32_t)i >= (uint32_t)length) ::sys::throw_index(); return data[i]; }
    T get_Item(int32_t i) { return (*this)[i]; }
    void set_Item(int32_t i, const T& v) { (*this)[i] = v; }
    T* begin() { return data; }
    T* end() { return data + length; }
    Array* Clone() { Array* a = New(length); for (int32_t i = 0; i < length; i++) a->data[i] = data[i]; return a; }
    int32_t GetLength(int32_t d) { return length; }
    int32_t get_Rank() { return 1; }
};

template<class T> struct Array2 final : ArrayBase
{
    T* data = nullptr;
    int32_t d0 = 0, d1 = 0;
    static Array2* New(int32_t a, int32_t b)
    {
        size_t hdr = (sizeof(Array2) + alignof(T) - 1) & ~(alignof(T) - 1);
        size_t bytes = hdr + sizeof(T) * (size_t)a * (size_t)b + 1;
        void* mem = ::sys::pointer_free<T>::value ? ::sys::gc_alloc_atomic(bytes) : ::sys::gc_alloc(bytes);
        Array2* r = new (mem) Array2();
        r->d0 = a; r->d1 = b; r->length = a * b;
        r->data = reinterpret_cast<T*>((uint8_t*)mem + hdr);
        for (int32_t i = 0; i < a * b; i++) new (&r->data[i]) T();
        return r;
    }
    static Array2* Of2(std::initializer_list<std::initializer_list<T>> rows)
    {
        int32_t a = (int32_t)rows.size();
        int32_t b = a ? (int32_t)rows.begin()->size() : 0;
        Array2* r = New(a, b);
        int32_t i = 0;
        for (auto& row : rows) { int32_t j = 0; for (auto& x : row) r->at(i, j++) = x; i++; }
        return r;
    }
    T& at(int32_t i, int32_t j)
    {
        if ((uint32_t)i >= (uint32_t)d0 || (uint32_t)j >= (uint32_t)d1) ::sys::throw_index();
        return data[i * d1 + j];
    }
    int32_t GetLength(int32_t d) { return d == 0 ? d0 : d1; }
    T* begin() { return data; }
    T* end() { return data + length; }
    Array2* Clone() { Array2* r = New(d0, d1); for (int32_t i = 0; i < length; i++) r->data[i] = data[i]; return r; }
    int32_t get_Rank() { return 2; }
};

} // namespace System

namespace System { namespace Collections { namespace Generic {
template<class T> struct ArrayEnumObj final : Object, IEnumerator_<T>
{
    ::System::Array<T>* arr; int32_t index = -1;
    explicit ArrayEnumObj(::System::Array<T>* a) : arr(a) {}
    bool MoveNext() override { index++; return index < arr->length; }
    T get_Current() override { return arr->data[index]; }
};
}}
template<class T> Collections::Generic::IEnumerator_<T>* Array<T>::GetEnumerator__I() { return ::sys::gcnew<Collections::Generic::ArrayEnumObj<T>>(this); }
}

namespace sys {

template<class To, class From> To array_cast(From* a)
{
    // covariant array conversion (e.g. Player[] -> Entity[]): reinterpret, element pointers stay valid
    return reinterpret_cast<To>(a);
}

// ---------------------------------------------------------------- delegates
struct Delegate : ::System::Object
{
    ::System::Array<Delegate*>* invocationList = nullptr;
    static Delegate* Combine(Delegate* a, Delegate* b);
    static Delegate* Remove(Delegate* a, Delegate* b);
    static Delegate* CreateDelegate(::System::Type* t, ::System::Object* target, ::System::String* method);
    virtual bool SameAs(Delegate* o) { return o == this; }
    virtual Delegate* CloneMulticast() { return gcnew<Delegate>(); }
    virtual ::System::Object* get_Target() { return nullptr; }
    virtual ::System::Object* DynamicInvoke0() { return nullptr; }
};

template<class Sig> struct Fn;
template<class R, class... A> struct Fn<R(A...)> : Delegate
{
    virtual R Call(A... a) { return R(); }
    Delegate* CloneMulticast() override { return gcnew<Fn>(); }
    R Invoke(A... a)
    {
        if (invocationList)
        {
            auto* l = invocationList;
            for (int32_t i = 0; i < l->length - 1; i++) static_cast<Fn*>(l->data[i])->Call(a...);
            return static_cast<Fn*>(l->data[l->length - 1])->Call(a...);
        }
        return Call(a...);
    }
    R operator()(A... a) { return Invoke(a...); }
    ::System::Object* DynamicInvoke0() override
    {
        if constexpr (sizeof...(A) == 0)
        {
            if constexpr (std::is_void_v<R>) { Invoke(); return nullptr; }
            else return box_any<R>(Invoke());
        }
        else return nullptr;
    }
};

template<class Sig, class F> struct FnLambda;
template<class R, class... A, class F> struct FnLambda<R(A...), F> final : Fn<R(A...)>
{
    F f;
    explicit FnLambda(F&& fn) : f(std::move(fn)) {}
    explicit FnLambda(const F& fn) : f(fn) {}
    R Call(A... a) override { return f(a...); }
};

template<class Sig, class F> Fn<Sig>* fn(F&& f)
{
    return gcnew<FnLambda<Sig, std::decay_t<F>>>(std::forward<F>(f));
}

template<class Sig, class T, class MP> struct FnBound;
template<class R, class... A, class T, class MP> struct FnBound<R(A...), T, MP> final : Fn<R(A...)>
{
    T* target; MP mp;
    FnBound(T* t, MP m) : target(t), mp(m) {}
    R Call(A... a) override
    {
        if constexpr (std::is_void_v<R>) (target->*mp)(a...);
        else return (R)(target->*mp)(a...);
    }
    bool SameAs(Delegate* o) override
    {
        auto* b = dynamic_cast<FnBound*>(o);
        return b && b->target == target && b->mp == mp;
    }
    ::System::Object* get_Target() override { return to_object(target); }
};
template<class Sig, class T, class C, class MR, class... MA> Fn<Sig>* fn_bind(T* target, MR (C::*mp)(MA...))
{
    if (!target) throw_null();
    C* c = static_cast<C*>(target);
    return gcnew<FnBound<Sig, C, MR (C::*)(MA...)>>(c, mp);
}

template<class Sig, class FP> struct FnStatic;
template<class R, class... A, class FP> struct FnStatic<R(A...), FP> final : Fn<R(A...)>
{
    FP fp;
    explicit FnStatic(FP f) : fp(f) {}
    R Call(A... a) override
    {
        if constexpr (std::is_void_v<R>) fp(a...);
        else return (R)fp(a...);
    }
    bool SameAs(Delegate* o) override { auto* b = dynamic_cast<FnStatic*>(o); return b && b->fp == fp; }
};
template<class Sig, class FP> Fn<Sig>* fn_static(FP fp) { return gcnew<FnStatic<Sig, FP>>(fp); }

inline Delegate* DelegateCombineRaw(Delegate* a, Delegate* b) { return Delegate::Combine(a, b); }
inline Delegate* DelegateRemoveRaw(Delegate* a, Delegate* b) { return Delegate::Remove(a, b); }
template<class D, class E> D* Delegate_Combine(D* a, E* b) { return static_cast<D*>(DelegateCombineRaw(a, b)); }
template<class D, class E> D* Delegate_Remove(D* a, E* b) { return static_cast<D*>(DelegateRemoveRaw(a, b)); }
template<class D> D* Delegate_Combine(D* a, std::nullptr_t) { return a; }

} // namespace sys

