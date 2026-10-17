#include "runtime_sys.h"
#include "../platform/platform.h"
#include <cstdio>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <unistd.h>

using namespace System;
using namespace System::IO;

namespace sys {

static bool IsAbsolute(const std::string& p)
{
    if (p.empty()) return false;
    if (p[0] == '/') return true;
    size_t c = p.find(":/");
    return c != std::string::npos && c < 12;
}

std::string resolve_path_utf8(const std::string& in)
{
    std::string p = in;
    std::replace(p.begin(), p.end(), '\\', '/');
    // collapse "//"
    std::string q;
    for (size_t i = 0; i < p.size(); i++)
    {
        if (p[i] == '/' && !q.empty() && q.back() == '/' && !(i >= 2 && p[i - 2] == ':')) continue;
        q += p[i];
    }
    if (IsAbsolute(q)) return q;
    while (q.rfind("./", 0) == 0) q = q.substr(2);
    return plat::content_root() + "/" + q;
}
std::string resolve_path(String* path)
{
    if (!path) throw_msg("ArgumentNullException", "path");
    return resolve_path_utf8(path->ToUtf8());
}

bool read_file(const std::string& nativePath, std::vector<uint8_t>& out)
{
    plat::loading_tick(nativePath.c_str());
    FILE* f = fopen(nativePath.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    size_t got = n > 0 ? fread(out.data(), 1, (size_t)n, f) : 0;
    fclose(f);
    out.resize(got);
    return true;
}

std::u16string decode_text(const std::vector<uint8_t>& b)
{
    // BOM detection: UTF-8, UTF-16 LE/BE; default UTF-8
    if (b.size() >= 2 && b[0] == 0xFF && b[1] == 0xFE)
    {
        std::u16string r;
        for (size_t i = 2; i + 1 < b.size(); i += 2) r.push_back((char16_t)(b[i] | (b[i + 1] << 8)));
        return r;
    }
    if (b.size() >= 2 && b[0] == 0xFE && b[1] == 0xFF)
    {
        std::u16string r;
        for (size_t i = 2; i + 1 < b.size(); i += 2) r.push_back((char16_t)((b[i] << 8) | b[i + 1]));
        return r;
    }
    size_t start = (b.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) ? 3 : 0;
    String* s = String::FromUtf8((const char*)b.data() + start, b.size() - start);
    return s->ToU16();
}

std::u16string read_text_file(const std::string& nativePath)
{
    std::vector<uint8_t> b;
    if (!read_file(nativePath, b)) throw gcnew<FileNotFoundException>(String::FromUtf8(("Could not find file '" + nativePath + "'.").c_str()));
    return decode_text(b);
}

static void MkdirP(const std::string& path)
{
    std::string cur;
    for (size_t i = 0; i < path.size(); i++)
    {
        cur += path[i];
        if ((path[i] == '/' && i > 0 && path[i - 1] != ':') || i == path.size() - 1)
        {
            std::string d = cur;
            if (d.size() > 1 && d.back() == '/') d.pop_back();
            if (d.empty() || d.back() == ':') continue;
            mkdir(d.c_str(), 0777);
        }
    }
}

static std::vector<std::string> ListDir(const std::string& native, bool dirs)
{
    std::vector<std::string> r;
    DIR* d = opendir(native.c_str());
    if (!d) return r;
    while (dirent* e = readdir(d))
    {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        struct stat st;
        std::string full = native + "/" + n;
        bool isDir = stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        if (isDir == dirs) r.push_back(n);
    }
    closedir(d);
    // Windows/NTFS enumeration order is case-insensitive alphabetical
    std::sort(r.begin(), r.end(), [](const std::string& a, const std::string& b) {
        size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; i++)
        {
            int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);
            if (x != y) return x < y;
        }
        return a.size() < b.size();
    });
    return r;
}

static bool MatchPattern(const std::string& name, const std::string& pat)
{
    // simple * and ? wildcard, case-insensitive
    size_t n = 0, p = 0, star = std::string::npos, mark = 0;
    while (n < name.size())
    {
        if (p < pat.size() && (pat[p] == '?' || tolower((unsigned char)pat[p]) == tolower((unsigned char)name[n]))) { n++; p++; }
        else if (p < pat.size() && pat[p] == '*') { star = p++; mark = n; }
        else if (star != std::string::npos) { p = star + 1; n = ++mark; }
        else return false;
    }
    while (p < pat.size() && pat[p] == '*') p++;
    return p == pat.size();
}

static void Collect(const std::string& userPath, const std::string& native, const std::string& pat, bool recurse, std::vector<String*>& out)
{
    std::string up = userPath;
    std::replace(up.begin(), up.end(), '\\', '/');
    if (!up.empty() && up.back() == '/') up.pop_back();
    std::string np = native;
    if (!np.empty() && np.back() == '/') np.pop_back();
    for (auto& f : ListDir(np, false))
        if (MatchPattern(f, pat)) out.push_back(String::FromUtf8((up + "/" + f).c_str()));
    if (recurse)
        for (auto& d : ListDir(np, true)) Collect(up + "/" + d, np + "/" + d, pat, true, out);
}

template<class V> static Array<String*>* ToArr(const V& v)
{
    auto* a = Array<String*>::New((int32_t)v.size());
    for (size_t i = 0; i < v.size(); i++) a->data[i] = v[i];
    return a;
}

} // namespace sys

namespace System { namespace IO {

std::vector<uint8_t> Stream::ReadAllBytesNative()
{
    std::vector<uint8_t> r;
    auto* buf = Array<uint8_t>::New(65536);
    while (true)
    {
        int32_t n = Read(buf, 0, buf->length);
        if (n <= 0) break;
        r.insert(r.end(), buf->data, buf->data + n);
    }
    return r;
}

int32_t MemoryStream::Read(Array<uint8_t>* b, int32_t off, int32_t count)
{
    int64_t avail = (int64_t)buf.size() - pos;
    int32_t n = (int32_t)std::max<int64_t>(0, std::min<int64_t>(avail, count));
    if (n) memcpy(b->data + off, buf.data() + pos, (size_t)n);
    pos += n;
    return n;
}
void MemoryStream::Write(Array<uint8_t>* b, int32_t off, int32_t count)
{
    if ((size_t)(pos + count) > buf.size()) buf.resize((size_t)(pos + count));
    memcpy(buf.data() + pos, b->data + off, (size_t)count);
    pos += count;
}
int64_t MemoryStream::Seek(int64_t off, SeekOrigin o)
{
    pos = o == SeekOrigin::Begin ? off : (o == SeekOrigin::Current ? pos + off : (int64_t)buf.size() + off);
    return pos;
}
Array<uint8_t>* MemoryStream::ToArray()
{
    auto* a = Array<uint8_t>::New((int32_t)buf.size());
    if (!buf.empty()) memcpy(a->data, buf.data(), buf.size());
    return a;
}

FileStream::FileStream(String* p, FileMode mode) : FileStream(p, mode, mode == FileMode::Open ? FileAccess::Read : FileAccess::ReadWrite, FileShare::None) {}
FileStream::FileStream(String* p, FileMode mode, FileAccess access) : FileStream(p, mode, access, FileShare::None) {}
FileStream::FileStream(String* p, FileMode mode, FileAccess access, FileShare)
{
    path = ::sys::resolve_path(p);
    plat::loading_tick(path.c_str());
    const char* m = "rb";
    switch (mode)
    {
    case FileMode::Create: case FileMode::Truncate: case FileMode::CreateNew: m = "wb"; writing = true; break;
    case FileMode::Append: m = "ab"; writing = true; break;
    case FileMode::OpenOrCreate: m = access == FileAccess::Read ? "rb" : "r+b"; writing = access != FileAccess::Read; break;
    case FileMode::Open: m = access == FileAccess::Read ? "rb" : "r+b"; writing = access != FileAccess::Read; break;
    }
    f = fopen(path.c_str(), m);
    if (!f && mode == FileMode::OpenOrCreate) f = fopen(path.c_str(), "w+b");
    if (!f)
    {
        if (!writing) throw ::sys::gcnew<FileNotFoundException>(String::FromUtf8(("Could not find file '" + path + "'.").c_str()));
        throw ::sys::gcnew<IOException>(String::FromUtf8(("Could not open file '" + path + "'.").c_str()));
    }
}
int32_t FileStream::Read(Array<uint8_t>* b, int32_t off, int32_t count) { return f ? (int32_t)fread(b->data + off, 1, (size_t)count, f) : 0; }
void FileStream::Write(Array<uint8_t>* b, int32_t off, int32_t count)
{
    if (!f) ::sys::throw_msg("IOException", "closed");
    if (fwrite(b->data + off, 1, (size_t)count, f) != (size_t)count) ::sys::throw_msg("IOException", "write failed");
}
void FileStream::Flush() { if (f) fflush(f); }
void FileStream::Close() { if (f) { fclose(f); f = nullptr; } }
int64_t FileStream::get_Length()
{
    if (!f) return 0;
    long cur = ftell(f); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, cur, SEEK_SET);
    return n;
}
int64_t FileStream::get_Position() { return f ? ftell(f) : 0; }
void FileStream::set_Position(int64_t p) { if (f) fseek(f, (long)p, SEEK_SET); }
int64_t FileStream::Seek(int64_t off, SeekOrigin o)
{
    if (!f) return 0;
    fseek(f, (long)off, o == SeekOrigin::Begin ? SEEK_SET : (o == SeekOrigin::Current ? SEEK_CUR : SEEK_END));
    return ftell(f);
}

StreamReader::StreamReader(String* p) { text = ::sys::read_text_file(::sys::resolve_path(p)); }
StreamReader::StreamReader(Stream* s)
{
    auto b = s->ReadAllBytesNative();
    text = ::sys::decode_text(b);
}
String* StreamReader::ReadToEnd() { auto r = String::FromU16(text.data() + pos, text.size() - pos); pos = text.size(); return r; }
String* StreamReader::ReadLine()
{
    if (pos >= text.size()) return nullptr;
    size_t e = pos;
    while (e < text.size() && text[e] != u'\n' && text[e] != u'\r') e++;
    String* r = String::FromU16(text.data() + pos, e - pos);
    if (e < text.size() && text[e] == u'\r') e++;
    if (e < text.size() && text[e] == u'\n') e++;
    pos = e;
    return r;
}
String* StringReader::ReadLine()
{
    if (pos >= text.size()) return nullptr;
    size_t e = pos;
    while (e < text.size() && text[e] != u'\n' && text[e] != u'\r') e++;
    String* r = String::FromU16(text.data() + pos, e - pos);
    if (e < text.size() && text[e] == u'\r') e++;
    if (e < text.size() && text[e] == u'\n') e++;
    pos = e;
    return r;
}

StreamWriter::StreamWriter(String* p, bool append)
{
    std::string path = ::sys::resolve_path(p);
    f = fopen(path.c_str(), append ? "ab" : "wb");
    if (!f) throw ::sys::gcnew<IOException>(String::FromUtf8(("Could not open '" + path + "' for writing").c_str()));
}
StreamWriter::StreamWriter(Stream* s, ::System::Text::Encoding* enc) : stream(s) {}
void StreamWriter::Write(String* s)
{
    if (!s) return;
    std::string u = s->ToUtf8();
    if (f) fwrite(u.data(), 1, u.size(), f);
    else if (stream)
    {
        auto* a = Array<uint8_t>::New((int32_t)u.size());
        memcpy(a->data, u.data(), u.size());
        stream->Write(a, 0, a->length);
    }
}
void StreamWriter::Flush() { if (f) fflush(f); if (stream) stream->Flush(); }
void StreamWriter::Close() { if (f) { fclose(f); f = nullptr; } }

bool File::Exists(String* p)
{
    if (!p) return false;
    struct stat st;
    return stat(::sys::resolve_path(p).c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}
void File::Delete(String* p) { remove(::sys::resolve_path(p).c_str()); }
String* File::ReadAllText(String* p) { return String::FromStd(::sys::read_text_file(::sys::resolve_path(p))); }
Array<String*>* File::ReadAllLines(String* p)
{
    auto* r = ::sys::gcnew<StreamReader>(p);
    std::vector<String*> lines;
    while (String* l = r->ReadLine()) lines.push_back(l);
    return ::sys::ToArr(lines);
}
Array<uint8_t>* File::ReadAllBytes(String* p)
{
    std::vector<uint8_t> b;
    if (!::sys::read_file(::sys::resolve_path(p), b)) ::sys::throw_msg("FileNotFoundException", p->ToUtf8().c_str());
    auto* a = Array<uint8_t>::New((int32_t)b.size());
    if (!b.empty()) memcpy(a->data, b.data(), b.size());
    return a;
}
void File::WriteAllText(String* p, String* text)
{
    FILE* f = fopen(::sys::resolve_path(p).c_str(), "wb");
    if (!f) ::sys::throw_msg("IOException", "write");
    std::string u = text ? text->ToUtf8() : "";
    fwrite(u.data(), 1, u.size(), f);
    fclose(f);
}
void File::Copy(String* a, String* b)
{
    std::vector<uint8_t> d;
    if (!::sys::read_file(::sys::resolve_path(a), d)) ::sys::throw_msg("FileNotFoundException", "copy");
    FILE* f = fopen(::sys::resolve_path(b).c_str(), "wb");
    if (!f) ::sys::throw_msg("IOException", "copy");
    fwrite(d.data(), 1, d.size(), f);
    fclose(f);
}

bool Directory::Exists(String* p)
{
    if (!p) return false;
    struct stat st;
    std::string n = ::sys::resolve_path(p);
    if (!n.empty() && n.back() == '/') n.pop_back();
    return stat(n.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
DirectoryInfo* Directory::CreateDirectory(String* p) { ::sys::MkdirP(::sys::resolve_path(p)); return ::sys::gcnew<DirectoryInfo>(); }
Array<String*>* Directory::GetDirectories(String* p)
{
    std::string up = p->ToUtf8();
    std::replace(up.begin(), up.end(), '\\', '/');
    if (!up.empty() && up.back() == '/') up.pop_back();
    std::vector<String*> out;
    for (auto& d : ::sys::ListDir(::sys::resolve_path(p), true)) out.push_back(String::FromUtf8((up + "/" + d).c_str()));
    return ::sys::ToArr(out);
}
Array<String*>* Directory::GetFiles(String* p) { return GetFiles(p, TF_S(u"*"), SearchOption::TopDirectoryOnly); }
Array<String*>* Directory::GetFiles(String* p, String* pat) { return GetFiles(p, pat, SearchOption::TopDirectoryOnly); }
Array<String*>* Directory::GetFiles(String* p, String* pat, SearchOption opt)
{
    std::vector<String*> out;
    ::sys::Collect(p->ToUtf8(), ::sys::resolve_path(p), pat->ToUtf8(), opt == SearchOption::AllDirectories, out);
    return ::sys::ToArr(out);
}
Collections::Generic::IEnumerable<String*>* Directory::EnumerateDirectories(String* p) { return GetDirectories(p); }
Collections::Generic::IEnumerable<String*>* Directory::EnumerateFiles(String* p) { return GetFiles(p); }
Collections::Generic::IEnumerable<String*>* Directory::EnumerateFiles(String* p, String* pat) { return GetFiles(p, pat); }
Collections::Generic::IEnumerable<String*>* Directory::EnumerateFiles(String* p, String* pat, SearchOption opt) { return GetFiles(p, pat, opt); }

String* Path::Combine(String* a, String* b)
{
    if (!a || a->length == 0) return b;
    if (!b || b->length == 0) return a;
    std::string bs = b->ToUtf8();
    if (bs[0] == '/' || bs.find(":/") != std::string::npos) return b;
    char16_t last = a->data[a->length - 1];
    if (last == u'/' || last == u'\\') return String::Concat(a, b);
    return String::Concat(String::Concat(a, TF_S(u"/")), b);
}
static int32_t LastSep(String* p)
{
    for (int32_t i = p->length - 1; i >= 0; i--) if (p->data[i] == u'/' || p->data[i] == u'\\') return i;
    return -1;
}
String* Path::GetExtension(String* p)
{
    if (!p) return nullptr;
    for (int32_t i = p->length - 1; i >= 0; i--)
    {
        if (p->data[i] == u'.') return i == p->length - 1 ? String::Empty() : p->Substring(i);
        if (p->data[i] == u'/' || p->data[i] == u'\\') break;
    }
    return String::Empty();
}
String* Path::GetFileName(String* p) { if (!p) return nullptr; return p->Substring(LastSep(p) + 1); }
String* Path::GetFileNameWithoutExtension(String* p)
{
    String* f = GetFileName(p);
    int32_t d = f->LastIndexOf(u'.');
    return d < 0 ? f : f->Substring(0, d);
}
String* Path::GetDirectoryName(String* p) { if (!p) return nullptr; int32_t s = LastSep(p); return s < 0 ? String::Empty() : p->Substring(0, s); }
String* Path::ChangeExtension(String* p, String* ext)
{
    String* e = GetExtension(p);
    String* base = e->length ? p->Substring(0, p->length - e->length) : p;
    if (!ext) return base;
    if (ext->length && ext->data[0] != u'.') ext = String::Concat(TF_S(u"."), ext);
    return String::Concat(base, ext);
}

}} // namespace System::IO
