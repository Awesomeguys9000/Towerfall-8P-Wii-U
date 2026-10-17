// System.IO subset. Relative paths are resolved against the game content root.
#pragma once
#include "system.h"
#include <cstdio>

namespace sys {
// platform hooks (platform_*.cpp)
std::string resolve_path(::System::String* path);   // to native UTF-8 path
std::string resolve_path_utf8(const std::string& path);
bool read_file(const std::string& nativePath, std::vector<uint8_t>& out);
std::u16string read_text_file(const std::string& nativePath);
std::u16string decode_text(const std::vector<uint8_t>& bytes);
}

namespace System { namespace IO {

enum class FileMode : int32_t { CreateNew = 1, Create = 2, Open = 3, OpenOrCreate = 4, Truncate = 5, Append = 6 };
enum class FileAccess : int32_t { Read = 1, Write = 2, ReadWrite = 3 };
enum class FileShare : int32_t { None = 0, Read = 1, Write = 2, ReadWrite = 3, Delete = 4 };
enum class SearchOption : int32_t { TopDirectoryOnly = 0, AllDirectories = 1 };
enum class SeekOrigin : int32_t { Begin = 0, Current = 1, End = 2 };

struct Stream : Object, IDisposable
{
    virtual int32_t Read(Array<uint8_t>* buf, int32_t off, int32_t count) { return 0; }
    virtual void Write(Array<uint8_t>* buf, int32_t off, int32_t count) {}
    virtual void WriteByte(uint8_t b) { auto* a = Array<uint8_t>::New(1); a->data[0] = b; Write(a, 0, 1); }
    virtual int32_t ReadByte() { auto* a = Array<uint8_t>::New(1); return Read(a, 0, 1) == 1 ? a->data[0] : -1; }
    virtual void Flush() {}
    virtual void Close() {}
    void Dispose() override { Close(); }
    virtual int64_t get_Length() { return 0; }
    virtual int64_t get_Position() { return 0; }
    virtual void set_Position(int64_t p) {}
    virtual int64_t Seek(int64_t off, SeekOrigin o) { return 0; }
    virtual bool get_CanRead() { return true; }
    virtual bool get_CanWrite() { return false; }
    // whole content helper for loaders
    virtual std::vector<uint8_t> ReadAllBytesNative();
};

struct MemoryStream final : Stream
{
    std::vector<uint8_t> buf;
    int64_t pos = 0;
    MemoryStream() {}
    explicit MemoryStream(Array<uint8_t>* data) { buf.assign(data->data, data->data + data->length); }
    int32_t Read(Array<uint8_t>* b, int32_t off, int32_t count) override;
    void Write(Array<uint8_t>* b, int32_t off, int32_t count) override;
    int64_t get_Length() override { return (int64_t)buf.size(); }
    int64_t get_Position() override { return pos; }
    void set_Position(int64_t p) override { pos = p; }
    int64_t Seek(int64_t off, SeekOrigin o) override;
    bool get_CanWrite() override { return true; }
    Array<uint8_t>* ToArray();
    std::vector<uint8_t> ReadAllBytesNative() override { return std::vector<uint8_t>(buf.begin() + pos, buf.end()); }
};

struct FileStream final : Stream, ::sys::Finalizable
{
    FILE* f = nullptr;
    std::string path;
    bool writing = false;
    FileStream(String* path, FileMode mode);
    FileStream(String* path, FileMode mode, FileAccess access);
    FileStream(String* path, FileMode mode, FileAccess access, FileShare share);
    ~FileStream() { Close(); }
    int32_t Read(Array<uint8_t>* b, int32_t off, int32_t count) override;
    void Write(Array<uint8_t>* b, int32_t off, int32_t count) override;
    void Flush() override;
    void Close() override;
    int64_t get_Length() override;
    int64_t get_Position() override;
    void set_Position(int64_t p) override;
    int64_t Seek(int64_t off, SeekOrigin o) override;
    bool get_CanWrite() override { return writing; }
    std::vector<uint8_t> ReadAllBytesNative() override;   // one bulk read (much faster on Wii U SD)
};

struct TextReader : Object, IDisposable
{
    virtual String* ReadToEnd() = 0;
    virtual String* ReadLine() = 0;
    virtual int32_t Peek() = 0;
    virtual int32_t Read() = 0;
    virtual void Close() {}
    void Dispose() override { Close(); }
};
struct StreamReader final : TextReader
{
    std::u16string text;
    size_t pos = 0;
    explicit StreamReader(String* path);
    explicit StreamReader(Stream* s);
    String* ReadToEnd() override;
    String* ReadLine() override;
    int32_t Peek() override { return pos < text.size() ? text[pos] : -1; }
    int32_t Read() override { return pos < text.size() ? text[pos++] : -1; }
    bool get_EndOfStream() { return pos >= text.size(); }
};
struct StringReader final : TextReader
{
    std::u16string text;
    size_t pos = 0;
    explicit StringReader(String* s) { if (s) text = s->ToU16(); }
    String* ReadToEnd() override { auto r = String::FromU16(text.data() + pos, text.size() - pos); pos = text.size(); return r; }
    String* ReadLine() override;
    int32_t Peek() override { return pos < text.size() ? text[pos] : -1; }
    int32_t Read() override { return pos < text.size() ? text[pos++] : -1; }
};

struct TextWriter : Object, IDisposable
{
    virtual void Write(String* s) = 0;
    virtual void Write(char16_t c) { char16_t b[1] = { c }; Write(String::FromU16(b, 1)); }
    virtual void WriteLine(String* s) { Write(s); WriteLine(); }
    virtual void WriteLine() { Write(TF_S(u"\n")); }
    virtual void Flush() {}
    virtual void Close() {}
    void Dispose() override { Close(); }
};
struct StreamWriter final : TextWriter, ::sys::Finalizable
{
    Stream* stream = nullptr;
    FILE* f = nullptr;
    bool utf16 = false;
    StreamWriter(String* path, bool append);
    explicit StreamWriter(String* path) : StreamWriter(path, false) {}
    StreamWriter(Stream* s, ::System::Text::Encoding* enc);
    ~StreamWriter() { if (f) fclose(f); f = nullptr; }
    void Write(String* s) override;
    void Flush() override;
    void Close() override;
    using TextWriter::Write;
    using TextWriter::WriteLine;
};

struct File
{
    static bool Exists(String* path);
    static void Delete(String* path);
    static String* ReadAllText(String* path);
    static Array<String*>* ReadAllLines(String* path);
    static Array<uint8_t>* ReadAllBytes(String* path);
    static void WriteAllText(String* path, String* text);
    static void Copy(String* a, String* b);
    static void Copy(String* a, String* b, bool overwrite) { Copy(a, b); }
};

struct DirectoryInfo final : Object {};
struct Directory
{
    static bool Exists(String* path);
    static DirectoryInfo* CreateDirectory(String* path);
    static Array<String*>* GetDirectories(String* path);
    static Array<String*>* GetFiles(String* path);
    static Array<String*>* GetFiles(String* path, String* pattern);
    static Array<String*>* GetFiles(String* path, String* pattern, SearchOption opt);
    static Collections::Generic::IEnumerable<String*>* EnumerateDirectories(String* path);
    static Collections::Generic::IEnumerable<String*>* EnumerateFiles(String* path);
    static Collections::Generic::IEnumerable<String*>* EnumerateFiles(String* path, String* pattern);
    static Collections::Generic::IEnumerable<String*>* EnumerateFiles(String* path, String* pattern, SearchOption opt);
};

struct Path
{
    static constexpr char16_t DirectorySeparatorChar = u'/';
    static char16_t get_DirectorySeparatorChar() { return u'/'; }
    static String* Combine(String* a, String* b);
    static String* Combine(String* a, String* b, String* c) { return Combine(Combine(a, b), c); }
    static String* Combine(String* a, String* b, String* c, String* d) { return Combine(Combine(Combine(a, b), c), d); }
    static String* GetExtension(String* p);
    static String* GetFileName(String* p);
    static String* GetFileNameWithoutExtension(String* p);
    static String* GetDirectoryName(String* p);
    static String* ChangeExtension(String* p, String* ext);
    static String* GetFullPath(String* p) { return p; }
};

}} // namespace System::IO
