#include "runtime_sys.h"
#include "../platform/platform.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unordered_map>
#if defined(__GNUC__)
#include <cxxabi.h>
#endif

using namespace System;

namespace sys {

static std::unordered_map<std::type_index, Type*>& Registry()
{
    static std::unordered_map<std::type_index, Type*> r;
    return r;
}

Type* type_lookup(std::type_index ti)
{
    auto& r = Registry();
    auto it = r.find(ti);
    return it == r.end() ? nullptr : it->second;
}
void type_register(std::type_index ti, Type* t) { Registry()[ti] = t; }

::System::String* demangle_name(const char* mangled, bool full)
{
    std::string s;
#if defined(__GNUC__)
    int status = 0;
    char* d = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    if (d && status == 0) { s = d; free(d); }
    else s = mangled;
#else
    s = mangled;
#endif
    // convert C++ "A::B::C" to "A.B.C" and strip template noise
    std::string out;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == ':' && i + 1 < s.size() && s[i + 1] == ':') { out += '.'; i++; }
        else out += s[i];
    }
    if (!full)
    {
        size_t lt = out.find('<');
        std::string base = lt == std::string::npos ? out : out.substr(0, lt);
        size_t dot = base.rfind('.');
        out = dot == std::string::npos ? base : base.substr(dot + 1);
    }
    return String::FromUtf8(out.c_str());
}

Type* type_new(std::type_index ti, const char* mangled)
{
    Type* t = gcnew<Type>();
    gc_pin(t);
    t->tid = ti;
    t->fullName = demangle_name(mangled, true);
    t->name = demangle_name(mangled, false);
    gc_pin(t->fullName);
    gc_pin(t->name);
    type_register(ti, t);
    return t;
}

void type_set_name(Type* t, const char16_t* name, const char16_t* fullName)
{
    t->name = String::Lit(name, std::char_traits<char16_t>::length(name));
    t->fullName = String::Lit(fullName, std::char_traits<char16_t>::length(fullName));
    t->reflected = true;
}

Object* parse_prim(std::type_index ti, String* s)
{
    if (ti == typeid(int32_t)) return box<int32_t>(Int32::Parse(s));
    if (ti == typeid(float)) return box<float>(Single::Parse(s));
    if (ti == typeid(double)) return box<double>(Double::Parse(s));
    if (ti == typeid(bool)) return box<bool>(Boolean::Parse(s));
    if (ti == typeid(int64_t)) return box<int64_t>(Int64::Parse(s));
    if (ti == typeid(uint64_t)) return box<uint64_t>(UInt64::Parse(s));
    if (ti == typeid(uint32_t)) return box<uint32_t>((uint32_t)Int64::Parse(s));
    if (ti == typeid(uint8_t)) return box<uint8_t>((uint8_t)Int32::Parse(s));
    if (ti == typeid(int16_t)) return box<int16_t>((int16_t)Int32::Parse(s));
    if (ti == typeid(char16_t)) return box<char16_t>(s->length ? s->data[0] : 0);
    throw_msg("FormatException", "unsupported primitive parse");
}

Delegate* make_delegate_from_method(Object* target, Reflection::MethodInfo* m) { return nullptr; }

} // namespace sys

namespace System {

// ---------------------------------------------------------------- Type / reflection
void Type::AddField(const char16_t* n, Type* t, bool isStatic, bool isLiteral, Object* (*get)(Object*), void (*set)(Object*, Object*), Array<Object*>* (*attrs)())
{
    auto* f = ::sys::gcnew<Reflection::FieldInfo>();
    ::sys::gc_pin(f);
    f->name = String::Lit(n, std::char_traits<char16_t>::length(n));
    f->type = t; f->isStatic = isStatic; f->isLiteral = isLiteral;
    f->getter = get; f->setter = set; f->attrs = attrs;
    fields.push_back(f);
}
void Type::AddProp(const char16_t* n, Type* t, Object* (*get)(Object*), void (*set)(Object*, Object*))
{
    auto* p = ::sys::gcnew<Reflection::PropertyInfo>();
    ::sys::gc_pin(p);
    p->name = String::Lit(n, std::char_traits<char16_t>::length(n));
    p->type = t; p->getter = get; p->setter = set;
    props.push_back(p);
}
void Type::AddMethod(const char16_t* n, Object* (*invoke)(Object*))
{
    auto* m = ::sys::gcnew<Reflection::MethodInfo>();
    ::sys::gc_pin(m);
    m->name = String::Lit(n, std::char_traits<char16_t>::length(n));
    m->invoker = invoke;
    methods.push_back(m);
}
Array<Reflection::FieldInfo*>* Type::GetFields()
{
    std::vector<Reflection::FieldInfo*> all;
    for (Type* t = this; t; t = t->baseType)
        for (auto* f : t->fields)
            if (t == this || !f->isStatic) all.push_back(f);
    auto* a = Array<Reflection::FieldInfo*>::New((int32_t)all.size());
    for (size_t i = 0; i < all.size(); i++) a->data[i] = all[i];
    return a;
}
Array<Reflection::PropertyInfo*>* Type::GetProperties()
{
    std::vector<Reflection::PropertyInfo*> all;
    for (Type* t = this; t; t = t->baseType) for (auto* p : t->props) all.push_back(p);
    auto* a = Array<Reflection::PropertyInfo*>::New((int32_t)all.size());
    for (size_t i = 0; i < all.size(); i++) a->data[i] = all[i];
    return a;
}
Reflection::FieldInfo* Type::GetField(String* n)
{
    for (Type* t = this; t; t = t->baseType) for (auto* f : t->fields) if (String::op_Equality(f->name, n)) return f;
    return nullptr;
}
Reflection::MethodInfo* Type::GetMethod(String* n)
{
    for (Type* t = this; t; t = t->baseType) for (auto* m : t->methods) if (String::op_Equality(m->name, n)) return m;
    return nullptr;
}
Reflection::MethodInfo* Type::GetMethod(String* n, Reflection::BindingFlags) { return GetMethod(n); }

Array<Object*>* Reflection::FieldInfo::GetCustomAttributes(bool)
{
    if (!attrCache)
    {
        attrCache = attrs ? attrs() : Array<Object*>::New(0);
        ::sys::gc_pin(attrCache);
    }
    return attrCache;
}

} // namespace System

::sys::Delegate* sys::Delegate::CreateDelegate(::System::Type* t, ::System::Object* target, ::System::String* method)
{
    if (!target) return nullptr;
    auto* m = ::sys::GetType(target)->GetMethod(method);
    if (!m || !t->makeDelegate) return nullptr;
    return t->makeDelegate(target, m);
}

namespace System {

EventArgs* EventArgs::get_Empty() { static EventArgs* e = nullptr; if (!e) { e = ::sys::gcnew<EventArgs>(); ::sys::gc_pin(e); } return e; }

// ---------------------------------------------------------------- Exception
Exception::Exception() {}
String* Exception::ToString()
{
    String* n = ::sys::demangle_name(typeid(*this).name(), true);
    return String::Concat(String::Concat(n, TF_S(u": ")), get_Message());
}

// ---------------------------------------------------------------- Random (.NET Framework algorithm)
Random::Random() { Init(Environment::get_TickCount()); }
Random::Random(int32_t seed) { Init(seed); }
void Random::Init(int32_t Seed)
{
    const int32_t MSEED = 161803398;
    int32_t ii, mj, mk;
    int32_t subtraction = (Seed == INT32_MIN) ? INT32_MAX : (Seed < 0 ? -Seed : Seed);
    mj = MSEED - subtraction;
    SeedArray[55] = mj;
    mk = 1;
    for (int32_t i = 1; i < 55; i++)
    {
        ii = (21 * i) % 55;
        SeedArray[ii] = mk;
        mk = mj - mk;
        if (mk < 0) mk += INT32_MAX;
        mj = SeedArray[ii];
    }
    for (int32_t k = 1; k < 5; k++)
        for (int32_t i = 1; i < 56; i++)
        {
            SeedArray[i] -= SeedArray[1 + (i + 30) % 55];
            if (SeedArray[i] < 0) SeedArray[i] += INT32_MAX;
        }
    inext = 0;
    inextp = 21;
}
int32_t Random::InternalSample()
{
    int32_t locINext = inext, locINextp = inextp;
    if (++locINext >= 56) locINext = 1;
    if (++locINextp >= 56) locINextp = 1;
    int32_t retVal = SeedArray[locINext] - SeedArray[locINextp];
    if (retVal == INT32_MAX) retVal--;
    if (retVal < 0) retVal += INT32_MAX;
    SeedArray[locINext] = retVal;
    inext = locINext;
    inextp = locINextp;
    return retVal;
}
double Random::GetSampleForLargeRange()
{
    int32_t result = InternalSample();
    bool negative = InternalSample() % 2 == 0;
    if (negative) result = -result;
    double d = result;
    d += (INT32_MAX - 1);
    d /= 2.0 * INT32_MAX - 1;
    return d;
}
int32_t Random::Next(int32_t max)
{
    if (max < 0) ::sys::throw_msg("ArgumentOutOfRangeException", "maxValue");
    return (int32_t)(Sample() * max);
}
int32_t Random::Next(int32_t min, int32_t max)
{
    if (min > max) ::sys::throw_msg("ArgumentOutOfRangeException", "minValue");
    int64_t range = (int64_t)max - min;
    if (range <= INT32_MAX) return (int32_t)(Sample() * range) + min;
    return (int32_t)((int64_t)(GetSampleForLargeRange() * range) + min);
}

// ---------------------------------------------------------------- parsing
static std::string Narrow(String* s)
{
    if (!s) ::sys::throw_msg("ArgumentNullException", "s");
    std::string r = s->Trim()->ToUtf8();
    return r;
}
int32_t Int32::Parse(String* s)
{
    std::string t = Narrow(s);
    char* end = nullptr;
    long long v = strtoll(t.c_str(), &end, 10);
    if (t.empty() || *end != 0) ::sys::throw_msg("FormatException", ("Input string was not in a correct format: " + t).c_str());
    if (v > INT32_MAX || v < INT32_MIN) ::sys::throw_msg("OverflowException", "Int32");
    return (int32_t)v;
}
bool Int32::TryParse(String* s, int32_t& out)
{
    if (!s) { out = 0; return false; }
    std::string t = s->Trim()->ToUtf8();
    char* end = nullptr;
    long long v = strtoll(t.c_str(), &end, 10);
    if (t.empty() || *end != 0 || v > INT32_MAX || v < INT32_MIN) { out = 0; return false; }
    out = (int32_t)v; return true;
}
int64_t Int64::Parse(String* s)
{
    std::string t = Narrow(s);
    char* end = nullptr;
    long long v = strtoll(t.c_str(), &end, 10);
    if (t.empty() || *end != 0) ::sys::throw_msg("FormatException", "Int64");
    return v;
}
uint64_t UInt64::Parse(String* s)
{
    std::string t = Narrow(s);
    char* end = nullptr;
    unsigned long long v = strtoull(t.c_str(), &end, 10);
    if (t.empty() || *end != 0) ::sys::throw_msg("FormatException", "UInt64");
    return v;
}
static bool ParseDouble(const std::string& t, double& out)
{
    // invariant culture: '.' decimal separator, independent of C locale
    const char* p = t.c_str();
    while (*p == ' ') p++;
    bool neg = false;
    if (*p == '-') { neg = true; p++; } else if (*p == '+') p++;
    double v = 0; bool any = false;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; any = true; }
    if (*p == '.')
    {
        p++;
        double f = 0.1;
        while (*p >= '0' && *p <= '9') { v += (*p - '0') * f; f *= 0.1; p++; any = true; }
    }
    if (any && (*p == 'e' || *p == 'E'))
    {
        p++;
        bool eneg = false;
        if (*p == '-') { eneg = true; p++; } else if (*p == '+') p++;
        int e = 0;
        while (*p >= '0' && *p <= '9') { e = e * 10 + (*p - '0'); p++; }
        v *= std::pow(10.0, eneg ? -e : e);
    }
    while (*p == ' ') p++;
    if (!any)
    {
        std::string r(p);
        if (r == "Infinity") { out = neg ? -INFINITY : INFINITY; return true; }
        if (r == "NaN") { out = NAN; return true; }
        return false;
    }
    if (*p) return false;
    out = neg ? -v : v;
    return true;
}
float Single::Parse(String* s)
{
    double d;
    std::string t = Narrow(s);
    // use strtod for exactness when possible
    if (!ParseDouble(t, d)) ::sys::throw_msg("FormatException", ("Input string was not in a correct format: " + t).c_str());
    char* end = nullptr;
    double d2 = strtod(t.c_str(), &end);
    if (end && *end == 0 && t.find(',') == std::string::npos) d = d2;
    return (float)d;
}
bool Single::TryParse(String* s, float& out)
{
    double d;
    if (!s || !ParseDouble(s->Trim()->ToUtf8(), d)) { out = 0; return false; }
    out = (float)d; return true;
}
double Double::Parse(String* s)
{
    double d;
    if (!ParseDouble(Narrow(s), d)) ::sys::throw_msg("FormatException", "Double");
    return d;
}
bool Boolean::Parse(String* s)
{
    std::string t = Narrow(s);
    for (auto& c : t) c = (char)tolower(c);
    if (t == "true") return true;
    if (t == "false") return false;
    ::sys::throw_msg("FormatException", "Boolean");
}

// ---------------------------------------------------------------- time
String* TimeSpan::ToString()
{
    char b[64];
    int64_t t = ticks < 0 ? -ticks : ticks;
    int64_t days = t / 864000000000LL;
    int h = (int)((t / 36000000000LL) % 24), m = (int)((t / 600000000LL) % 60), s = (int)((t / 10000000LL) % 60);
    int frac = (int)(t % 10000000LL);
    std::string r = ticks < 0 ? "-" : "";
    if (days) r += std::to_string(days) + ".";
    snprintf(b, sizeof b, "%02d:%02d:%02d", h, m, s);
    r += b;
    if (frac) { snprintf(b, sizeof b, ".%07d", frac); r += b; }
    return String::FromUtf8(r.c_str());
}
DateTime DateTime::get_Now()
{
    DateTime d;
    d.ticks = (int64_t)time(nullptr) * 10000000LL + 621355968000000000LL;
    return d;
}
String* DateTime::ToString()
{
    time_t t = (time_t)((ticks - 621355968000000000LL) / 10000000LL);
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[64];
    strftime(b, sizeof b, "%m/%d/%Y %I:%M:%S %p", &tmv);
    return String::FromUtf8(b);
}

Version::Version(String* s)
{
    if (!s) ::sys::throw_msg("ArgumentNullException", "version");
    auto* parts = s->Split(Array<char16_t>::Of({ u'.' }));
    if (parts->length < 2) ::sys::throw_msg("ArgumentException", "Version string portion was too short or too long.");
    major = Int32::Parse(parts->data[0]);
    minor = Int32::Parse(parts->data[1]);
    if (parts->length > 2) build = Int32::Parse(parts->data[2]);
    if (parts->length > 3) revision = Int32::Parse(parts->data[3]);
}
int32_t Version::CompareTo(Version* o)
{
    if (!o) return 1;
    if (major != o->major) return major > o->major ? 1 : -1;
    if (minor != o->minor) return minor > o->minor ? 1 : -1;
    if (build != o->build) return build > o->build ? 1 : -1;
    if (revision != o->revision) return revision > o->revision ? 1 : -1;
    return 0;
}
String* Version::ToString()
{
    std::string r = std::to_string(major) + "." + std::to_string(minor);
    if (build >= 0) r += "." + std::to_string(build);
    if (build >= 0 && revision >= 0) r += "." + std::to_string(revision);
    return String::FromUtf8(r.c_str());
}

// ---------------------------------------------------------------- Enum
static const ::sys::EnumMeta& MetaOf(Type* t)
{
    if (!t || !t->enumMeta) ::sys::throw_msg("ArgumentException", "Type provided must be an Enum.");
    return *t->enumMeta;
}
Array<String*>* Enum::GetNames(Type* t)
{
    auto& m = MetaOf(t);
    // .NET returns names sorted by (unsigned) value
    std::vector<::sys::EnumEntry> e = m.entries;
    std::stable_sort(e.begin(), e.end(), [](const ::sys::EnumEntry& a, const ::sys::EnumEntry& b) { return (uint64_t)a.value < (uint64_t)b.value; });
    auto* a = Array<String*>::New((int32_t)e.size());
    for (size_t i = 0; i < e.size(); i++) a->data[i] = String::Lit(e[i].name, std::char_traits<char16_t>::length(e[i].name));
    return a;
}
bool Enum::IsDefined(Type* t, Object* value)
{
    auto& m = MetaOf(t);
    if (auto* s = dynamic_cast<String*>(value))
    {
        for (auto& e : m.entries) if (String::op_Equality(s, String::Lit(e.name, std::char_traits<char16_t>::length(e.name)))) return true;
        return false;
    }
    if (auto* bi = dynamic_cast<::sys::Boxed<int32_t>*>(value)) { for (auto& e : m.entries) if (e.value == bi->value) return true; return false; }
    // boxed enum of that type: compare via ToString round trip
    String* str = value ? value->ToString() : nullptr;
    for (auto& e : m.entries) if (str && String::op_Equality(str, String::Lit(e.name, std::char_traits<char16_t>::length(e.name)))) return true;
    return false;
}
Object* Enum::Parse(Type* t, String* s) { return Parse(t, s, false); }
Object* Enum::Parse(Type* t, String* s, bool ignoreCase)
{
    int64_t v;
    if (!::sys::enum_parse(MetaOf(t), s, v, ignoreCase))
        ::sys::throw_msg("ArgumentException", ("Requested value '" + (s ? s->ToUtf8() : std::string("null")) + "' was not found.").c_str());
    // re-box via the type's parse function to get the right boxed enum type
    if (t->parse)
    {
        String* num = ::sys::ToString(v);
        return t->parse(num);
    }
    return ::sys::box<int32_t>((int32_t)v);
}

// ---------------------------------------------------------------- Environment & misc
String* Environment::GetEnvironmentVariable(String* name)
{
    const char* v = getenv(name->ToUtf8().c_str());
    return v ? String::FromUtf8(v) : nullptr;
}
int32_t Environment::get_TickCount() { return (int32_t)(plat::now_ns() / 1000000); }
String* Environment::GetFolderPath(SpecialFolder) { return String::FromUtf8(plat::save_dir().c_str()); }

AppDomain* AppDomain::get_CurrentDomain() { static AppDomain* d = nullptr; if (!d) { d = ::sys::gcnew<AppDomain>(); ::sys::gc_pin(d); } return d; }
String* AppDomain::get_BaseDirectory() { return String::FromUtf8((plat::content_root() + "/").c_str()); }

void Console::WriteLine(String* s) { plat::log(s ? s->ToUtf8().c_str() : ""); }
void Console::WriteLine() { plat::log(""); }
void Console::Write(String* s) { plat::log(s ? s->ToUtf8().c_str() : ""); }

StringComparer* StringComparer::get_InvariantCultureIgnoreCase()
{
    static StringComparer* c = nullptr;
    if (!c) { c = ::sys::gcnew<StringComparer>(); c->ignoreCase = true; ::sys::gc_pin(c); }
    return c;
}
StringComparer* StringComparer::get_Ordinal()
{
    static StringComparer* c = nullptr;
    if (!c) { c = ::sys::gcnew<StringComparer>(); ::sys::gc_pin(c); }
    return c;
}
bool StringComparer::Equals(String* a, String* b)
{
    if (!ignoreCase) return String::op_Equality(a, b);
    if (a == b) return true;
    if (!a || !b || a->length != b->length) return false;
    for (int32_t i = 0; i < a->length; i++) if (Char::ToLower(a->data[i]) != Char::ToLower(b->data[i])) return false;
    return true;
}
int32_t StringComparer::GetHashCode(String* a)
{
    if (!a) return 0;
    if (!ignoreCase) return a->GetHashCode();
    uint32_t h = 5381;
    for (int32_t i = 0; i < a->length; i++) h = ((h << 5) + h) ^ Char::ToLower(a->data[i]);
    return (int32_t)h;
}

namespace Globalization {
CultureInfo* CultureInfo::get_InvariantCulture() { static CultureInfo* c = nullptr; if (!c) { c = ::sys::gcnew<CultureInfo>(); ::sys::gc_pin(c); } return c; }
}
namespace Text {
Encoding* Encoding::get_Unicode() { static Encoding* e = nullptr; if (!e) { e = ::sys::gcnew<Encoding>(); ::sys::gc_pin(e); } return e; }
Encoding* Encoding::get_UTF8() { return get_Unicode(); }
}
namespace Diagnostics {
int64_t Stopwatch::NowNs() { return plat::now_ns(); }
}
namespace Threading {
namespace Tasks {
TaskFactory* Task::get_Factory() { static TaskFactory* f = nullptr; if (!f) { f = ::sys::gcnew<TaskFactory>(); ::sys::gc_pin(f); } return f; }
}
void Thread::Sleep(int32_t ms) { plat::sleep_ms(ms); }
}

} // namespace System
