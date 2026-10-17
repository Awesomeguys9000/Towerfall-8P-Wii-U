// Test of runtime/xna graphics (SpriteBatch, shader emulation, blend probe) and content (SpriteFont XNB).
//
// Build (macOS), from port/tests:
//   clang++ -std=c++20 -O1 -I../runtime -I../runtime/third_party -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 \
//     graphics_test.cpp ../runtime/xna/math.cpp ../runtime/xna/graphics.cpp ../runtime/xna/content.cpp ../runtime/sys/*.cpp \
//     -L/opt/homebrew/lib -lSDL2 -o /tmp/graphics_test
// Usage: graphics_test <TF-8P dir>     (writes /tmp/gfx_text.ppm)
#include "xna/xna.h"
#include "platform/platform.h"
#include <SDL.h>
#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static SDL_Window* g_win;
static SDL_Renderer* g_ren;
static std::string g_root;

namespace plat {
SDL_Renderer* renderer() { return g_ren; }
SDL_Window* window() { return g_win; }
void log(const char* m) { printf("%s\n", m); }
std::string content_root() { return g_root; }
std::string save_dir() { return "/tmp"; }
bool is_wiiu() { return false; }
int64_t now_ns() { return (int64_t)SDL_GetPerformanceCounter() * 1000000000LL / (int64_t)SDL_GetPerformanceFrequency(); }
void sleep_ms(int ms) { SDL_Delay(ms); }
}

static int g_fail = 0;
static void check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fail++;
}

static std::vector<uint32_t> read(Texture2D* t)
{
    std::vector<uint32_t> v;
    t->ReadPixels(v);
    return v;
}
static const uint8_t* px(const std::vector<uint32_t>& v, int w, int x, int y) { return (const uint8_t*)&v[(size_t)y * w + x]; }
static bool near(const uint8_t* p, int r, int g, int b, int a, int tol = 2)
{
    return abs(p[0] - r) <= tol && abs(p[1] - g) <= tol && abs(p[2] - b) <= tol && abs(p[3] - a) <= tol;
}

static RenderTarget2D* solid_target(int w, int h, Color c)
{
    auto* t = ::sys::gcnew<RenderTarget2D>(GraphicsDevice::Instance, w, h);
    GraphicsDevice::Instance->SetRenderTarget(t);
    GraphicsDevice::Instance->Clear(c);
    GraphicsDevice::Instance->SetRenderTarget(nullptr);
    return t;
}

static void run()
{
    xna_graphics_init();
    GraphicsDevice* gd = ::sys::gcnew<GraphicsDevice>();
    GraphicsDevice::Instance = gd;
    SpriteBatch* sb = ::sys::gcnew<SpriteBatch>(gd);

    // ---- lighting emulation: S = F - k * L
    {
        auto* F = solid_target(8, 8, Color(200, 100, 50, 255));
        auto* L = solid_target(8, 8, Color(100, 100, 100, 255));
        auto* S = solid_target(8, 8, Color(0, 0, 0, 0));
        Effect* fx = ::sys::gcnew<Effect>();
        fx->kind = EffectKind::Lighting;
        fx->parameters->get_Item(String::FromUtf8("Alpha"))->SetValue(0.5f);
        gd->get_Textures()->set_Item(1, L);
        gd->SetRenderTarget(S);
        sb->Begin(SpriteSortMode::Texture, BlendState::AlphaBlend, SamplerState::PointClamp, DepthStencilState::None, RasterizerState::CullNone, fx, Matrix::get_Identity());
        sb->Draw(F, Vector2(0, 0), Color::get_White());
        sb->End();
        gd->SetRenderTarget(nullptr);
        auto v = read(S);
        const uint8_t* p = px(v, 8, 3, 3);
        printf("  lighting result %d %d %d %d (want 150 50 0 255)\n", p[0], p[1], p[2], p[3]);
        check(near(p, 150, 50, 0, 255), "lighting darken");
    }

    // ---- LightingLayer-style BlendState (ReverseSubtract, SourceAlpha/One)
    {
        auto* D = solid_target(4, 4, Color(200, 200, 200, 255));
        auto* bs = ::sys::gcnew<BlendState>();
        bs->set_AlphaBlendFunction(BlendFunction::ReverseSubtract);
        bs->set_ColorBlendFunction(BlendFunction::ReverseSubtract);
        bs->set_ColorSourceBlend(Blend::SourceAlpha);
        bs->set_ColorDestinationBlend(Blend::One);
        bs->set_AlphaSourceBlend(Blend::Zero);
        bs->set_AlphaDestinationBlend(Blend::One);
        auto* src = ::sys::gcnew<Texture2D>(gd, 1, 1);
        uint8_t white[4] = { 255, 255, 255, 255 };
        src->SetPixels((const uint32_t*)white);
        gd->SetRenderTarget(D);
        sb->Begin(SpriteSortMode::Deferred, bs, SamplerState::PointClamp, DepthStencilState::None, RasterizerState::CullNone);
        sb->Draw(src, Rectangle(0, 0, 4, 4), Color(100, 50, 0, 128));
        sb->End();
        gd->SetRenderTarget(nullptr);
        auto v = read(D);
        const uint8_t* p = px(v, 4, 1, 1);
        // src = (100,50,0,128) * a(128/255) => (50,25,0); dst - src
        printf("  revsub result %d %d %d %d (want ~150 175 200 255)\n", p[0], p[1], p[2], p[3]);
        check(near(p, 150, 175, 200, 255, 3), "ReverseSubtract blend state");
    }

    // ---- sprite transform: 2x2 texture, flipH, scale 2, origin, translation matrix
    {
        auto* T = ::sys::gcnew<Texture2D>(gd, 2, 2);
        uint8_t d[16] = { 255, 0, 0, 255,  0, 255, 0, 255,  0, 0, 255, 255,  255, 255, 255, 255 }; // R G / B W
        T->SetPixels((const uint32_t*)d);
        auto* D = solid_target(16, 16, Color(0, 0, 0, 0));
        gd->SetRenderTarget(D);
        sb->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, SamplerState::PointClamp, DepthStencilState::None, RasterizerState::CullNone,
                  nullptr, Matrix::CreateTranslation(4, 2, 0));
        // origin (1,1) in texels, scale 2 => top-left at pos - 2 = (2,2) + translation => (6,4), size 4x4
        sb->Draw(T, Vector2(4, 4), ::System::Nullable<Rectangle>(), Color::get_White(), 0.0f, Vector2(1, 1), 2.0f, SpriteEffects::FlipHorizontally, 0);
        sb->End();
        gd->SetRenderTarget(nullptr);
        auto v = read(D);
        check(near(px(v, 16, 6, 4), 0, 255, 0, 255), "flipH top-left is G");
        check(near(px(v, 16, 9, 4), 255, 0, 0, 255), "flipH top-right is R");
        check(near(px(v, 16, 6, 7), 255, 255, 255, 255), "flipH bottom-left is W");
        check(near(px(v, 16, 5, 4), 0, 0, 0, 0) && near(px(v, 16, 10, 4), 0, 0, 0, 0) && near(px(v, 16, 6, 8), 0, 0, 0, 0), "sprite bounds");
        // GetData of a target and of a CPU texture
        auto t2 = read(T);
        check(memcmp(t2.data(), d, 16) == 0, "Texture2D GetData (cpu copy)");

        // rotation by 90deg around origin (0,0): (x,y)->(-y,x)
        auto* D2 = solid_target(8, 8, Color(0, 0, 0, 0));
        gd->SetRenderTarget(D2);
        sb->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, SamplerState::PointClamp, DepthStencilState::None, RasterizerState::CullNone);
        sb->Draw(T, Vector2(4, 0), ::System::Nullable<Rectangle>(), Color::get_White(), MathHelper::Pi / 2, Vector2(0, 0), 1.0f, SpriteEffects::None, 0);
        sb->End();
        gd->SetRenderTarget(nullptr);
        auto v2 = read(D2);
        // texel R(0,0) covers [0,1]x[0,1] -> rotated to x in [3,4], y in [0,1]
        check(near(px(v2, 8, 3, 0), 255, 0, 0, 255), "rotation R");
        check(near(px(v2, 8, 3, 1), 0, 255, 0, 255), "rotation G");
        check(near(px(v2, 8, 2, 0), 0, 0, 255, 255), "rotation B");
    }

    // ---- ScreenWave: 4x1 texture with distinct columns, s = A*sin(C) = 0.25 => shift one column
    {
        auto* T = ::sys::gcnew<RenderTarget2D>(gd, 4, 2);
        uint8_t d[32];
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 4; x++) { uint8_t* p = d + (y * 4 + x) * 4; p[0] = (uint8_t)(x * 60); p[1] = (uint8_t)(y * 100); p[2] = 0; p[3] = 255; }
        T->SetPixels((const uint32_t*)d);
        auto* D = solid_target(8, 4, Color(0, 0, 0, 0));
        Effect* fx = ::sys::gcnew<Effect>();
        fx->kind = EffectKind::ScreenWave;
        fx->parameters->get_Item(String::FromUtf8("Counter"))->SetValue(MathHelper::Pi / 2);
        fx->parameters->get_Item(String::FromUtf8("Amplitude"))->SetValue(0.25f);
        fx->parameters->get_Item(String::FromUtf8("Frequency"))->SetValue(0.0f);
        gd->SetRenderTarget(D);
        sb->Begin(SpriteSortMode::Texture, BlendState::Opaque, SamplerState::PointClamp, DepthStencilState::None, RasterizerState::CullNone, fx);
        sb->Draw(T, Rectangle(0, 0, 8, 4), ::System::Nullable<Rectangle>(Rectangle(0, 0, 4, 2)), Color::get_White());
        sb->End();
        gd->SetRenderTarget(nullptr);
        auto v = read(D);
        bool ok = true;
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 8; x++)
            {
                int sc = ((x / 2) + 1) % 4, sr = y / 2;
                if (!near(px(v, 8, x, y), sc * 60, sr * 100, 0, 255)) { ok = false; printf("  wave mismatch at %d,%d: %d %d\n", x, y, px(v, 8, x, y)[0], px(v, 8, x, y)[1]); }
            }
        check(ok, "screen wave shift + wrap");
    }

    // ---- fonts
    const char* names[] = { "Misc/Archer", "Misc\\OpenSans", "Misc/MonocleDefault" };
    Content::ContentManager* cm = ::sys::gcnew<Content::ContentManager>();
    cm->RootDirectory = String::FromUtf8("Content");
    SpriteFont* archer = nullptr;
    for (const char* n : names)
    {
        SpriteFont* f = cm->LoadFont(String::FromUtf8(n));
        if (!archer) archer = f;
        Vector2 m = f->MeasureString(String::FromUtf8("Hello World!"));
        Vector2 m2 = f->MeasureString(String::FromUtf8("AB\nC"));
        printf("  %s: %dx%d tex, %d glyphs, lineSpacing %d spacing %g default %d; 'Hello World!' = %s, 'AB\\nC' = %s\n", n,
               f->texture->width, f->texture->height, (int)f->chars.size(), f->lineSpacing, f->spacing, f->defaultChar,
               m.ToString()->ToUtf8().c_str(), m2.ToString()->ToUtf8().c_str());
        check(f->chars.size() > 90, "font glyph count");
    }
    {
        auto* D = solid_target(200, 40, Color(0, 0, 128, 255));
        gd->SetRenderTarget(D);
        sb->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, SamplerState::PointClamp, DepthStencilState::None, RasterizerState::CullNone);
        sb->DrawString(archer, String::FromUtf8("HELLO TOWERFALL 0123"), Vector2(2, 2), Color::get_White());
        sb->DrawString(archer, String::FromUtf8("Scaled x2"), Vector2(2, 14), Color(255, 200, 0, 255), 0.0f, Vector2(0, 0), 2.0f, SpriteEffects::None, 0);
        sb->End();
        gd->SetRenderTarget(nullptr);
        auto v = read(D);
        FILE* f = fopen("/tmp/gfx_text.ppm", "wb");
        fprintf(f, "P6 200 40 255\n");
        for (auto p : v) fwrite(&p, 1, 3, f);
        fclose(f);
        printf("  wrote /tmp/gfx_text.ppm\n");
    }

    // ---- math
    {
        Matrix m = Matrix::CreateRotationZ(0.3f) * Matrix::CreateScale(2.0f) * Matrix::CreateTranslation(5, 6, 0);
        Matrix i = Matrix::Invert(m);
        Vector2 p = Vector2::Transform(Vector2::Transform(Vector2(3, 4), m), i);
        check(std::fabs(p.X - 3) < 1e-4f && std::fabs(p.Y - 4) < 1e-4f, "Matrix invert round trip");
        printf("  %s %s %s\n", Vector2(1, 2.5f).ToString()->ToUtf8().c_str(), Rectangle(1, 2, 3, 4).ToString()->ToUtf8().c_str(),
               Color::get_CornflowerBlue().ToString()->ToUtf8().c_str());
        check(std::fabs(MathHelper::WrapAngle(7.0f) - (7.0f - 6.28318548f)) < 1e-5f, "WrapAngle");
        Rectangle a(0, 0, 10, 10), b(5, 5, 10, 10);
        check(Rectangle::Intersect(a, b) == Rectangle(5, 5, 5, 5) && Rectangle::Union(a, b) == Rectangle(0, 0, 15, 15), "Rectangle Intersect/Union");
    }
}

int main(int argc, char** argv)
{
    int stackTop;
    ::sys::gc_init(&stackTop);
    if (argc < 2) { fprintf(stderr, "usage: graphics_test <TowerFall folder>\n"); return 1; }
    g_root = argv[1];
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SDL_Init: %s\n", SDL_GetError()); return 1; }
    g_win = SDL_CreateWindow("gfx test", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!g_ren) { printf("renderer: %s\n", SDL_GetError()); return 1; }
    SDL_RendererInfo info;
    SDL_GetRendererInfo(g_ren, &info);
    printf("renderer: %s\n", info.name);
    SDL_RenderSetLogicalSize(g_ren, 1280, 720);
    try { run(); }
    catch (...) { printf("exception thrown\n"); g_fail++; }
    printf("%s (%d failures)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    SDL_DestroyRenderer(g_ren);
    SDL_Quit();
    return g_fail ? 1 : 0;
}
