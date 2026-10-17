// ContentManager: SpriteFont XNB loader and Effect stubs (shaders are emulated in graphics.cpp).
// All file data is little-endian and is read with explicit byte assembly (Wii U is big-endian).
#include "xna/xna.h"
#include <algorithm>
#include <cstring>
#include <numeric>

namespace Microsoft { namespace Xna { namespace Framework { namespace Content {

using namespace ::Microsoft::Xna::Framework::Graphics;

namespace {

[[noreturn]] void fail(const std::string& m)
{
    ::sys::throw_msg("Microsoft.Xna.Framework.Content.ContentLoadException", m.c_str());
}

struct Reader
{
    const uint8_t* d; size_t n; size_t p;
    void need(size_t k) { if (p + k > n) fail("unexpected end of XNB data"); }
    uint8_t u8() { need(1); return d[p++]; }
    uint16_t u16() { need(2); uint16_t v = (uint16_t)(d[p] | (d[p + 1] << 8)); p += 2; return v; }
    uint32_t u32() { need(4); uint32_t v = (uint32_t)d[p] | ((uint32_t)d[p + 1] << 8) | ((uint32_t)d[p + 2] << 16) | ((uint32_t)d[p + 3] << 24); p += 4; return v; }
    uint64_t u64() { uint64_t lo = u32(); uint64_t hi = u32(); return lo | (hi << 32); }
    int32_t i32() { return (int32_t)u32(); }
    float f32() { uint32_t b = u32(); float f; memcpy(&f, &b, 4); return f; }
    uint32_t v7()
    {
        uint32_t r = 0; int s = 0;
        for (;;) { uint8_t b = u8(); r |= (uint32_t)(b & 0x7F) << s; s += 7; if (!(b & 0x80)) return r; if (s > 35) fail("bad 7-bit int"); }
    }
    std::string str() { uint32_t len = v7(); need(len); std::string s((const char*)d + p, len); p += len; return s; }
    // .NET BinaryReader.ReadChar (UTF-8)
    uint32_t chr()
    {
        uint8_t b0 = u8();
        if (b0 < 0x80) return b0;
        int extra = b0 < 0xE0 ? 1 : b0 < 0xF0 ? 2 : 3;
        uint32_t c = b0 & (extra == 1 ? 0x1F : extra == 2 ? 0x0F : 0x07);
        for (int i = 0; i < extra; i++) c = (c << 6) | (u8() & 0x3F);
        return c;
    }
    const uint8_t* bytes(size_t k) { need(k); const uint8_t* r = d + p; p += k; return r; }
};

std::vector<uint8_t> lz4_block(const uint8_t* src, size_t srcLen, size_t outSize)
{
    std::vector<uint8_t> out;
    out.reserve(outSize);
    size_t p = 0;
    while (p < srcLen)
    {
        uint8_t tok = src[p++];
        size_t ln = tok >> 4;
        if (ln == 15) { uint8_t b; do { if (p >= srcLen) fail("lz4"); b = src[p++]; ln += b; } while (b == 255); }
        if (p + ln > srcLen) fail("lz4 literal overrun");
        out.insert(out.end(), src + p, src + p + ln);
        p += ln;
        if (p >= srcLen) break;
        if (p + 2 > srcLen) fail("lz4");
        size_t off = src[p] | (src[p + 1] << 8); p += 2;
        size_t ml = tok & 15;
        if (ml == 15) { uint8_t b; do { if (p >= srcLen) fail("lz4"); b = src[p++]; ml += b; } while (b == 255); }
        ml += 4;
        if (off == 0 || off > out.size()) fail("lz4 bad offset");
        for (size_t i = 0; i < ml; i++) out.push_back(out[out.size() - off]);
    }
    if (out.size() > outSize) out.resize(outSize);
    return out;
}

inline void put(uint8_t* o, uint8_t r, uint8_t g, uint8_t b, uint8_t a) { o[0] = r; o[1] = g; o[2] = b; o[3] = a; }

void rgb565(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b)
{
    uint32_t R = (c >> 11) & 31, G = (c >> 5) & 63, B = c & 31;
    r = (uint8_t)(R << 3 | R >> 2); g = (uint8_t)(G << 2 | G >> 4); b = (uint8_t)(B << 3 | B >> 2);
}

void decode_dxt(const uint8_t* data, size_t len, int w, int h, int kind, uint8_t* out)
{
    size_t bs = kind == 1 ? 8 : 16;
    size_t p = 0;
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4)
        {
            if (p + bs > len) fail("DXT data truncated");
            const uint8_t* blk = data + p; p += bs;
            const uint8_t* cb = blk + bs - 8;
            uint16_t c0 = (uint16_t)(cb[0] | (cb[1] << 8)), c1 = (uint16_t)(cb[2] | (cb[3] << 8));
            uint32_t idx = (uint32_t)cb[4] | ((uint32_t)cb[5] << 8) | ((uint32_t)cb[6] << 16) | ((uint32_t)cb[7] << 24);
            uint8_t col[4][4];
            rgb565(c0, col[0][0], col[0][1], col[0][2]); col[0][3] = 255;
            rgb565(c1, col[1][0], col[1][1], col[1][2]); col[1][3] = 255;
            if (c0 > c1 || kind != 1)
            {
                for (int i = 0; i < 3; i++)
                {
                    col[2][i] = (uint8_t)((2 * col[0][i] + col[1][i] + 1) / 3);
                    col[3][i] = (uint8_t)((col[0][i] + 2 * col[1][i] + 1) / 3);
                }
                col[2][3] = col[3][3] = 255;
            }
            else
            {
                for (int i = 0; i < 3; i++) col[2][i] = (uint8_t)((col[0][i] + col[1][i]) / 2);
                col[2][3] = 255;
                col[3][0] = col[3][1] = col[3][2] = col[3][3] = 0;
            }
            uint8_t alpha[16];
            bool hasAlpha = false;
            if (kind == 3)
            {
                hasAlpha = true;
                uint64_t a64 = 0;
                for (int i = 7; i >= 0; i--) a64 = (a64 << 8) | blk[i];
                for (int i = 0; i < 16; i++) alpha[i] = (uint8_t)(((a64 >> (4 * i)) & 15) * 17);
            }
            else if (kind == 5)
            {
                hasAlpha = true;
                uint8_t a0 = blk[0], a1 = blk[1];
                uint64_t bits = 0;
                for (int i = 7; i >= 2; i--) bits = (bits << 8) | blk[i];
                uint8_t tab[8] = { a0, a1 };
                if (a0 > a1) for (int i = 0; i < 6; i++) tab[2 + i] = (uint8_t)(((6 - i) * a0 + (1 + i) * a1 + 3) / 7);
                else
                {
                    for (int i = 0; i < 4; i++) tab[2 + i] = (uint8_t)(((4 - i) * a0 + (1 + i) * a1 + 2) / 5);
                    tab[6] = 0; tab[7] = 255;
                }
                for (int i = 0; i < 16; i++) alpha[i] = tab[(bits >> (3 * i)) & 7];
            }
            for (int i = 0; i < 16; i++)
            {
                int x = bx + (i & 3), y = by + (i >> 2);
                if (x >= w || y >= h) continue;
                const uint8_t* c = col[(idx >> (2 * i)) & 3];
                uint8_t* o = out + ((size_t)y * w + x) * 4;
                put(o, c[0], c[1], c[2], hasAlpha ? alpha[i] : c[3]);
            }
        }
}

// Decodes an XNA SurfaceFormat into RGBA8 bytes.
std::vector<uint32_t> decode_surface(int fmt, int w, int h, const uint8_t* data, size_t len)
{
    std::vector<uint32_t> px((size_t)w * h, 0u);
    uint8_t* out = reinterpret_cast<uint8_t*>(px.data());
    size_t n = (size_t)w * h;
    switch (fmt)
    {
    case 0: // Color: bytes R,G,B,A
        if (len < n * 4) fail("texture data truncated");
        memcpy(out, data, n * 4);
        break;
    case 1: case 2: case 3:
        if (len < n * 2) fail("texture data truncated");
        for (size_t i = 0; i < n; i++)
        {
            uint16_t c = (uint16_t)(data[2 * i] | (data[2 * i + 1] << 8));
            uint8_t* o = out + i * 4;
            if (fmt == 1) { uint8_t r, g, b; rgb565(c, r, g, b); put(o, r, g, b, 255); }
            else if (fmt == 2)
                put(o, (uint8_t)(((c >> 10) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 31) * 255 / 31), (uint8_t)((c & 31) * 255 / 31), (c >> 15) ? 255 : 0);
            else
                put(o, (uint8_t)(((c >> 8) & 15) * 17), (uint8_t)(((c >> 4) & 15) * 17), (uint8_t)((c & 15) * 17), (uint8_t)((c >> 12) * 17));
        }
        break;
    case 4: decode_dxt(data, len, w, h, 1, out); break;
    case 5: decode_dxt(data, len, w, h, 3, out); break;
    case 6: decode_dxt(data, len, w, h, 5, out); break;
    case 12: // Alpha8
        if (len < n) fail("texture data truncated");
        for (size_t i = 0; i < n; i++) put(out + i * 4, 255, 255, 255, data[i]);
        break;
    default:
        fail("unsupported SurfaceFormat " + std::to_string(fmt));
    }
    return px;
}

std::string content_path(String* root, String* name, const char* ext)
{
    std::string r = root ? root->ToUtf8() : std::string("Content");
    std::string n = name ? name->ToUtf8() : std::string();
    std::replace(r.begin(), r.end(), '\\', '/');
    std::replace(n.begin(), n.end(), '\\', '/');
    std::string full = r;
    if (!full.empty() && full.back() != '/') full += '/';
    full += n;
    full += ext;
    return full;
}

} // namespace

SpriteFont* ContentManager::LoadFont(String* name)
{
    std::string rel = content_path(RootDirectory, name, ".xnb");
    std::string native = ::sys::resolve_path(String::FromUtf8(rel.c_str(), rel.size()));
    std::vector<uint8_t> file;
    if (!::sys::read_file(native, file)) fail("Could not load asset '" + rel + "' (" + native + ")");
    if (file.size() < 10 || file[0] != 'X' || file[1] != 'N' || file[2] != 'B') fail("'" + rel + "' is not an XNB file");
    uint8_t flags = file[5];
    uint32_t size = (uint32_t)file[6] | ((uint32_t)file[7] << 8) | ((uint32_t)file[8] << 16) | ((uint32_t)file[9] << 24);
    if (size > file.size()) size = (uint32_t)file.size();
    std::vector<uint8_t> body;
    if (flags & 0x80) fail("'" + rel + "': LZX-compressed XNB not supported");
    if (flags & 0x40)
    {
        if (file.size() < 14) fail("truncated XNB");
        uint32_t dsize = (uint32_t)file[10] | ((uint32_t)file[11] << 8) | ((uint32_t)file[12] << 16) | ((uint32_t)file[13] << 24);
        body = lz4_block(file.data() + 14, size - 14, dsize);
    }
    else body.assign(file.begin() + 10, file.begin() + size);

    Reader r{ body.data(), body.size(), 0 };
    uint32_t nReaders = r.v7();
    std::vector<std::string> readers;
    for (uint32_t i = 0; i < nReaders; i++) { readers.push_back(r.str()); r.i32(); }
    r.v7(); // shared resources
    uint32_t tid = r.v7();
    if (tid == 0 || tid > readers.size() || readers[tid - 1].find("SpriteFontReader") == std::string::npos)
        fail("'" + rel + "' is not a SpriteFont");

    // Texture2D
    r.v7();
    int32_t fmt = r.i32();
    int32_t w = (int32_t)r.u32(), h = (int32_t)r.u32();
    uint32_t mips = r.u32();
    uint32_t dlen = r.u32();
    const uint8_t* tdata = r.bytes(dlen);
    std::vector<uint32_t> px = decode_surface(fmt, w, h, tdata, dlen);
    for (uint32_t i = 1; i < mips; i++) r.bytes(r.u32());

    auto rects = [&r]() {
        r.v7();
        uint32_t n = r.u32();
        std::vector<Rectangle> v(n);
        for (uint32_t i = 0; i < n; i++) { v[i].X = r.i32(); v[i].Y = r.i32(); v[i].Width = r.i32(); v[i].Height = r.i32(); }
        return v;
    };
    std::vector<Rectangle> glyphs = rects();
    std::vector<Rectangle> crops = rects();
    r.v7();
    uint32_t nc = r.u32();
    std::vector<char16_t> chars(nc);
    for (uint32_t i = 0; i < nc; i++) chars[i] = (char16_t)r.chr();
    int32_t lineSpacing = r.i32();
    float spacing = r.f32();
    r.v7();
    uint32_t nk = r.u32();
    std::vector<Vector3> kern(nk);
    for (uint32_t i = 0; i < nk; i++) { kern[i].X = r.f32(); kern[i].Y = r.f32(); kern[i].Z = r.f32(); }
    int32_t defChar = -1;
    if (r.p < r.n && r.u8()) defChar = (int32_t)r.chr();
    if (glyphs.size() != nc || crops.size() != nc || kern.size() != nc) fail("'" + rel + "': inconsistent glyph tables");

    // sort all tables by character so SpriteFont::Index can binary-search
    std::vector<uint32_t> ord(nc);
    std::iota(ord.begin(), ord.end(), 0u);
    std::stable_sort(ord.begin(), ord.end(), [&chars](uint32_t a, uint32_t b) { return chars[a] < chars[b]; });

    SpriteFont* f = ::sys::gcnew<SpriteFont>();
    f->texture = Texture2D::FromPixels(w, h, px.data(), false);
    f->chars.resize(nc); f->glyphs.resize(nc); f->cropping.resize(nc); f->kerning.resize(nc);
    for (uint32_t i = 0; i < nc; i++)
    {
        f->chars[i] = chars[ord[i]]; f->glyphs[i] = glyphs[ord[i]];
        f->cropping[i] = crops[ord[i]]; f->kerning[i] = kern[ord[i]];
    }
    f->lineSpacing = lineSpacing;
    f->spacing = spacing;
    f->defaultChar = defChar;
    return f;
}

Effect* ContentManager::LoadEffect(String* name)
{
    std::string n = name ? name->ToUtf8() : std::string();
    Effect* e = ::sys::gcnew<Effect>();
    auto param = [e](const char* pn) { e->parameters->get_Item(String::FromUtf8(pn)); };
    if (n.find("LightingEffect") != std::string::npos)
    {
        e->kind = EffectKind::Lighting;
        param("Alpha");
    }
    else if (n.find("ScreenEffects") != std::string::npos)
    {
        e->kind = EffectKind::ScreenWave;
        param("Counter"); param("Amplitude"); param("Frequency");
    }
    return e;
}

}}}} // namespace Microsoft::Xna::Framework::Content
