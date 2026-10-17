// System namespace subset: Type/reflection, exceptions, math, time, text, misc.
#pragma once
#include "gen.h"
#include <unordered_map>

namespace System {

struct IDisposable : Interface { virtual void Dispose() = 0; };
struct IFormatProvider : Interface {};
struct EventArgs : Object { static EventArgs* get_Empty(); };
struct Attribute : Object {};
struct ICloneable : Interface {};
template<class T> struct IComparable : Interface {};

enum class MidpointRounding : int32_t { ToEven = 0, AwayFromZero = 1 };
enum class StringSplitOptions : int32_t { None = 0, RemoveEmptyEntries = 1 };
enum class StringComparison : int32_t { CurrentCulture, CurrentCultureIgnoreCase, InvariantCulture, InvariantCultureIgnoreCase, Ordinal, OrdinalIgnoreCase };

namespace Reflection {
enum class BindingFlags : int32_t { Default = 0, IgnoreCase = 1, DeclaredOnly = 2, Instance = 4, Static = 8, Public = 16, NonPublic = 32 };
struct FieldInfo;
struct MethodInfo;
struct PropertyInfo;
}

enum class TypeKind : int32_t { Other, Primitive, String, Enum, Class, Struct, Array, List, Nullable, Delegate };

struct Type final : Object
{
    String* name = nullptr;
    String* fullName = nullptr;
    TypeKind kind = TypeKind::Other;
    std::type_index tid = std::type_index(typeid(void));
    const ::sys::EnumMeta* enumMeta = nullptr;
    Type* (*elementType)() = nullptr;              // arrays, lists, nullable
    Object* (*factory)() = nullptr;               // default constructor
    Type* baseType = nullptr;
    // list / array helpers (for serialization)
    Object* (*newArray)(int32_t n) = nullptr;
    void (*arraySet)(Object* arr, int32_t i, Object* v) = nullptr;
    Object* (*arrayGet)(Object* arr, int32_t i) = nullptr;
    int32_t (*count)(Object* coll) = nullptr;
    void (*listAdd)(Object* list, Object* v) = nullptr;
    Object* (*parse)(String* s) = nullptr;         // primitives & enums
    String* (*format)(Object* boxed) = nullptr;
    ::sys::Delegate* (*makeDelegate)(Object* target, Reflection::MethodInfo* m) = nullptr;
    std::vector<Reflection::FieldInfo*> fields;
    std::vector<Reflection::PropertyInfo*> props;
    std::vector<Reflection::MethodInfo*> methods;
    bool reflected = false;

    String* get_Name() { return name; }
    String* get_FullName() { return fullName; }
    String* ToString() override { return fullName; }
    Type* get_BaseType() { return baseType; }
    bool get_IsEnum() { return kind == TypeKind::Enum; }
    bool get_IsArray() { return kind == TypeKind::Array; }
    static bool op_Equality(Type* a, Type* b) { return a == b; }
    static bool op_Inequality(Type* a, Type* b) { return a != b; }
    Array<Reflection::FieldInfo*>* GetFields();
    Array<Reflection::PropertyInfo*>* GetProperties();
    Reflection::FieldInfo* GetField(String* name);
    Reflection::MethodInfo* GetMethod(String* name);
    Reflection::MethodInfo* GetMethod(String* name, Reflection::BindingFlags flags);

    // registration helpers used by generated reflection code
    void AddField(const char16_t* name, Type* t, bool isStatic, bool isLiteral,
                  Object* (*get)(Object*), void (*set)(Object*, Object*), Array<Object*>* (*attrs)());
    void AddProp(const char16_t* name, Type* t, Object* (*get)(Object*), void (*set)(Object*, Object*));
    void AddMethod(const char16_t* name, Object* (*invoke)(Object*));
};

namespace Reflection {
struct MemberInfo : Object
{
    String* name = nullptr;
    String* get_Name() { return name; }
};
struct FieldInfo final : MemberInfo
{
    Type* type = nullptr;
    bool isStatic = false, isLiteral = false;
    Object* (*getter)(Object*) = nullptr;
    void (*setter)(Object*, Object*) = nullptr;
    Array<Object*>* (*attrs)() = nullptr;
    Array<Object*>* attrCache = nullptr;
    Type* get_FieldType() { return type; }
    bool get_IsLiteral() { return isLiteral; }
    bool get_IsStatic() { return isStatic; }
    Object* GetValue(Object* o) { return getter(o); }
    void SetValue(Object* o, Object* v) { if (setter) setter(o, v); }
    Array<Object*>* GetCustomAttributes(bool inherit);
};
struct PropertyInfo final : MemberInfo
{
    Type* type = nullptr;
    Object* (*getter)(Object*) = nullptr;
    void (*setter)(Object*, Object*) = nullptr;
    Type* get_PropertyType() { return type; }
    Object* GetValue(Object* o, Array<Object*>*) { return getter(o); }
    void SetValue(Object* o, Object* v, Array<Object*>*) { if (setter) setter(o, v); }
};
struct MethodInfo final : MemberInfo
{
    Object* (*invoker)(Object*) = nullptr;
    Object* Invoke(Object* target, Array<Object*>* args) { return invoker(target); }
    static bool op_Equality(MethodInfo* a, MethodInfo* b) { return a == b; }
    static bool op_Inequality(MethodInfo* a, MethodInfo* b) { return a != b; }
};
}

struct Exception : Object
{
    String* message = nullptr;
    Exception* inner = nullptr;
    Exception();
    explicit Exception(String* msg) : message(msg) {}
    Exception(String* msg, Exception* in) : message(msg), inner(in) {}
    virtual String* get_Message() { return message ? message : String::FromUtf8("Exception"); }
    String* ToString() override;
    String* get_StackTrace() { return String::Empty(); }
    Exception* get_InnerException() { return inner; }
};
#define SYS_EXCEPTION(N, B) struct N : B { N() {} explicit N(String* m) : B(m) {} N(String* m, Exception* i) : B(m, i) {} };
SYS_EXCEPTION(SystemException, Exception)
SYS_EXCEPTION(NotImplementedException, SystemException)
SYS_EXCEPTION(NotSupportedException, SystemException)
SYS_EXCEPTION(InvalidOperationException, SystemException)
SYS_EXCEPTION(ArgumentException, SystemException)
SYS_EXCEPTION(ArgumentNullException, ArgumentException)
SYS_EXCEPTION(ArgumentOutOfRangeException, ArgumentException)
SYS_EXCEPTION(IndexOutOfRangeException, SystemException)
SYS_EXCEPTION(InvalidCastException, SystemException)
SYS_EXCEPTION(NullReferenceException, SystemException)
SYS_EXCEPTION(FormatException, SystemException)
SYS_EXCEPTION(OverflowException, SystemException)
SYS_EXCEPTION(KeyNotFoundException, SystemException)
namespace IO {
SYS_EXCEPTION(IOException, SystemException)
SYS_EXCEPTION(FileNotFoundException, IOException)
SYS_EXCEPTION(DirectoryNotFoundException, IOException)
}
namespace Collections { namespace Generic { using KeyNotFoundException = ::System::KeyNotFoundException; } }

// ------------------------------------------------------------- primitives
struct Math
{
    static double Sin(double v) { return std::sin(v); }
    static double Cos(double v) { return std::cos(v); }
    static double Tan(double v) { return std::tan(v); }
    static double Asin(double v) { return std::asin(v); }
    static double Acos(double v) { return std::acos(v); }
    static double Atan(double v) { return std::atan(v); }
    static double Atan2(double y, double x) { return std::atan2(y, x); }
    static double Sqrt(double v) { return std::sqrt(v); }
    static double Pow(double a, double b) { return std::pow(a, b); }
    static double Exp(double v) { return std::exp(v); }
    static double Log(double v) { return std::log(v); }
    static double Log(double v, double b) { return std::log(v) / std::log(b); }
    static double Log10(double v) { return std::log10(v); }
    static double Floor(double v) { return std::floor(v); }
    static double Ceiling(double v) { return std::ceil(v); }
    static double Truncate(double v) { return std::trunc(v); }
    static double Round(double v) { return std::nearbyint(v); }
    static double Round(double v, MidpointRounding m) { return m == MidpointRounding::AwayFromZero ? std::round(v) : std::nearbyint(v); }
    static double Round(double v, int32_t digits) { double p = std::pow(10.0, digits); return std::nearbyint(v * p) / p; }
    static float Abs(float v) { return std::fabs(v); }
    static double Abs(double v) { return std::fabs(v); }
    static int32_t Abs(int32_t v) { return v < 0 ? -v : v; }
    static int64_t Abs(int64_t v) { return v < 0 ? -v : v; }
    static int32_t Sign(float v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
    static int32_t Sign(double v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
    static int32_t Sign(int32_t v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
    template<class T> static T Max(T a, T b) { if constexpr (std::is_floating_point_v<T>) { if (std::isnan(a) || std::isnan(b)) return a + b; } return a > b ? a : b; }
    template<class T> static T Min(T a, T b) { if constexpr (std::is_floating_point_v<T>) { if (std::isnan(a) || std::isnan(b)) return a + b; } return a < b ? a : b; }
};

struct Random : Object
{
    int32_t inext = 0, inextp = 0;
    int32_t SeedArray[56];
    Random();
    explicit Random(int32_t seed);
    void Init(int32_t seed);
    int32_t InternalSample();
    virtual double Sample() { return InternalSample() * (1.0 / 2147483647); }
    double GetSampleForLargeRange();
    int32_t Next() { return InternalSample(); }
    int32_t Next(int32_t max);
    int32_t Next(int32_t min, int32_t max);
    double NextDouble() { return Sample(); }
};

struct Int32 { static int32_t Parse(String* s); template<class C> static int32_t Parse(String* s, C*) { return Parse(s); } static bool TryParse(String* s, int32_t& out); static int32_t CompareTo(int32_t a, int32_t b) { return a < b ? -1 : (a > b ? 1 : 0); } static bool Equals(int32_t a, int32_t b) { return a == b; } };
struct UInt64 { static uint64_t Parse(String* s); };
struct Int64 { static int64_t Parse(String* s); static int32_t CompareTo(int64_t a, int64_t b) { return a < b ? -1 : (a > b ? 1 : 0); } };
struct Single
{
    static float Parse(String* s);
    template<class C> static float Parse(String* s, C*) { return Parse(s); }
    static bool TryParse(String* s, float& out);
    static bool IsNaN(float f) { return std::isnan(f); }
    static bool IsInfinity(float f) { return std::isinf(f); }
    static int32_t CompareTo(float a, float b) { return a < b ? -1 : (a > b ? 1 : (a == b ? 0 : (std::isnan(a) ? (std::isnan(b) ? 0 : -1) : 1))); }
    static bool Equals(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }
};
struct Double { static double Parse(String* s); static bool IsNaN(double f) { return std::isnan(f); } static int32_t CompareTo(double a, double b) { return a < b ? -1 : (a > b ? 1 : 0); } };
struct Boolean { static bool Parse(String* s); };
struct Byte { static uint8_t Parse(String* s) { return (uint8_t)Int32::Parse(s); } };
struct Char
{
    static bool IsUpper(char16_t c) { return c >= u'A' && c <= u'Z'; }
    static bool IsLower(char16_t c) { return c >= u'a' && c <= u'z'; }
    static bool IsDigit(char16_t c) { return c >= u'0' && c <= u'9'; }
    static bool IsLetter(char16_t c) { return IsUpper(c) || IsLower(c) || c > 127; }
    static bool IsLetterOrDigit(char16_t c) { return IsLetter(c) || IsDigit(c); }
    static bool IsWhiteSpace(char16_t c) { return c == u' ' || c == u'\t' || c == u'\n' || c == u'\r' || c == 0xA0; }
    static char16_t ToUpper(char16_t c) { return IsLower(c) ? (char16_t)(c - 32) : c; }
    static char16_t ToLower(char16_t c) { return IsUpper(c) ? (char16_t)(c + 32) : c; }
    template<class C> static char16_t ToUpper(char16_t c, C*) { return ToUpper(c); }
    template<class C> static char16_t ToLower(char16_t c, C*) { return ToLower(c); }
    static String* ToString(char16_t c) { return ::sys::ToString(c); }
};

struct Convert
{
    static int32_t ToInt32(String* s) { return s ? Int32::Parse(s) : 0; }
    static int32_t ToInt32(float f) { return (int32_t)std::nearbyint(f); }
    static int32_t ToInt32(double f) { return (int32_t)std::nearbyint(f); }
    static int32_t ToInt32(bool b) { return b ? 1 : 0; }
    static float ToSingle(String* s) { return s ? Single::Parse(s) : 0.0f; }
    template<class C> static float ToSingle(String* s, C*) { return ToSingle(s); }
    static bool ToBoolean(String* s) { return s ? Boolean::Parse(s) : false; }
    static uint8_t ToByte(int32_t v) { if (v < 0 || v > 255) ::sys::throw_msg("OverflowException", "byte"); return (uint8_t)v; }
    static uint64_t ToUInt64(String* s) { return s ? UInt64::Parse(s) : 0; }
    static String* ToString(int32_t v) { return ::sys::ToString(v); }
};

struct TimeSpan
{
    int64_t ticks = 0;
    TimeSpan() {}
    explicit TimeSpan(int64_t t) : ticks(t) {}
    TimeSpan(int32_t h, int32_t m, int32_t s) : ticks(((int64_t)h * 3600 + (int64_t)m * 60 + s) * 10000000LL) {}
    static TimeSpan FromSeconds(double s) { return TimeSpan((int64_t)std::llround(s * 1000.0) * 10000LL); }
    static TimeSpan FromMilliseconds(double s) { return TimeSpan((int64_t)std::llround(s) * 10000LL); }
    static TimeSpan FromMinutes(double s) { return FromSeconds(s * 60); }
    static TimeSpan FromTicks(int64_t t) { return TimeSpan(t); }
    static TimeSpan get_Zero() { return TimeSpan(); }
    int64_t get_Ticks() const { return ticks; }
    int32_t get_Days() const { return (int32_t)(ticks / 864000000000LL); }
    int32_t get_Hours() const { return (int32_t)((ticks / 36000000000LL) % 24); }
    int32_t get_Minutes() const { return (int32_t)((ticks / 600000000LL) % 60); }
    int32_t get_Seconds() const { return (int32_t)((ticks / 10000000LL) % 60); }
    int32_t get_Milliseconds() const { return (int32_t)((ticks / 10000LL) % 1000); }
    double get_TotalHours() const { return ticks / 36000000000.0; }
    double get_TotalMinutes() const { return ticks / 600000000.0; }
    double get_TotalSeconds() const { return ticks / 10000000.0; }
    double get_TotalMilliseconds() const { return ticks / 10000.0; }
    friend TimeSpan operator+(TimeSpan a, TimeSpan b) { return TimeSpan(a.ticks + b.ticks); }
    friend TimeSpan operator-(TimeSpan a, TimeSpan b) { return TimeSpan(a.ticks - b.ticks); }
    friend bool operator<(TimeSpan a, TimeSpan b) { return a.ticks < b.ticks; }
    friend bool operator>(TimeSpan a, TimeSpan b) { return a.ticks > b.ticks; }
    friend bool operator<=(TimeSpan a, TimeSpan b) { return a.ticks <= b.ticks; }
    friend bool operator>=(TimeSpan a, TimeSpan b) { return a.ticks >= b.ticks; }
    friend bool operator==(TimeSpan a, TimeSpan b) { return a.ticks == b.ticks; }
    friend bool operator!=(TimeSpan a, TimeSpan b) { return a.ticks != b.ticks; }
    String* ToString();
    int32_t GetHashCode() { return (int32_t)ticks ^ (int32_t)(ticks >> 32); }
    int32_t CompareTo(TimeSpan o) { return ticks < o.ticks ? -1 : (ticks > o.ticks ? 1 : 0); }
};

struct DateTime
{
    int64_t ticks = 0;
    static DateTime get_Now();
    String* ToString();
    int32_t GetHashCode() { return (int32_t)ticks; }
};

struct Version final : Object
{
    int32_t major = 0, minor = 0, build = -1, revision = -1;
    Version() {}
    Version(int32_t a, int32_t b) : major(a), minor(b) {}
    Version(int32_t a, int32_t b, int32_t c) : major(a), minor(b), build(c) {}
    Version(int32_t a, int32_t b, int32_t c, int32_t d) : major(a), minor(b), build(c), revision(d) {}
    explicit Version(String* s);
    int32_t CompareTo(Version* o);
    String* ToString() override;
    bool Equals(Object* o) override { auto* v = dynamic_cast<Version*>(o); return v && CompareTo(v) == 0; }
    static bool op_Equality(Version* a, Version* b) { if (a == b) return true; if (!a || !b) return false; return a->CompareTo(b) == 0; }
    static bool op_Inequality(Version* a, Version* b) { return !op_Equality(a, b); }
    static bool op_LessThan(Version* a, Version* b) { return a->CompareTo(b) < 0; }
    static bool op_GreaterThan(Version* a, Version* b) { return b->CompareTo(a) < 0; }
    static bool op_LessThanOrEqual(Version* a, Version* b) { return a->CompareTo(b) <= 0; }
    static bool op_GreaterThanOrEqual(Version* a, Version* b) { return a->CompareTo(b) >= 0; }
    int32_t get_Major() { return major; }
    int32_t get_Minor() { return minor; }
    int32_t get_Build() { return build; }
    int32_t get_Revision() { return revision; }
};

template<class T1, class T2> struct Tuple final : Object
{
    T1 item1; T2 item2;
    Tuple(const T1& a, const T2& b) : item1(a), item2(b) {}
    T1 get_Item1() { return item1; }
    T2 get_Item2() { return item2; }
};

struct Enum
{
    static Array<String*>* GetNames(Type* t);
    static bool IsDefined(Type* t, Object* value);
    static Object* Parse(Type* t, String* s);
    static Object* Parse(Type* t, String* s, bool ignoreCase);
    template<class E> static String* ToString(E e) { return ::sys::ToString(e); }
    template<class E> static bool HasFlag(E a, E b) { return ((int64_t)a & (int64_t)b) == (int64_t)b; }
    template<class E> static int32_t CompareTo(E a, E b) { return (int64_t)a < (int64_t)b ? -1 : ((int64_t)a > (int64_t)b ? 1 : 0); }
    template<class E> static bool Equals(E a, Object* b) { return ::sys::Equals(a, b); }
    template<class E> static int32_t GetHashCode(E a) { return (int32_t)a; }
};

struct Environment
{
    static String* GetEnvironmentVariable(String* name);
    static void SetEnvironmentVariable(String* name, String* value) {}
    static String* get_NewLine() { return TF_S(u"\n"); }
    static int32_t get_TickCount();
    enum class SpecialFolder : int32_t { ApplicationData = 26, Personal = 5, MyDocuments = 5, LocalApplicationData = 28 };
    static String* GetFolderPath(SpecialFolder f);
};
using Environment_SpecialFolder = Environment::SpecialFolder;

struct AppDomain final : Object
{
    static AppDomain* get_CurrentDomain();
    String* get_BaseDirectory();
};

struct Console
{
    static void WriteLine(String* s);
    static void WriteLine();
    static void Write(String* s);
};

struct GC
{
    static void Collect() { ::sys::gc_collect(); }
    static void Collect(int32_t) { ::sys::gc_collect(); }
    static void WaitForPendingFinalizers() {}
    static int64_t GetTotalMemory(bool force) { if (force) ::sys::gc_collect(); return (int64_t)::sys::gc_live_bytes(); }
};

struct StringComparer final : Object, Collections::Generic::IEqualityComparer<String*>
{
    bool ignoreCase = false;
    static StringComparer* get_InvariantCultureIgnoreCase();
    static StringComparer* get_OrdinalIgnoreCase() { return get_InvariantCultureIgnoreCase(); }
    static StringComparer* get_Ordinal();
    bool Equals(String* a, String* b) override;
    int32_t GetHashCode(String* a) override;
    using Object::Equals;
    using Object::GetHashCode;
};

namespace Globalization {
struct CultureInfo final : Object, IFormatProvider
{
    static CultureInfo* get_InvariantCulture();
    static CultureInfo* get_CurrentCulture() { return get_InvariantCulture(); }
};
struct NumberFormatInfo : Object, IFormatProvider {};
}

namespace Text {
struct Encoding final : Object
{
    static Encoding* get_Unicode();
    static Encoding* get_UTF8();
    static Encoding* get_ASCII() { return get_UTF8(); }
};
struct StringBuilder final : Object
{
    std::u16string buf;
    StringBuilder() {}
    explicit StringBuilder(String* s) { if (s) buf.assign(s->data, s->length); }
    StringBuilder* Append(String* s) { if (s) buf.append(s->data, s->length); return this; }
    StringBuilder* Append(char16_t c) { buf.push_back(c); return this; }
    StringBuilder* Append(int32_t v) { return Append(::sys::ToString(v)); }
    StringBuilder* Append(float v) { return Append(::sys::ToString(v)); }
    StringBuilder* Append(Object* o) { return Append(::sys::ToStr(o)); }
    StringBuilder* AppendLine(String* s) { Append(s); buf.push_back(u'\n'); return this; }
    StringBuilder* AppendLine() { buf.push_back(u'\n'); return this; }
    StringBuilder* Clear() { buf.clear(); return this; }
    int32_t get_Length() { return (int32_t)buf.size(); }
    String* ToString() override { return String::FromStd(buf); }
};
}

namespace Diagnostics {
struct Stopwatch final : Object
{
    int64_t startNs = 0, elapsedNs = 0;
    bool running = false;
    static int64_t NowNs();
    void Start() { if (!running) { startNs = NowNs(); running = true; } }
    void Stop() { if (running) { elapsedNs += NowNs() - startNs; running = false; } }
    void Reset() { running = false; elapsedNs = 0; }
    void Restart() { elapsedNs = 0; startNs = NowNs(); running = true; }
    static Stopwatch* StartNew() { auto* s = ::sys::gcnew<Stopwatch>(); s->Start(); return s; }
    int64_t ElapsedNsNow() { return elapsedNs + (running ? NowNs() - startNs : 0); }
    int64_t get_ElapsedTicks() { return ElapsedNsNow() / 100; }
    int64_t get_ElapsedMilliseconds() { return ElapsedNsNow() / 1000000; }
    TimeSpan get_Elapsed() { return TimeSpan(ElapsedNsNow() / 100); }
    bool get_IsRunning() { return running; }
    static int64_t get_Frequency() { return 10000000; }
};
struct StackTrace final : Object
{
    StackTrace() {}
    StackTrace(Exception* e, bool) {}
    String* ToString() override { return String::Empty(); }
};
struct Debug { static void WriteLine(String* s) { Console::WriteLine(s); } template<class... A> static void Assert(A...) {} };
struct Process final : Object { static Process* Start(String*) { return nullptr; } };
}

inline String* _TF_TASKMSG() { return TF_S(u"[task exception, ignored like .NET] "); }
namespace Threading {
namespace Tasks {
struct Task;
struct TaskFactory final : Object
{
    template<class A> Task* StartNew(A* action);
};
struct Task final : Object
{
    bool completed = false, faulted = false;
    static TaskFactory* get_Factory();
    bool get_IsFaulted() { return faulted; }
    bool get_IsCompleted() { return completed; }
    void Wait() {}
};
template<class A> Task* TaskFactory::StartNew(A* action)
{
    // run synchronously: keeps all GC activity on the main thread.
    // Like .NET, an exception inside a task faults the task silently.
    auto* t = ::sys::gcnew<Task>();
    try { action->Invoke(); }
    catch (::System::Exception* e) { t->faulted = true; Console::WriteLine(String::Concat(_TF_TASKMSG(), e->ToString())); }
    t->completed = true;
    return t;
}
}
struct Thread final : Object { static void Sleep(int32_t ms); };
}

namespace Linq {
struct Enumerable
{
    template<class T, class E> static bool Contains(E* src, T v) { for (auto x : ::sys::each(src)) if (::sys::eq<T>(x, v)) return true; return false; }
    template<class T, class E> static int32_t Count(E* src) { int32_t n = 0; for (auto x : ::sys::each(src)) { (void)x; n++; } return n; }
    template<class T, class E, class P> static int32_t Count(E* src, P* pred) { int32_t n = 0; for (auto x : ::sys::each(src)) if (pred->Invoke(x)) n++; return n; }
    template<class T, class E> static Collections::Generic::List<T>* ToList(E* src) { auto* l = ::sys::gcnew<Collections::Generic::List<T>>(); for (auto x : ::sys::each(src)) l->Add(x); return l; }
    template<class T, class E> static Array<T>* ToArray(E* src) { return ToList<T>(src)->ToArray(); }
    template<class T, class E, class P> static bool Any(E* src, P* pred) { for (auto x : ::sys::each(src)) if (pred->Invoke(x)) return true; return false; }
    template<class T, class E> static bool Any(E* src) { for (auto x : ::sys::each(src)) { (void)x; return true; } return false; }
    template<class T, class E, class P> static bool All(E* src, P* pred) { for (auto x : ::sys::each(src)) if (!pred->Invoke(x)) return false; return true; }
    template<class T, class E> static T First(E* src) { for (auto x : ::sys::each(src)) return x; ::sys::throw_msg("InvalidOperationException", "Sequence contains no elements"); }
    template<class T, class E> static T FirstOrDefault(E* src) { for (auto x : ::sys::each(src)) return x; return T{}; }
    template<class T, class E, class P> static T FirstOrDefault(E* src, P* pred) { for (auto x : ::sys::each(src)) if (pred->Invoke(x)) return x; return T{}; }
    template<class T, class E> static T Last(E* src) { T r{}; bool any = false; for (auto x : ::sys::each(src)) { r = x; any = true; } if (!any) ::sys::throw_msg("InvalidOperationException", "Sequence contains no elements"); return r; }
    template<class T, class E> static T Max(E* src) { T r{}; bool any = false; for (auto x : ::sys::each(src)) { if (!any || x > r) r = x; any = true; } return r; }
    template<class T, class E> static T Min(E* src) { T r{}; bool any = false; for (auto x : ::sys::each(src)) { if (!any || x < r) r = x; any = true; } return r; }
    template<class T, class E> static T Sum(E* src) { T r{}; for (auto x : ::sys::each(src)) r += x; return r; }
};
}

} // namespace System

namespace sys {

// ---------------------------------------------------------------- Type registry
::System::Type* type_lookup(std::type_index ti);
void type_register(std::type_index ti, ::System::Type* t);
::System::Type* type_new(std::type_index ti, const char* name);
::System::String* demangle_name(const char* mangled, bool full);

template<class T> struct type_traits_of { };

template<class T> ::System::Object* type_factory()
{
    using U = std::remove_pointer_t<T>;
    if constexpr (std::is_pointer_v<T>)
    {
        if constexpr (std::is_default_constructible_v<U> && !std::is_abstract_v<U>)
        {
            if constexpr (requires(U* x) { x->__ctor(); }) return to_object(gcnew_ctor<U>());
            else if constexpr (std::is_base_of_v<::System::Object, U> || std::is_polymorphic_v<U>) return to_object(gcnew<U>());
            else return nullptr;
        }
        else return nullptr;
    }
    else return box<T>(T{});
}

template<class T> ::System::Type* typeof_();

template<class T> struct is_list : std::false_type {};
template<class T> struct is_list<::System::Collections::Generic::List<T>*> : std::true_type { typedef T elem; };
template<class T> struct is_array : std::false_type {};
template<class T> struct is_array<::System::Array<T>*> : std::true_type { typedef T elem; };
template<class T> struct is_nullable : std::false_type {};
template<class T> struct is_nullable<::System::Nullable<T>> : std::true_type { typedef T elem; };
template<class T> struct is_fn : std::false_type {};
template<class R, class... A> struct is_fn<Fn<R(A...)>*> : std::true_type {};

::System::Object* parse_prim(std::type_index ti, ::System::String* s);
::sys::Delegate* make_delegate_from_method(::System::Object* target, ::System::Reflection::MethodInfo* m);

template<class Sig> struct MethodDelegate;
template<class R, class... A> struct MethodDelegate<R(A...)>
{
    static Delegate* make(::System::Object* target, ::System::Reflection::MethodInfo* m)
    {
        auto inv = m->invoker;
        return fn<R(A...)>([target, inv](A... a) -> R {
            ::System::Object* r = inv(target);
            if constexpr (std::is_void_v<R>) { (void)r; return; }
            else return unbox_any<R>(r);
        });
    }
};
template<class T> struct fn_sig;
template<class Sig> struct fn_sig<Fn<Sig>*> { typedef Sig type; };

template<class T> ::System::Type* typeof_()
{
    static ::System::Type* cached = nullptr;
    if (cached) return cached;
    using U = std::remove_pointer_t<T>;
    std::type_index ti(typeid(U));
    if (auto* existing = type_lookup(ti)) { cached = existing; return cached; }
    ::System::Type* t = type_new(ti, typeid(U).name());
    cached = t;
    if constexpr (std::is_same_v<T, ::System::String*>) { t->kind = ::System::TypeKind::String; }
    else if constexpr (std::is_arithmetic_v<T>)
    {
        t->kind = ::System::TypeKind::Primitive;
        t->parse = [](::System::String* s) -> ::System::Object* { return parse_prim(std::type_index(typeid(T)), s); };
        t->factory = &type_factory<T>;
    }
    else if constexpr (std::is_enum_v<T>)
    {
        t->kind = ::System::TypeKind::Enum;
        if constexpr (EnumInfo<T>::defined) t->enumMeta = &EnumInfo<T>::get();
        t->parse = [](::System::String* s) -> ::System::Object* {
            int64_t v = 0;
            if constexpr (EnumInfo<T>::defined) { if (!enum_parse(EnumInfo<T>::get(), s, v)) throw_msg("ArgumentException", "enum parse"); }
            return box<T>((T)v);
        };
        t->factory = &type_factory<T>;
    }
    else if constexpr (is_nullable<T>::value)
    {
        t->kind = ::System::TypeKind::Nullable;
        t->elementType = &typeof_<typename is_nullable<T>::elem>;
    }
    else if constexpr (is_array<T>::value)
    {
        using E = typename is_array<T>::elem;
        t->kind = ::System::TypeKind::Array;
        t->elementType = &typeof_<E>;
        t->newArray = [](int32_t n) -> ::System::Object* { return ::System::Array<E>::New(n); };
        t->arraySet = [](::System::Object* a, int32_t i, ::System::Object* v) { (*static_cast<::System::Array<E>*>(a))[i] = unbox_any<E>(v); };
        t->arrayGet = [](::System::Object* a, int32_t i) -> ::System::Object* { return box_any<E>((*static_cast<::System::Array<E>*>(a))[i]); };
        t->count = [](::System::Object* a) -> int32_t { return static_cast<::System::Array<E>*>(a)->length; };
    }
    else if constexpr (is_list<T>::value)
    {
        using E = typename is_list<T>::elem;
        t->kind = ::System::TypeKind::List;
        t->elementType = &typeof_<E>;
        t->factory = &type_factory<T>;
        t->listAdd = [](::System::Object* l, ::System::Object* v) { static_cast<::System::Collections::Generic::List<E>*>(l)->Add(unbox_any<E>(v)); };
        t->arrayGet = [](::System::Object* l, int32_t i) -> ::System::Object* { return box_any<E>((*static_cast<::System::Collections::Generic::List<E>*>(l))[i]); };
        t->count = [](::System::Object* l) -> int32_t { return static_cast<::System::Collections::Generic::List<E>*>(l)->get_Count(); };
    }
    else if constexpr (is_fn<T>::value)
    {
        t->kind = ::System::TypeKind::Delegate;
        t->makeDelegate = [](::System::Object* target, ::System::Reflection::MethodInfo* m) -> Delegate* { return MethodDelegate<typename fn_sig<T>::type>::make(target, m); };
    }
    else if constexpr (std::is_pointer_v<T>)
    {
        t->kind = ::System::TypeKind::Class;
        t->factory = &type_factory<T>;
    }
    else
    {
        t->kind = ::System::TypeKind::Struct;
        if constexpr (std::is_default_constructible_v<T>) t->factory = &type_factory<T>;
    }
    return t;
}

template<class T> ::System::Type* GetType(T* p)
{
    if (!p) throw_null();
    ::System::Object* o = to_object(p);
    std::type_index ti(typeid(*o));
    if (auto* t = type_lookup(ti)) return t;
    return type_new(ti, typeid(*o).name());
}

// boxing implementation
template<class T> ::System::String* Boxed<T>::ToString() { return ::sys::ToString(value); }
template<class T> bool Boxed<T>::Equals(::System::Object* o)
{
    auto* b = dynamic_cast<Boxed<T>*>(o);
    if (!b) return false;
    if constexpr (std::is_class_v<T>) return memcmp(&b->value, &value, sizeof(T)) == 0;
    else return b->value == value;
}
template<class T> int32_t Boxed<T>::GetHashCode() { return ::sys::GetHashCode(value); }

::System::Object* box_small_int(int32_t v);
template<class T> ::System::Object* box(const T& v)
{
    if constexpr (std::is_same_v<T, int32_t>) { if (v >= -16 && v < 256) return box_small_int(v); }
    if constexpr (is_nullable<T>::value) { if (!v.hasValue) return nullptr; return box(v.value); }
    else return gcnew<Boxed<T>>(v);
}
template<class T> ::System::Object* box_nullable(const ::System::Nullable<T>& v) { if (!v.hasValue) return nullptr; return box<T>(v.value); }

template<class T> T unbox(::System::Object* o)
{
    if constexpr (is_nullable<T>::value)
    {
        if (!o) return T();
        return T(unbox<typename is_nullable<T>::elem>(o));
    }
    else
    {
        if (!o) throw_null();
        if (auto* b = dynamic_cast<Boxed<T>*>(o)) return b->value;
        if constexpr (std::is_enum_v<T> || std::is_integral_v<T>)
        {
            // enum <-> underlying int unboxing
            if (auto* bi = dynamic_cast<Boxed<int32_t>*>(o)) return (T)bi->value;
        }
        throw_invalid_cast(typeid(T).name());
    }
}

} // namespace sys

namespace sys {
void type_set_name(::System::Type* t, const char16_t* name, const char16_t* fullName);
}
