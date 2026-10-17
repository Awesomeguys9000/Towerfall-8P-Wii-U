// Microsoft.Xna.Framework.Graphics on top of SDL2's SDL_Renderer (no shaders).
//
// - Textures are SDL_PIXELFORMAT_RGBA32 (bytes r,g,b,a in memory on every endianness == XNA Color layout).
// - SpriteBatch builds quads on the CPU (XNA/FNA vertex math, transform matrix applied on the CPU) and
//   submits them with SDL_RenderGeometry, one call per run of sprites sharing a texture.
// - The two TowerFall pixel shaders are emulated (see port/assets_research/REPORT.md):
//     Lighting   : every render-target texture drawn in the batch is first darkened in place by
//                  GraphicsDevice::Textures[1] with a reverse-subtract blend (dst - k*L), then drawn.
//     ScreenWave : sprites are drawn as one strip per output row with a horizontally wrapped offset.
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#include "stb_image.h"

#include "xna/xna.h"
#include "platform/platform.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Microsoft { namespace Xna { namespace Framework { namespace Graphics {

// ======================================================================= statics
BlendState* BlendState::AlphaBlend = nullptr;
BlendState* BlendState::Additive = nullptr;
BlendState* BlendState::Opaque = nullptr;
BlendState* BlendState::NonPremultiplied = nullptr;
SamplerState* SamplerState::PointClamp = nullptr;
SamplerState* SamplerState::PointWrap = nullptr;
SamplerState* SamplerState::LinearClamp = nullptr;
SamplerState* SamplerState::LinearWrap = nullptr;
SamplerState* SamplerState::AnisotropicClamp = nullptr;
DepthStencilState* DepthStencilState::None = nullptr;
DepthStencilState* DepthStencilState::Default = nullptr;
DepthStencilState* DepthStencilState::DepthRead = nullptr;
RasterizerState* RasterizerState::CullNone = nullptr;
RasterizerState* RasterizerState::CullClockwise = nullptr;
RasterizerState* RasterizerState::CullCounterClockwise = nullptr;
GraphicsDevice* GraphicsDevice::Instance = nullptr;

namespace {

// SDL operation that yields dst - src (XNA ReverseSubtract) and src - dst (XNA Subtract);
// probed at init because SDL's header comments and backends disagree.
SDL_BlendOperation g_opDstMinusSrc = SDL_BLENDOPERATION_REV_SUBTRACT;
SDL_BlendOperation g_opSrcMinusDst = SDL_BLENDOPERATION_SUBTRACT;
bool g_customBlendWarned = false;

constexpr Uint32 kFormat = SDL_PIXELFORMAT_RGBA32;

SDL_Renderer* R() { return plat::renderer(); }

void log_msg(const char* fmt, const char* a = "", const char* b = "")
{
    char buf[512];
    snprintf(buf, sizeof buf, fmt, a, b);
    plat::log(buf);
}

SDL_Texture* create_sdl_texture(int w, int h, bool target)
{
    SDL_Renderer* r = R();
    if (!r) ::sys::throw_msg("InvalidOperationException", "Texture created before the renderer exists");
    if (w <= 0 || h <= 0) ::sys::throw_msg("ArgumentOutOfRangeException", "Texture size must be positive");
    SDL_Texture* t = SDL_CreateTexture(r, kFormat, target ? SDL_TEXTUREACCESS_TARGET : SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t)
    {
        std::string msg = std::string("SDL_CreateTexture failed: ") + SDL_GetError();
        ::sys::throw_msg("InvalidOperationException", msg.c_str());
    }
    SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
    SDL_SetTextureColorMod(t, 255, 255, 255);
    SDL_SetTextureAlphaMod(t, 255);
    return t;
}

// Restore whatever the GraphicsDevice thinks is bound (after internal target switches).
void restore_device_target(SDL_Texture* fallback)
{
    if (GraphicsDevice::Instance) GraphicsDevice::Instance->ApplyTarget();
    else SDL_SetRenderTarget(R(), fallback);
}

// Blit 'src' over the whole of target 'dst' with no blending.
void blit_replace(SDL_Texture* src, SDL_Texture* dst)
{
    SDL_Renderer* r = R();
    SDL_SetRenderTarget(r, dst);
    SDL_RenderSetViewport(r, nullptr);
    SDL_BlendMode old;
    SDL_GetTextureBlendMode(src, &old);
    SDL_SetTextureBlendMode(src, SDL_BLENDMODE_NONE);
    SDL_RenderCopy(r, src, nullptr, nullptr);
    SDL_SetTextureBlendMode(src, old);
}

void set_scale_mode(Texture2D* t, int mode)
{
    if (t->scaleMode == mode) return;
    // SDL backends apply the sampler state immediately; make queued draws use the old one.
    SDL_RenderFlush(R());
    SDL_SetTextureScaleMode(t->tex, mode ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    t->scaleMode = mode;
}

void set_blend(SDL_Texture* t, SDL_BlendMode m)
{
    if (SDL_SetTextureBlendMode(t, m) != 0)
    {
        if (!g_customBlendWarned)
        {
            g_customBlendWarned = true;
            log_msg("[xna] blend mode not supported by renderer (%s); falling back to BLEND", SDL_GetError());
        }
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
}

SDL_BlendFactor map_factor(Blend b)
{
    switch (b)
    {
    case Blend::One: return SDL_BLENDFACTOR_ONE;
    case Blend::Zero: return SDL_BLENDFACTOR_ZERO;
    case Blend::SourceColor: return SDL_BLENDFACTOR_SRC_COLOR;
    case Blend::InverseSourceColor: return SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case Blend::SourceAlpha: return SDL_BLENDFACTOR_SRC_ALPHA;
    case Blend::InverseSourceAlpha: return SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case Blend::DestinationColor: return SDL_BLENDFACTOR_DST_COLOR;
    case Blend::InverseDestinationColor: return SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    case Blend::DestinationAlpha: return SDL_BLENDFACTOR_DST_ALPHA;
    case Blend::InverseDestinationAlpha: return SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    default: return SDL_BLENDFACTOR_ONE; // BlendFactor / SourceAlphaSaturation: unsupported
    }
}

SDL_BlendOperation map_op(BlendFunction f)
{
    switch (f)
    {
    case BlendFunction::Add: return SDL_BLENDOPERATION_ADD;
    case BlendFunction::Subtract: return g_opSrcMinusDst;
    case BlendFunction::ReverseSubtract: return g_opDstMinusSrc;
    case BlendFunction::Max: return SDL_BLENDOPERATION_MAXIMUM;
    case BlendFunction::Min: return SDL_BLENDOPERATION_MINIMUM;
    }
    return SDL_BLENDOPERATION_ADD;
}

bool sampler_linear(SamplerState* s)
{
    if (!s) return true; // XNA default: LinearClamp
    switch (s->Filter)
    {
    case TextureFilter::Point:
    case TextureFilter::PointMipLinear:
    case TextureFilter::MinLinearMagPointMipLinear:
    case TextureFilter::MinLinearMagPointMipPoint:
        return false;   // magnification filter is point
    default:
        return true;
    }
}

// Detect which SDL op computes dst - src with a 1-pixel render + readback.
void probe_subtract()
{
    SDL_Renderer* r = R();
    if (!r) return;
    SDL_Texture* old = SDL_GetRenderTarget(r);
    SDL_Texture* dst = SDL_CreateTexture(r, kFormat, SDL_TEXTUREACCESS_TARGET, 4, 4);
    SDL_Texture* src = SDL_CreateTexture(r, kFormat, SDL_TEXTUREACCESS_STATIC, 4, 4);
    if (!dst || !src)
    {
        log_msg("[xna] blend probe: texture creation failed (%s); assuming REV_SUBTRACT = dst - src", SDL_GetError());
        if (dst) SDL_DestroyTexture(dst);
        if (src) SDL_DestroyTexture(src);
        return;
    }
    uint8_t px[4 * 4 * 4];
    for (int i = 0; i < 16; i++) { px[i * 4 + 0] = 50; px[i * 4 + 1] = 50; px[i * 4 + 2] = 50; px[i * 4 + 3] = 255; }
    SDL_UpdateTexture(src, nullptr, px, 16);
    SDL_SetRenderTarget(r, dst);
    SDL_RenderSetViewport(r, nullptr);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 200, 200, 200, 255);
    SDL_RenderClear(r);
    // First make sure pixel readback actually works on this renderer: read the plain clear
    // colour back. (On the Wii U / in Cemu, readback can return zeros, which would otherwise
    // look like "src - dst clamped to 0" and pick the wrong operation.)
    {
        uint8_t ref[4 * 4 * 4];
        memset(ref, 0, sizeof ref);
        int rr0 = SDL_RenderReadPixels(r, nullptr, kFormat, ref, 16);
        int v0 = ref[(1 * 4 + 1) * 4 + 0];
        if (rr0 != 0 || v0 < 190 || v0 > 210)
        {
            SDL_SetRenderTarget(r, old);
            SDL_DestroyTexture(src);
            SDL_DestroyTexture(dst);
            char buf[128];
            snprintf(buf, sizeof buf, "[xna] blend probe: readback unreliable (got %d, want 200); using REV_SUBTRACT = dst - src", v0);
            plat::log(buf);
            return;
        }
    }
    SDL_BlendMode m = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
                                                 SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    int rc = SDL_SetTextureBlendMode(src, m);
    uint8_t out[4 * 4 * 4];
    memset(out, 0xCD, sizeof out);
    int rr = -1;
    if (rc == 0)
    {
        SDL_RenderCopy(r, src, nullptr, nullptr);
        rr = SDL_RenderReadPixels(r, nullptr, kFormat, out, 16);
    }
    SDL_SetRenderTarget(r, old);
    SDL_DestroyTexture(src);
    SDL_DestroyTexture(dst);
    if (rc != 0 || rr != 0)
    {
        log_msg("[xna] blend probe failed (%s); assuming REV_SUBTRACT = dst - src", SDL_GetError());
        return;
    }
    int v = out[(1 * 4 + 1) * 4 + 0]; // red of pixel (1,1)
    if (v >= 120 && v <= 180)
    {
        g_opDstMinusSrc = SDL_BLENDOPERATION_REV_SUBTRACT; g_opSrcMinusDst = SDL_BLENDOPERATION_SUBTRACT;
        plat::log("[xna] blend probe: SDL REV_SUBTRACT = dst - src");
    }
    else if (v <= 30)
    {
        g_opDstMinusSrc = SDL_BLENDOPERATION_SUBTRACT; g_opSrcMinusDst = SDL_BLENDOPERATION_REV_SUBTRACT;
        plat::log("[xna] blend probe: SDL SUBTRACT = dst - src");
    }
    else
    {
        char buf[128];
        snprintf(buf, sizeof buf, "[xna] blend probe: unexpected value %d; assuming REV_SUBTRACT = dst - src", v);
        plat::log(buf);
    }
}

SDL_BlendMode darken_mode()
{
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE, g_opDstMinusSrc,
                                      SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
}

template<class T> void root(T*& p) { ::sys::gc_root((void*)&p, sizeof(void*)); }

} // namespace

// ======================================================================= init
void xna_graphics_init()
{
    auto mk = [](Blend cs, Blend cd, Blend as, Blend ad) { return ::sys::gcnew<BlendState>(cs, cd, as, ad); };
    BlendState::Opaque = mk(Blend::One, Blend::Zero, Blend::One, Blend::Zero);
    BlendState::AlphaBlend = mk(Blend::One, Blend::InverseSourceAlpha, Blend::One, Blend::InverseSourceAlpha);
    BlendState::Additive = mk(Blend::SourceAlpha, Blend::One, Blend::SourceAlpha, Blend::One);
    BlendState::NonPremultiplied = mk(Blend::SourceAlpha, Blend::InverseSourceAlpha, Blend::SourceAlpha, Blend::InverseSourceAlpha);
    root(BlendState::Opaque); root(BlendState::AlphaBlend); root(BlendState::Additive); root(BlendState::NonPremultiplied);

    auto ms = [](TextureFilter f, TextureAddressMode a) {
        auto* s = ::sys::gcnew<SamplerState>(); s->Filter = f; s->AddressU = s->AddressV = s->AddressW = a; return s; };
    SamplerState::PointClamp = ms(TextureFilter::Point, TextureAddressMode::Clamp);
    SamplerState::PointWrap = ms(TextureFilter::Point, TextureAddressMode::Wrap);
    SamplerState::LinearClamp = ms(TextureFilter::Linear, TextureAddressMode::Clamp);
    SamplerState::LinearWrap = ms(TextureFilter::Linear, TextureAddressMode::Wrap);
    SamplerState::AnisotropicClamp = ms(TextureFilter::Anisotropic, TextureAddressMode::Clamp);
    root(SamplerState::PointClamp); root(SamplerState::PointWrap); root(SamplerState::LinearClamp);
    root(SamplerState::LinearWrap); root(SamplerState::AnisotropicClamp);

    DepthStencilState::None = ::sys::gcnew<DepthStencilState>();
    DepthStencilState::Default = ::sys::gcnew<DepthStencilState>();
    DepthStencilState::DepthRead = ::sys::gcnew<DepthStencilState>();
    root(DepthStencilState::None); root(DepthStencilState::Default); root(DepthStencilState::DepthRead);

    RasterizerState::CullNone = ::sys::gcnew<RasterizerState>();
    RasterizerState::CullClockwise = ::sys::gcnew<RasterizerState>();
    RasterizerState::CullClockwise->cull = CullMode::CullClockwiseFace;
    RasterizerState::CullCounterClockwise = ::sys::gcnew<RasterizerState>();
    RasterizerState::CullCounterClockwise->cull = CullMode::CullCounterClockwiseFace;
    root(RasterizerState::CullNone); root(RasterizerState::CullClockwise); root(RasterizerState::CullCounterClockwise);

    root(GraphicsDevice::Instance);

    probe_subtract();
}

// ======================================================================= BlendState
int BlendState::SDLMode()
{
    if (sdlMode != -1) return sdlMode;
    if (ColorSourceBlend == Blend::One && ColorDestinationBlend == Blend::Zero && AlphaSourceBlend == Blend::One &&
        AlphaDestinationBlend == Blend::Zero && ColorBlendFunction == BlendFunction::Add && AlphaBlendFunction == BlendFunction::Add)
        sdlMode = (int)SDL_BLENDMODE_NONE;
    else
        sdlMode = (int)SDL_ComposeCustomBlendMode(map_factor(ColorSourceBlend), map_factor(ColorDestinationBlend), map_op(ColorBlendFunction),
                                                  map_factor(AlphaSourceBlend), map_factor(AlphaDestinationBlend), map_op(AlphaBlendFunction));
    return sdlMode;
}

// ======================================================================= Texture2D
Texture2D::Texture2D(GraphicsDevice*, int32_t w, int32_t h)
{
    width = w; height = h;
    tex = create_sdl_texture(w, h, false);
    std::vector<uint32_t> zero((size_t)w * h, 0u);
    SDL_UpdateTexture(tex, nullptr, zero.data(), w * 4);
}

Texture2D::Texture2D(GraphicsDevice* gd, int32_t w, int32_t h, bool, SurfaceFormat) : Texture2D(gd, w, h) {}

Texture2D::~Texture2D()
{
    if (tex && R()) SDL_DestroyTexture(tex);
    tex = nullptr;
}

void Texture2D::Dispose()
{
    if (tex && R()) SDL_DestroyTexture(tex);
    tex = nullptr;
    std::vector<uint32_t>().swap(cpuPixels);
    disposed = true;
}

Texture2D* Texture2D::FromPixels(int32_t w, int32_t h, const uint32_t* rgba, bool target)
{
    Texture2D* t = target ? (Texture2D*)::sys::gcnew<RenderTarget2D>(nullptr, w, h) : ::sys::gcnew<Texture2D>(nullptr, w, h);
    if (rgba) t->SetPixels(rgba);
    return t;
}

Texture2D* Texture2D::FromStream(GraphicsDevice*, ::System::IO::Stream* s)
{
    if (!s) ::sys::throw_msg("ArgumentNullException", "stream");
    std::vector<uint8_t> bytes = s->ReadAllBytesNative();
    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
    if (!px)
    {
        std::string msg = std::string("Could not decode image: ") + stbi_failure_reason();
        ::sys::throw_msg("InvalidOperationException", msg.c_str());
    }
    Texture2D* t = ::sys::gcnew<Texture2D>();
    t->width = w; t->height = h;
    t->tex = create_sdl_texture(w, h, false);
    SDL_UpdateTexture(t->tex, nullptr, px, w * 4);
    if (auto* fs = dynamic_cast<::System::IO::FileStream*>(s)) t->sourcePath = fs->path;
    else if ((size_t)w * h <= (1u << 20))
    {
        t->cpuPixels.resize((size_t)w * h);
        memcpy(t->cpuPixels.data(), px, (size_t)w * h * 4);
    }
    stbi_image_free(px);
    return t;
}

void Texture2D::SetPixels(const uint32_t* rgba)
{
    if (!tex || !rgba) return;
    if (isTarget)
    {
        SDL_Texture* old = SDL_GetRenderTarget(R());
        SDL_Texture* tmp = create_sdl_texture(width, height, false);
        SDL_UpdateTexture(tmp, nullptr, rgba, width * 4);
        blit_replace(tmp, tex);
        restore_device_target(old);
        SDL_DestroyTexture(tmp);
        return;
    }
    SDL_UpdateTexture(tex, nullptr, rgba, width * 4);
    size_t n = (size_t)width * height;
    if (n <= (1u << 20)) { cpuPixels.assign(rgba, rgba + n); }
    else std::vector<uint32_t>().swap(cpuPixels);
    sourcePath.clear();
}

void Texture2D::ReadPixels(std::vector<uint32_t>& out)
{
    size_t n = (size_t)width * height;
    out.assign(n, 0u);
    if (!tex) return;
    if (!isTarget && cpuPixels.size() == n) { out = cpuPixels; return; }
    if (!isTarget && !sourcePath.empty())
    {
        std::vector<uint8_t> bytes;
        if (::sys::read_file(sourcePath, bytes))
        {
            int w = 0, h = 0, c = 0;
            stbi_uc* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &c, 4);
            if (px && w == width && h == height)
            {
                memcpy(out.data(), px, n * 4);
                stbi_image_free(px);
                return;
            }
            if (px) stbi_image_free(px);
        }
    }
    // GPU readback
    SDL_Renderer* r = R();
    SDL_Texture* old = SDL_GetRenderTarget(r);
    SDL_Texture* src = tex;
    SDL_Texture* tmp = nullptr;
    if (!isTarget)
    {
        tmp = create_sdl_texture(width, height, true);
        blit_replace(tex, tmp);
        src = tmp;
    }
    SDL_SetRenderTarget(r, src);
    SDL_RenderSetViewport(r, nullptr);
    if (SDL_RenderReadPixels(r, nullptr, kFormat, out.data(), width * 4) != 0)
        log_msg("[xna] SDL_RenderReadPixels failed: %s", SDL_GetError());
    restore_device_target(old);
    if (tmp) SDL_DestroyTexture(tmp);
}

// ======================================================================= RenderTarget2D
RenderTarget2D::RenderTarget2D(GraphicsDevice*, int32_t w, int32_t h)
{
    width = w; height = h; isTarget = true;
    tex = create_sdl_texture(w, h, true);
    SDL_Renderer* r = R();
    SDL_Texture* old = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, tex);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    restore_device_target(old);
}

// ======================================================================= Effect
EffectParameterCollection::EffectParameterCollection()
{
    items = ::sys::gcnew<::System::Collections::Generic::List<EffectParameter*>>();
}

EffectParameter* EffectParameterCollection::get_Item(String* name)
{
    for (int32_t i = 0; i < items->size; i++)
        if (String::op_Equality(items->items->data[i]->name, name)) return items->items->data[i];
    auto* p = ::sys::gcnew<EffectParameter>();
    p->name = name;
    items->Add(p);
    return p;
}

float Effect::Param(const char* n)
{
    for (int32_t i = 0; i < parameters->items->size; i++)
    {
        EffectParameter* p = parameters->items->items->data[i];
        if (p->name && p->name->ToUtf8() == n) return p->value;
    }
    return 0.0f;
}

// ======================================================================= Viewport
String* Viewport::ToString()
{
    std::string s = "{X:" + std::to_string(x) + " Y:" + std::to_string(y) + " Width:" + std::to_string(width) +
                    " Height:" + std::to_string(height) + " MinDepth:0 MaxDepth:1}";
    return String::FromUtf8(s.c_str(), s.size());
}

// ======================================================================= GraphicsDevice
GraphicsDevice::GraphicsDevice()
{
    textures = ::sys::gcnew<TextureCollection>();
    samplers = ::sys::gcnew<SamplerStateCollection>();
    pp = ::sys::gcnew<PresentationParameters>();
    pp->BackBufferWidth = plat::BACKBUFFER_W; pp->BackBufferHeight = plat::BACKBUFFER_H;
    displayMode = ::sys::gcnew<DisplayMode>();
    displayMode->width = plat::BACKBUFFER_W; displayMode->height = plat::BACKBUFFER_H;
    viewport = Viewport(0, 0, plat::BACKBUFFER_W, plat::BACKBUFFER_H);
    scissor = Rectangle(0, 0, plat::BACKBUFFER_W, plat::BACKBUFFER_H);
    blendState = BlendState::Opaque;
    depthState = DepthStencilState::Default;
    rasterState = RasterizerState::CullCounterClockwise;
    for (int i = 0; i < 8; i++) samplers->slots[i] = SamplerState::LinearWrap;
    if (!Instance) Instance = this;
}

void GraphicsDevice::ApplyTarget()
{
    SDL_Renderer* r = R();
    if (!r) return;
    SDL_SetRenderTarget(r, (currentTarget && currentTarget->tex) ? currentTarget->tex : nullptr);
    SDL_Rect vr{ viewport.x, viewport.y, viewport.width, viewport.height };
    SDL_RenderSetViewport(r, &vr);
}

void GraphicsDevice::Clear(Color c)
{
    SDL_Renderer* r = R();
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderClear(r);   // whole target, like FNA's Clear
}

void GraphicsDevice::SetRenderTarget(RenderTarget2D* rt)
{
    currentTarget = rt;
    if (rt) viewport = Viewport(0, 0, rt->width, rt->height);
    else viewport = Viewport(0, 0, plat::BACKBUFFER_W, plat::BACKBUFFER_H);
    ApplyTarget();
}

void GraphicsDevice::set_Viewport(Viewport v)
{
    viewport = v;
    SDL_Rect vr{ v.x, v.y, v.width, v.height };
    SDL_RenderSetViewport(R(), &vr);
}

void GraphicsDevice::Present() {}

// ======================================================================= SpriteFont
int32_t SpriteFont::Index(char16_t c)
{
    auto it = std::lower_bound(chars.begin(), chars.end(), c);
    if (it != chars.end() && *it == c) return (int32_t)(it - chars.begin());
    if (defaultChar >= 0 && (char16_t)defaultChar != c) return Index((char16_t)defaultChar);
    return -1;
}

Vector2 SpriteFont::MeasureString(String* s)
{
    if (!s) ::sys::throw_msg("ArgumentNullException", "text");
    if (s->length == 0) return Vector2(0, 0);
    Vector2 result(0, 0);
    float cur = 0, finalLineHeight = (float)lineSpacing;
    bool first = true;
    for (int32_t i = 0; i < s->length; i++)
    {
        char16_t c = s->data[i];
        if (c == u'\r') continue;
        if (c == u'\n')
        {
            result.X = std::max(result.X, cur);
            result.Y += lineSpacing;
            cur = 0; finalLineHeight = (float)lineSpacing; first = true;
            continue;
        }
        int32_t idx = Index(c);
        if (idx < 0) continue;
        const Vector3& k = kerning[idx];
        if (first) { cur += std::fabs(k.X); first = false; }
        else cur += spacing + k.X;
        cur += k.Y + k.Z;
        float ch = (float)cropping[idx].Height;
        if (ch > finalLineHeight) finalLineHeight = ch;
    }
    result.X = std::max(result.X, cur);
    result.Y += finalLineHeight;
    return result;
}

// ======================================================================= SpriteBatch
namespace {

struct SpriteItem
{
    Texture2D* tex;
    float x[4], y[4];   // transformed corners: TL, TR, BL, BR
    float u[4], v[4];
    float depth;
    uint8_t r, g, b, a;
};

std::vector<SDL_Vertex> g_verts;
std::vector<int> g_idx;
std::vector<int32_t> g_order;

void emit_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, SDL_Color c)
{
    int base = (int)g_verts.size();
    g_verts.push_back(SDL_Vertex{ { x0, y0 }, c, { u0, v0 } });
    g_verts.push_back(SDL_Vertex{ { x1, y0 }, c, { u1, v0 } });
    g_verts.push_back(SDL_Vertex{ { x0, y1 }, c, { u0, v1 } });
    g_verts.push_back(SDL_Vertex{ { x1, y1 }, c, { u1, v1 } });
    int q[6] = { base, base + 1, base + 2, base + 1, base + 3, base + 2 };
    g_idx.insert(g_idx.end(), q, q + 6);
}

void submit(SDL_Texture* t)
{
    if (g_verts.empty()) return;
    SDL_RenderGeometry(R(), t, g_verts.data(), (int)g_verts.size(), g_idx.data(), (int)g_idx.size());
    g_verts.clear();
    g_idx.clear();
}

// ScreenEffects shader: x = frac(uv.x + A*sin(uv.y*F + C)); out = tex(x, uv.y)
void emit_wave(const SpriteItem& it, float A, float F, float C, SDL_Color col)
{
    float x0 = std::min(std::min(it.x[0], it.x[1]), std::min(it.x[2], it.x[3]));
    float x1 = std::max(std::max(it.x[0], it.x[1]), std::max(it.x[2], it.x[3]));
    float y0 = std::min(std::min(it.y[0], it.y[1]), std::min(it.y[2], it.y[3]));
    float y1 = std::max(std::max(it.y[0], it.y[1]), std::max(it.y[2], it.y[3]));
    float Wd = x1 - x0, Hd = y1 - y0;
    if (Wd <= 0 || Hd <= 0) return;
    // uv at the left/right and top/bottom edges (honours flips)
    float uL = it.u[0], uR = it.u[1], vT = it.v[0], vB = it.v[2];
    if (it.x[1] < it.x[0]) std::swap(uL, uR);
    if (it.y[2] < it.y[0]) std::swap(vT, vB);
    float th = (float)it.tex->height;
    int rows = (int)std::ceil(Hd - 0.001f);
    for (int j = 0; j < rows; j++)
    {
        float ry0 = y0 + j, ry1 = std::min(y0 + j + 1, y1);
        float vmid = ((ry0 + ry1) * 0.5f - y0) / Hd;
        float uvY = vT + vmid * (vB - vT);
        float s = A * std::sin(uvY * F + C);
        float row = std::floor(uvY * th);
        if (row < 0) row = 0;
        if (row > th - 1) row = th - 1;
        float vc = (row + 0.5f) / th;
        // sampled u runs from ua to ub across the strip, wrapped into [0,1)
        float ua = uL + s, ub = uR + s;
        float span = ub - ua;
        if (span == 0) continue;
        float lo = std::min(ua, ub), hi = std::max(ua, ub);
        float t = lo;
        int guard = 0;
        while (t < hi && guard++ < 8)
        {
            float n = std::floor(t);
            float e = std::min(hi, n + 1.0f);
            // dest x for sampled u values t and e
            float xa = x0 + (t - ua) / span * Wd;
            float xb = x0 + (e - ua) / span * Wd;
            float sa = t - n, sb = e - n;
            if (xa > xb) { std::swap(xa, xb); std::swap(sa, sb); }
            emit_quad(xa, ry0, xb, ry1, sa, vc, sb, vc, col);
            t = e;
        }
    }
}

} // namespace

void SpriteBatch::Begin()
{
    Begin(SpriteSortMode::Deferred, nullptr, nullptr, nullptr, nullptr, nullptr, Matrix::get_Identity());
}
void SpriteBatch::Begin(SpriteSortMode m, BlendState* b)
{
    Begin(m, b, nullptr, nullptr, nullptr, nullptr, Matrix::get_Identity());
}
void SpriteBatch::Begin(SpriteSortMode m, BlendState* b, SamplerState* s, DepthStencilState* d, RasterizerState* r)
{
    Begin(m, b, s, d, r, nullptr, Matrix::get_Identity());
}
void SpriteBatch::Begin(SpriteSortMode m, BlendState* b, SamplerState* s, DepthStencilState* d, RasterizerState* r, Effect* e)
{
    Begin(m, b, s, d, r, e, Matrix::get_Identity());
}
void SpriteBatch::Begin(SpriteSortMode m, BlendState* b, SamplerState* s, DepthStencilState*, RasterizerState*, Effect* e, Matrix transform)
{
    if (begun) ::sys::throw_msg("InvalidOperationException", "Begin cannot be called again until End has been successfully called.");
    begun = true;
    sortMode = m;
    blend = b ? b : BlendState::AlphaBlend;
    sampler = s ? s : SamplerState::LinearClamp;
    effect = e;
    matrix = transform;
    itemCount = 0;
}

void SpriteBatch::End()
{
    if (!begun) ::sys::throw_msg("InvalidOperationException", "End was called, but Begin has not yet been called.");
    Flush();
    begun = false;
    effect = nullptr;
}

void SpriteBatch::PushSprite(Texture2D* t, float srcX, float srcY, float srcW, float srcH, float dstX, float dstY, float dstW, float dstH,
                             Color c, float originX, float originY, float rot, int fx, float depth)
{
    if (!begun) ::sys::throw_msg("InvalidOperationException", "Draw was called, but Begin has not yet been called.");
    if (!t) ::sys::throw_msg("ArgumentNullException", "texture");
    if (itemCount == itemCap)
    {
        int32_t ncap = itemCap ? itemCap * 2 : 256;
        void* mem = ::sys::gc_alloc(sizeof(SpriteItem) * (size_t)ncap);
        if (items) memcpy(mem, items, sizeof(SpriteItem) * (size_t)itemCount);
        items = mem;
        itemCap = ncap;
    }
    SpriteItem& it = static_cast<SpriteItem*>(items)[itemCount++];
    it.tex = t;
    it.depth = depth;
    it.r = c.r; it.g = c.g; it.b = c.b; it.a = c.a;

    float tw = (float)t->width, th = (float)t->height;
    if (tw <= 0) tw = 1;
    if (th <= 0) th = 1;
    // FNA PushSprite: origin normalised by the source size, corners scaled by the destination size
    float ox = srcW != 0 ? originX / srcW : 0.0f;
    float oy = srcH != 0 ? originY / srcH : 0.0f;
    float sn = 0, cs = 1;
    if (rot != 0) { sn = (float)std::sin((double)rot); cs = (float)std::cos((double)rot); }
    static const float CX[4] = { 0, 1, 0, 1 };
    static const float CY[4] = { 0, 0, 1, 1 };
    const Matrix& M = matrix;
    for (int k = 0; k < 4; k++)
    {
        float lx = (CX[k] - ox) * dstW;
        float ly = (CY[k] - oy) * dstH;
        float px = (-sn * ly) + (cs * lx) + dstX;
        float py = (sn * lx) + (cs * ly) + dstY;
        it.x[k] = px * M.M11 + py * M.M21 + M.M41;
        it.y[k] = px * M.M12 + py * M.M22 + M.M42;
        int f = k ^ (fx & 3);
        it.u[k] = (srcX + CX[f] * srcW) / tw;
        it.v[k] = (srcY + CY[f] * srcH) / th;
    }
}

void SpriteBatch::Flush()
{
    int32_t n = itemCount;
    if (n == 0) return;
    SpriteItem* arr = static_cast<SpriteItem*>(items);
    SDL_Renderer* r = R();

    g_order.resize(n);
    for (int32_t i = 0; i < n; i++) g_order[i] = i;
    if (sortMode == SpriteSortMode::BackToFront)
        std::stable_sort(g_order.begin(), g_order.end(), [arr](int32_t a, int32_t b) { return arr[a].depth > arr[b].depth; });
    else if (sortMode == SpriteSortMode::FrontToBack)
        std::stable_sort(g_order.begin(), g_order.end(), [arr](int32_t a, int32_t b) { return arr[a].depth < arr[b].depth; });

    EffectKind kind = effect ? effect->kind : EffectKind::Unknown;

    // ---- Lighting: darken every render target used as a source by Textures[1] (in place)
    if (kind == EffectKind::Lighting && device)
    {
        Texture2D* light = dynamic_cast<Texture2D*>(device->textures->get_Item(1));
        float k = effect->Param("Alpha");
        k = k < 0 ? 0 : (k > 1 ? 1 : k);
        if (light && light->tex && k > 0)
        {
            std::vector<Texture2D*> done;
            SDL_BlendMode dm = darken_mode();
            Uint8 a = (Uint8)std::lround(k * 255.0f);
            for (int32_t i = 0; i < n; i++)
            {
                Texture2D* t = arr[i].tex;
                if (!t->isTarget || !t->tex || t == light) continue;
                if (std::find(done.begin(), done.end(), t) != done.end()) continue;
                done.push_back(t);
                SDL_SetRenderTarget(r, t->tex);
                SDL_RenderSetViewport(r, nullptr);
                set_blend(light->tex, dm);
                set_scale_mode(light, 0);
                emit_quad(0, 0, (float)t->width, (float)t->height, 0, 0, 1, 1, SDL_Color{ 255, 255, 255, a });
                submit(light->tex);
            }
            if (!done.empty()) device->ApplyTarget();
        }
    }

    // ---- draw runs
    SDL_BlendMode mode = (SDL_BlendMode)blend->SDLMode();
    int scale = sampler_linear(sampler) ? 1 : 0;
    float wA = 0, wF = 0, wC = 0;
    bool wave = kind == EffectKind::ScreenWave;
    if (wave) { wA = effect->Param("Amplitude"); wF = effect->Param("Frequency"); wC = effect->Param("Counter"); }

    Texture2D* cur = nullptr;
    int32_t inRun = 0;
    for (int32_t oi = 0; oi < n; oi++)
    {
        const SpriteItem& it = arr[g_order[oi]];
        Texture2D* t = it.tex;
        if (!t->tex) continue;   // disposed
        if (t != cur || inRun >= 2048)
        {
            if (cur) submit(cur->tex);
            cur = t;
            inRun = 0;
            set_blend(t->tex, mode);
            set_scale_mode(t, scale);
        }
        inRun++;
        SDL_Color col{ it.r, it.g, it.b, it.a };
        if (wave && wA != 0)
        {
            emit_wave(it, wA, wF, wC, col);
            if (g_verts.size() > 16000) { submit(t->tex); inRun = 0; }
            continue;
        }
        int base = (int)g_verts.size();
        for (int k = 0; k < 4; k++) g_verts.push_back(SDL_Vertex{ { it.x[k], it.y[k] }, col, { it.u[k], it.v[k] } });
        int q[6] = { base, base + 1, base + 2, base + 1, base + 3, base + 2 };
        g_idx.insert(g_idx.end(), q, q + 6);
    }
    if (cur) submit(cur->tex);
    g_verts.clear();
    g_idx.clear();

    // drop texture references so the GC can reclaim them
    memset(items, 0, sizeof(SpriteItem) * (size_t)n);
    itemCount = 0;
}

// ---- Draw overloads (XNA/FNA semantics)
void SpriteBatch::Draw(Texture2D* t, Vector2 pos, Color c)
{
    if (!t) ::sys::throw_msg("ArgumentNullException", "texture");
    PushSprite(t, 0, 0, (float)t->width, (float)t->height, pos.X, pos.Y, (float)t->width, (float)t->height, c, 0, 0, 0, 0, 0);
}

void SpriteBatch::Draw(Texture2D* t, Rectangle dest, Color c)
{
    if (!t) ::sys::throw_msg("ArgumentNullException", "texture");
    PushSprite(t, 0, 0, (float)t->width, (float)t->height, (float)dest.X, (float)dest.Y, (float)dest.Width, (float)dest.Height, c, 0, 0, 0, 0, 0);
}

void SpriteBatch::Draw(Texture2D* t, Vector2 pos, ::System::Nullable<Rectangle> src, Color c)
{
    Draw(t, pos, src, c, 0.0f, Vector2(0, 0), Vector2(1, 1), SpriteEffects::None, 0.0f);
}

void SpriteBatch::Draw(Texture2D* t, Rectangle dest, ::System::Nullable<Rectangle> src, Color c)
{
    Draw(t, dest, src, c, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
}

void SpriteBatch::Draw(Texture2D* t, Vector2 pos, ::System::Nullable<Rectangle> src, Color c, float rot, Vector2 origin, float scale, SpriteEffects fx, float depth)
{
    Draw(t, pos, src, c, rot, origin, Vector2(scale, scale), fx, depth);
}

void SpriteBatch::Draw(Texture2D* t, Vector2 pos, ::System::Nullable<Rectangle> src, Color c, float rot, Vector2 origin, Vector2 scale, SpriteEffects fx, float depth)
{
    if (!t) ::sys::throw_msg("ArgumentNullException", "texture");
    float sx, sy, sw, sh;
    if (src.hasValue) { sx = (float)src.value.X; sy = (float)src.value.Y; sw = (float)src.value.Width; sh = (float)src.value.Height; }
    else { sx = 0; sy = 0; sw = (float)t->width; sh = (float)t->height; }
    PushSprite(t, sx, sy, sw, sh, pos.X, pos.Y, sw * scale.X, sh * scale.Y, c, origin.X, origin.Y, rot, (int)fx, depth);
}

void SpriteBatch::Draw(Texture2D* t, Rectangle dest, ::System::Nullable<Rectangle> src, Color c, float rot, Vector2 origin, SpriteEffects fx, float depth)
{
    if (!t) ::sys::throw_msg("ArgumentNullException", "texture");
    float sx, sy, sw, sh;
    if (src.hasValue) { sx = (float)src.value.X; sy = (float)src.value.Y; sw = (float)src.value.Width; sh = (float)src.value.Height; }
    else { sx = 0; sy = 0; sw = (float)t->width; sh = (float)t->height; }
    PushSprite(t, sx, sy, sw, sh, (float)dest.X, (float)dest.Y, (float)dest.Width, (float)dest.Height, c, origin.X, origin.Y, rot, (int)fx, depth);
}

void SpriteBatch::DrawString(SpriteFont* f, String* s, Vector2 pos, Color c)
{
    DrawString(f, s, pos, c, 0.0f, Vector2(0, 0), Vector2(1, 1), SpriteEffects::None, 0.0f);
}

void SpriteBatch::DrawString(SpriteFont* f, String* s, Vector2 pos, Color c, float rot, Vector2 origin, float scale, SpriteEffects fx, float depth)
{
    DrawString(f, s, pos, c, rot, origin, Vector2(scale, scale), fx, depth);
}

void SpriteBatch::DrawString(SpriteFont* f, String* s, Vector2 pos, Color c, float rot, Vector2 origin, Vector2 scale, SpriteEffects fx, float depth)
{
    if (!f) ::sys::throw_msg("ArgumentNullException", "spriteFont");
    if (!s) ::sys::throw_msg("ArgumentNullException", "text");
    if (!f->texture) return;
    int ifx = (int)fx & 3;
    Vector2 size(0, 0);
    if (ifx) size = f->MeasureString(s);
    float cx = 0, cy = 0;
    bool first = true;
    for (int32_t i = 0; i < s->length; i++)
    {
        char16_t ch = s->data[i];
        if (ch == u'\r') continue;
        if (ch == u'\n') { cx = 0; cy += f->lineSpacing; first = true; continue; }
        int32_t idx = f->Index(ch);
        if (idx < 0) continue;
        const Vector3& k = f->kerning[idx];
        if (first) { cx += std::fabs(k.X); first = false; }
        else cx += f->spacing + k.X;
        const Rectangle& g = f->glyphs[idx];
        const Rectangle& cr = f->cropping[idx];
        float lx = cx + cr.X, ly = cy + cr.Y;
        if (ifx & 1) lx = size.X - lx - g.Width;
        if (ifx & 2) ly = size.Y - ly - g.Height;
        PushSprite(f->texture, (float)g.X, (float)g.Y, (float)g.Width, (float)g.Height, pos.X, pos.Y,
                   g.Width * scale.X, g.Height * scale.Y, c, origin.X - lx, origin.Y - ly, rot, ifx, depth);
        cx += k.Y + k.Z;
    }
}

}}}} // namespace Microsoft::Xna::Framework::Graphics
