#include "runtime_sys.h"
#include "../platform/platform.h"
#include <cstdio>
#include <cstdlib>
#include <cinttypes>
#include <algorithm>

using namespace System;

namespace sys {

[[noreturn]] __attribute__((noinline)) void throw_null()
{
    // Log where it happened: the caller's address plus our own, so it can be mapped back with
    // addr2line even when the code is relocated (Wii U RPX).
    char b[160];
    snprintf(b, sizeof b, "NullReferenceException at caller=%p (throw_null=%p)", __builtin_return_address(0), (void*)&throw_null);
    plat::log(b);
    throw gcnew<NullReferenceException>(String::FromUtf8("Object reference not set to an instance of an object."));
}
[[noreturn]] void throw_invalid_cast(const char* to)
{
    std::string m = "Specified cast is not valid (to ";
    m += to; m += ")";
    throw gcnew<InvalidCastException>(String::FromUtf8(m.c_str()));
}
[[noreturn]] void throw_index() { throw gcnew<IndexOutOfRangeException>(String::FromUtf8("Index was outside the bounds of the array.")); }
[[noreturn]] void throw_switch() { throw gcnew<InvalidOperationException>(String::FromUtf8("Unhandled switch value.")); }
[[noreturn]] void throw_msg(const char* type, const char* msg)
{
    String* m = String::FromUtf8(msg);
    std::string t = type;
    if (t == "InvalidOperationException") throw gcnew<InvalidOperationException>(m);
    if (t == "ArgumentException") throw gcnew<ArgumentException>(m);
    if (t == "KeyNotFoundException") throw gcnew<KeyNotFoundException>(m);
    if (t == "FormatException") throw gcnew<FormatException>(m);
    if (t == "OverflowException") throw gcnew<OverflowException>(m);
    if (t == "IOException") throw gcnew<IO::IOException>(m);
    if (t == "FileNotFoundException") throw gcnew<IO::FileNotFoundException>(m);
    if (t == "NotImplementedException") throw gcnew<NotImplementedException>(m);
    throw gcnew<Exception>(m);
}

int32_t hash_float(float f)
{
    if (f == 0) return 0;
    uint32_t u; memcpy(&u, &f, 4);
    return (int32_t)u;
}
int32_t hash_double(double d)
{
    if (d == 0) return 0;
    uint64_t u; memcpy(&u, &d, 8);
    return (int32_t)u ^ (int32_t)(u >> 32);
}

static String* Ascii(const char* s) { return String::FromUtf8(s); }

::System::String* ToString(int32_t v) { char b[16]; snprintf(b, sizeof b, "%d", v); return Ascii(b); }
::System::String* ToString(uint32_t v) { char b[16]; snprintf(b, sizeof b, "%u", v); return Ascii(b); }
::System::String* ToString(int64_t v) { char b[32]; snprintf(b, sizeof b, "%" PRId64, v); return Ascii(b); }
::System::String* ToString(uint64_t v) { char b[32]; snprintf(b, sizeof b, "%" PRIu64, v); return Ascii(b); }
::System::String* ToString(bool v) { return v ? TF_S(u"True") : TF_S(u"False"); }
::System::String* ToString(char16_t v) { return String::FromU16(&v, 1); }
::System::String* ToString(::System::String* v) { return v; }

// .NET Framework style "G" formatting: up to 7 (float) / 15 (double) significant digits
static std::string FormatG(double v, int digits)
{
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
    char b[64];
    snprintf(b, sizeof b, "%.*G", digits, v);
    std::string s = b;
    // %G gives exponent form like 1E+07; .NET uses E+07 too. Strip trailing zeros handled by %G.
    size_t e = s.find('E');
    if (e != std::string::npos)
    {
        // .NET: 1E+15 style, exponent at least 2 digits
        std::string mant = s.substr(0, e), ex = s.substr(e + 1);
        char sign = ex[0];
        std::string digs = ex.substr(1);
        while (digs.size() > 2 && digs[0] == '0') digs.erase(0, 1);
        while (digs.size() < 2) digs = "0" + digs;
        s = mant + "E" + sign + digs;
    }
    return s;
}

::System::String* ToString(float v) { return Ascii(FormatG(v, 7).c_str()); }
::System::String* ToString(double v) { return Ascii(FormatG(v, 15).c_str()); }

// Custom / standard numeric format strings (subset)
static std::string FormatNumber(double v, const std::u16string& fmt, bool isInt)
{
    if (fmt.empty()) return isInt ? std::to_string((long long)v) : FormatG(v, 15);
    char c0 = (char)toupper((char)fmt[0]);
    bool standard = fmt.size() <= 3 && (c0 == 'F' || c0 == 'D' || c0 == 'N' || c0 == 'G' || c0 == 'X' || c0 == 'P') &&
                    std::all_of(fmt.begin() + 1, fmt.end(), [](char16_t c) { return c >= u'0' && c <= u'9'; });
    if (standard)
    {
        int prec = -1;
        if (fmt.size() > 1) prec = std::stoi(std::string(fmt.begin() + 1, fmt.end()));
        char b[128];
        switch (c0)
        {
        case 'F': snprintf(b, sizeof b, "%.*f", prec < 0 ? 2 : prec, v); return b;
        case 'P': snprintf(b, sizeof b, "%.*f %%", prec < 0 ? 2 : prec, v * 100); return b;
        case 'D': { long long iv = (long long)v; snprintf(b, sizeof b, "%0*lld", prec < 0 ? 1 : prec, iv); return b; }
        case 'X': { snprintf(b, sizeof b, fmt[0] == u'x' ? "%0*llx" : "%0*llX", prec < 0 ? 1 : prec, (long long)v); return b; }
        case 'N':
        {
            snprintf(b, sizeof b, "%.*f", prec < 0 ? 2 : prec, std::fabs(v));
            std::string s = b, ip = s.substr(0, s.find('.')), fp = s.find('.') != std::string::npos ? s.substr(s.find('.')) : "";
            std::string r;
            for (size_t i = 0; i < ip.size(); i++) { if (i && (ip.size() - i) % 3 == 0) r += ','; r += ip[i]; }
            return (v < 0 ? "-" : "") + r + fp;
        }
        case 'G': return FormatG(v, prec <= 0 ? 15 : prec);
        }
    }
    // custom: count digits after '.', zero placeholders before '.', grouping by literal separators
    std::u16string f = fmt;
    size_t dot = f.find(u'.');
    int decimals = 0, minDecimals = 0;
    if (dot != std::u16string::npos)
        for (size_t i = dot + 1; i < f.size(); i++)
        {
            if (f[i] == u'0') { decimals++; minDecimals = decimals; }
            else if (f[i] == u'#') decimals++;
            else break;
        }
    char b[128];
    snprintf(b, sizeof b, "%.*f", decimals, std::fabs(v));
    std::string num = b;
    std::string ip = num.substr(0, num.find('.'));
    std::string fp = num.find('.') != std::string::npos ? num.substr(num.find('.') + 1) : "";
    while ((int)fp.size() > minDecimals && !fp.empty() && fp.back() == '0') fp.pop_back();
    // integer part placeholders
    std::u16string ipat = dot == std::u16string::npos ? f : f.substr(0, dot);
    int zeroCount = 0;
    for (auto c : ipat) if (c == u'0') zeroCount++;
    if (ip == "0" && zeroCount == 0) ip = "";
    while ((int)ip.size() < zeroCount) ip = "0" + ip;
    // fill placeholders right-to-left, keeping literal characters
    std::string out;
    int di = (int)ip.size() - 1;
    int lastPlaceholder = -1;
    for (int i = (int)ipat.size() - 1; i >= 0; i--) if (ipat[i] == u'0' || ipat[i] == u'#') { lastPlaceholder = i; }
    for (int i = (int)ipat.size() - 1; i >= 0; i--)
    {
        char16_t c = ipat[i];
        if (c == u'0' || c == u'#')
        {
            if (di >= 0) out.insert(out.begin(), ip[di--]);
            if (i == lastPlaceholder) while (di >= 0) out.insert(out.begin(), ip[di--]);
        }
        else
        {
            // literal: only keep if more digits remain to the left
            bool digitsLeft = di >= 0;
            if (digitsLeft || (c != u' ' && c != u',')) out.insert(out.begin(), (char)c);
        }
    }
    if (!fp.empty()) out += "." + fp;
    if (v < 0 && out.find_first_not_of("0.") != std::string::npos) out = "-" + out;
    // trim leading separators
    while (!out.empty() && out[0] == ' ') out.erase(0, 1);
    return out;
}

::System::String* ToString(int32_t v, ::System::String* fmt) { return Ascii(FormatNumber(v, fmt ? fmt->ToU16() : u"", true).c_str()); }
::System::String* ToString(int64_t v, ::System::String* fmt) { return Ascii(FormatNumber((double)v, fmt ? fmt->ToU16() : u"", true).c_str()); }
::System::String* ToString(uint64_t v, ::System::String* fmt) { return Ascii(FormatNumber((double)v, fmt ? fmt->ToU16() : u"", true).c_str()); }
::System::String* ToString(float v, ::System::String* fmt) { return Ascii(FormatNumber(v, fmt ? fmt->ToU16() : u"", false).c_str()); }
::System::String* ToString(double v, ::System::String* fmt) { return Ascii(FormatNumber(v, fmt ? fmt->ToU16() : u"", false).c_str()); }

::System::String* type_name_string(const char* mangled) { return demangle_name(mangled, true); }

::System::String* enum_to_string(const EnumMeta& m, int64_t v)
{
    for (auto& e : m.entries) if (e.value == v) return String::Lit(e.name, std::char_traits<char16_t>::length(e.name));
    if (m.flags && v != 0)
    {
        std::u16string r;
        int64_t rem = v;
        for (auto it = m.entries.rbegin(); it != m.entries.rend(); ++it)
            if (it->value != 0 && (rem & it->value) == it->value)
            {
                rem &= ~it->value;
                r = std::u16string(it->name) + (r.empty() ? u"" : u", ") + r;
            }
        if (rem == 0 && !r.empty()) return String::FromStd(r);
    }
    return ToString(v);
}

bool enum_parse(const EnumMeta& m, ::System::String* s, int64_t& out, bool ignoreCase)
{
    if (!s) return false;
    std::u16string str = s->Trim()->ToU16();
    auto eqs = [&](const std::u16string& a, const char16_t* b) {
        size_t n = std::char_traits<char16_t>::length(b);
        if (a.size() != n) return false;
        for (size_t i = 0; i < n; i++)
        {
            char16_t x = a[i], y = b[i];
            if (ignoreCase) { x = Char::ToLower(x); y = Char::ToLower(y); }
            if (x != y) return false;
        }
        return true;
    };
    // comma-separated flags
    int64_t acc = 0;
    size_t start = 0;
    bool any = false;
    while (start <= str.size())
    {
        size_t comma = str.find(u',', start);
        std::u16string part = str.substr(start, comma == std::u16string::npos ? std::u16string::npos : comma - start);
        while (!part.empty() && part.front() == u' ') part.erase(0, 1);
        while (!part.empty() && part.back() == u' ') part.pop_back();
        bool found = false;
        for (auto& e : m.entries) if (eqs(part, e.name)) { acc |= e.value; found = true; break; }
        if (!found)
        {
            // numeric
            if (!part.empty() && (Char::IsDigit(part[0]) || part[0] == u'-'))
            {
                acc |= std::stoll(std::string(part.begin(), part.end()));
                found = true;
            }
        }
        if (!found) return false;
        any = true;
        if (comma == std::u16string::npos) break;
        start = comma + 1;
    }
    out = acc;
    return any;
}

static ::System::Object* g_smallInts[272];
::System::Object* box_small_int(int32_t v)
{
    auto& slot = g_smallInts[v + 16];
    if (!slot) { slot = gcnew<Boxed<int32_t>>(v); gc_pin(slot); }
    return slot;
}

} // namespace sys

namespace sys {
Delegate* Delegate::Combine(Delegate* a, Delegate* b)
{
    if (!a) return b;
    if (!b) return a;
    std::vector<Delegate*> items;
    auto add = [&](Delegate* d) {
        if (d->invocationList) for (int32_t i = 0; i < d->invocationList->length; i++) items.push_back(d->invocationList->data[i]);
        else items.push_back(d);
    };
    add(a); add(b);
    // Clone a's concrete type is not possible generically; build a multicast wrapper by
    // re-using the first single delegate's type: we allocate a shallow copy holder.
    auto* list = ::System::Array<Delegate*>::New((int32_t)items.size());
    for (size_t i = 0; i < items.size(); i++) list->data[i] = items[i];
    Delegate* holder = a->CloneMulticast();
    holder->invocationList = list;
    return holder;
}
Delegate* Delegate::Remove(Delegate* a, Delegate* b)
{
    if (!a) return nullptr;
    if (!b) return a;
    std::vector<Delegate*> items;
    if (a->invocationList) for (int32_t i = 0; i < a->invocationList->length; i++) items.push_back(a->invocationList->data[i]);
    else items.push_back(a);
    std::vector<Delegate*> rem;
    if (b->invocationList) for (int32_t i = 0; i < b->invocationList->length; i++) rem.push_back(b->invocationList->data[i]);
    else rem.push_back(b);
    // remove last occurrence of the sequence rem
    for (int start = (int)items.size() - (int)rem.size(); start >= 0; start--)
    {
        bool match = true;
        for (size_t j = 0; j < rem.size(); j++)
            if (!(items[start + j] == rem[j] || items[start + j]->SameAs(rem[j]))) { match = false; break; }
        if (match)
        {
            items.erase(items.begin() + start, items.begin() + start + rem.size());
            break;
        }
    }
    if (items.empty()) return nullptr;
    if (items.size() == 1) return items[0];
    auto* list = ::System::Array<Delegate*>::New((int32_t)items.size());
    for (size_t i = 0; i < items.size(); i++) list->data[i] = items[i];
    Delegate* holder = items[0]->CloneMulticast();
    holder->invocationList = list;
    return holder;
}
}

namespace System {

String* Object::ToString() { return ::sys::demangle_name(typeid(*this).name(), true); }

String* String::Alloc(int32_t len)
{
    size_t hdr = (sizeof(String) + 7) & ~(size_t)7;
    void* mem = ::sys::gc_alloc_atomic(hdr + sizeof(char16_t) * ((size_t)len + 1));
    if (::sys::g_gc_census) ::sys::gc_tag(mem, "String");
    String* s = new (mem) String();
    s->length = len;
    s->data = reinterpret_cast<char16_t*>((uint8_t*)mem + hdr);
    return s;
}
String* String::Lit(const char16_t* p, size_t len)
{
    String* s = Alloc((int32_t)len);
    memcpy(s->data, p, len * 2);
    ::sys::gc_pin(s);
    return s;
}
String* String::FromU16(const char16_t* p, size_t len)
{
    String* s = Alloc((int32_t)len);
    if (len) memcpy(s->data, p, len * 2);
    return s;
}
String* String::FromUtf8(const char* p, size_t len)
{
    if (!p) return len == 0 ? Empty() : nullptr;
    if (len == (size_t)-1) len = strlen(p);
    std::u16string out;
    out.reserve(len);
    for (size_t i = 0; i < len;)
    {
        uint8_t c = (uint8_t)p[i];
        uint32_t cp;
        if (c < 0x80) { cp = c; i += 1; }
        else if ((c >> 5) == 6 && i + 1 < len) { cp = ((c & 0x1F) << 6) | (p[i + 1] & 0x3F); i += 2; }
        else if ((c >> 4) == 14 && i + 2 < len) { cp = ((c & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F); i += 3; }
        else if ((c >> 3) == 30 && i + 3 < len) { cp = ((c & 0x07) << 18) | ((p[i + 1] & 0x3F) << 12) | ((p[i + 2] & 0x3F) << 6) | (p[i + 3] & 0x3F); i += 4; }
        else { cp = c; i += 1; }
        if (cp >= 0x10000) { cp -= 0x10000; out.push_back((char16_t)(0xD800 + (cp >> 10))); out.push_back((char16_t)(0xDC00 + (cp & 0x3FF))); }
        else out.push_back((char16_t)cp);
    }
    return FromStd(out);
}
std::string String::ToUtf8() const
{
    std::string o;
    o.reserve(length);
    for (int32_t i = 0; i < length; i++)
    {
        uint32_t c = data[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < length) { c = 0x10000 + ((c - 0xD800) << 10) + (data[i + 1] - 0xDC00); i++; }
        if (c < 0x80) o.push_back((char)c);
        else if (c < 0x800) { o.push_back((char)(0xC0 | (c >> 6))); o.push_back((char)(0x80 | (c & 0x3F))); }
        else if (c < 0x10000) { o.push_back((char)(0xE0 | (c >> 12))); o.push_back((char)(0x80 | ((c >> 6) & 0x3F))); o.push_back((char)(0x80 | (c & 0x3F))); }
        else { o.push_back((char)(0xF0 | (c >> 18))); o.push_back((char)(0x80 | ((c >> 12) & 0x3F))); o.push_back((char)(0x80 | ((c >> 6) & 0x3F))); o.push_back((char)(0x80 | (c & 0x3F))); }
    }
    return o;
}

static String* g_empty = nullptr;
String* String::Empty()
{
    if (!g_empty) { g_empty = Alloc(0); ::sys::gc_pin(g_empty); }
    return g_empty;
}
bool String::op_Equality(String* a, String* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    return a->length == b->length && memcmp(a->data, b->data, (size_t)a->length * 2) == 0;
}
bool String::Equals(Object* o)
{
    auto* s = dynamic_cast<String*>(o);
    return s && op_Equality(this, s);
}
int32_t String::GetHashCode()
{
    uint32_t h = 5381;
    for (int32_t i = 0; i < length; i++) h = ((h << 5) + h) ^ data[i];
    return (int32_t)h;
}
String* String::Concat(String* a, String* b)
{
    if (!a || a->length == 0) return b ? b : Empty();
    if (!b || b->length == 0) return a;
    String* s = Alloc(a->length + b->length);
    memcpy(s->data, a->data, (size_t)a->length * 2);
    memcpy(s->data + a->length, b->data, (size_t)b->length * 2);
    return s;
}
String* String::Concat(Array<Object*>* parts)
{
    String* r = Empty();
    if (parts) for (int32_t i = 0; i < parts->length; i++) r = Concat(r, ::sys::ToStr(parts->data[i]));
    return r;
}
String* String::Join(String* sep, Array<String*>* parts)
{
    std::u16string r;
    for (int32_t i = 0; i < parts->length; i++)
    {
        if (i && sep) r.append(sep->data, sep->length);
        if (parts->data[i]) r.append(parts->data[i]->data, parts->data[i]->length);
    }
    return FromStd(r);
}
int32_t String::CompareTo(String* o)
{
    if (!o) return 1;
    // culture-aware compare approximated with case-insensitive first, then ordinal
    int32_t n = std::min(length, o->length);
    for (int32_t i = 0; i < n; i++)
    {
        char16_t a = Char::ToLower(data[i]), b = Char::ToLower(o->data[i]);
        if (a != b) return a < b ? -1 : 1;
    }
    if (length != o->length) return length < o->length ? -1 : 1;
    for (int32_t i = 0; i < n; i++) if (data[i] != o->data[i]) return data[i] > o->data[i] ? -1 : 1; // lowercase first
    return 0;
}
bool String::StartsWith(String* s) { return s->length <= length && memcmp(data, s->data, (size_t)s->length * 2) == 0; }
bool String::EndsWith(String* s) { return s->length <= length && memcmp(data + length - s->length, s->data, (size_t)s->length * 2) == 0; }
int32_t String::IndexOf(char16_t c, int32_t start)
{
    for (int32_t i = start; i < length; i++) if (data[i] == c) return i;
    return -1;
}
int32_t String::IndexOf(String* s)
{
    if (!s) ::sys::throw_null();
    if (s->length == 0) return 0;
    for (int32_t i = 0; i + s->length <= length; i++)
        if (memcmp(data + i, s->data, (size_t)s->length * 2) == 0) return i;
    return -1;
}
int32_t String::IndexOfAny(Array<char16_t>* chars)
{
    for (int32_t i = 0; i < length; i++)
        for (int32_t j = 0; j < chars->length; j++) if (data[i] == chars->data[j]) return i;
    return -1;
}
int32_t String::LastIndexOf(char16_t c)
{
    for (int32_t i = length - 1; i >= 0; i--) if (data[i] == c) return i;
    return -1;
}
String* String::Insert(int32_t i, String* s)
{
    std::u16string r = ToU16();
    r.insert((size_t)i, s->data, s->length);
    return FromStd(r);
}
String* String::Remove(int32_t start) { return Substring(0, start); }
String* String::Remove(int32_t start, int32_t count)
{
    std::u16string r = ToU16();
    r.erase((size_t)start, (size_t)count);
    return FromStd(r);
}
String* String::Replace(char16_t a, char16_t b)
{
    String* s = FromU16(data, length);
    for (int32_t i = 0; i < length; i++) if (s->data[i] == a) s->data[i] = b;
    return s;
}
String* String::Replace(String* a, String* b)
{
    if (!a || a->length == 0) return this;
    std::u16string r, src = ToU16(), from = a->ToU16(), to = b ? b->ToU16() : u"";
    size_t pos = 0;
    while (true)
    {
        size_t f = src.find(from, pos);
        if (f == std::u16string::npos) { r.append(src, pos, std::u16string::npos); break; }
        r.append(src, pos, f - pos);
        r += to;
        pos = f + from.size();
    }
    return FromStd(r);
}
Array<String*>* String::Split(Array<char16_t>* seps) { return Split(seps, 0); }
Array<String*>* String::Split(Array<char16_t>* seps, int32_t options)
{
    std::vector<String*> parts;
    int32_t start = 0;
    auto isSep = [&](char16_t c) {
        if (!seps || seps->length == 0) return Char::IsWhiteSpace(c);
        for (int32_t j = 0; j < seps->length; j++) if (seps->data[j] == c) return true;
        return false;
    };
    for (int32_t i = 0; i <= length; i++)
    {
        if (i == length || isSep(data[i]))
        {
            if (!(options == 1 && i == start)) parts.push_back(FromU16(data + start, i - start));
            start = i + 1;
        }
    }
    auto* a = Array<String*>::New((int32_t)parts.size());
    for (size_t i = 0; i < parts.size(); i++) a->data[i] = parts[i];
    return a;
}
String* String::Substring(int32_t start)
{
    if (start < 0 || start > length) ::sys::throw_msg("ArgumentOutOfRangeException", "Substring");
    return FromU16(data + start, length - start);
}
String* String::Substring(int32_t start, int32_t len)
{
    if (start < 0 || len < 0 || start + len > length) ::sys::throw_msg("ArgumentOutOfRangeException", "Substring");
    return FromU16(data + start, len);
}
Array<char16_t>* String::ToCharArray()
{
    auto* a = Array<char16_t>::New(length);
    if (length) memcpy(a->data, data, (size_t)length * 2);
    return a;
}
String* String::ToLower() { String* s = FromU16(data, length); for (int32_t i = 0; i < length; i++) s->data[i] = Char::ToLower(s->data[i]); return s; }
String* String::ToUpper() { String* s = FromU16(data, length); for (int32_t i = 0; i < length; i++) s->data[i] = Char::ToUpper(s->data[i]); return s; }
String* String::Trim()
{
    int32_t a = 0, b = length;
    while (a < b && Char::IsWhiteSpace(data[a])) a++;
    while (b > a && Char::IsWhiteSpace(data[b - 1])) b--;
    return (a == 0 && b == length) ? this : FromU16(data + a, b - a);
}
String* String::TrimStart() { int32_t a = 0; while (a < length && Char::IsWhiteSpace(data[a])) a++; return FromU16(data + a, length - a); }
String* String::TrimEnd() { int32_t b = length; while (b > 0 && Char::IsWhiteSpace(data[b - 1])) b--; return FromU16(data, b); }
String* String::PadLeft(int32_t n, char16_t c) { if (length >= n) return this; std::u16string r((size_t)(n - length), c); r.append(data, length); return FromStd(r); }
String* String::PadRight(int32_t n, char16_t c) { if (length >= n) return this; std::u16string r(data, length); r.append((size_t)(n - length), c); return FromStd(r); }

} // namespace System
