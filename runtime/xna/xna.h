// Microsoft.Xna.Framework subset implemented on SDL2 (SDL_Renderer).
#pragma once
#include "sys/runtime_sys.h"

struct SDL_Texture;

namespace Microsoft { namespace Xna { namespace Framework {

using ::System::Object;
using ::System::String;

enum class PlayerIndex : int32_t { One = 0, Two, Three, Four, Five, Six, Seven, Eight };

struct Matrix;

struct Vector2
{
    float X = 0, Y = 0;
    Vector2() {}
    Vector2(float x, float y) : X(x), Y(y) {}
    explicit Vector2(float v) : X(v), Y(v) {}
    static Vector2 get_Zero() { return Vector2(); }
    static Vector2 get_One() { return Vector2(1, 1); }
    static Vector2 get_UnitX() { return Vector2(1, 0); }
    static Vector2 get_UnitY() { return Vector2(0, 1); }
    float Length() const { return std::sqrt(X * X + Y * Y); }
    float LengthSquared() const { return X * X + Y * Y; }
    void Normalize() { float l = 1.0f / Length(); X *= l; Y *= l; }
    static Vector2 Normalize(Vector2 v) { v.Normalize(); return v; }
    static float Distance(Vector2 a, Vector2 b) { return (a - b).Length(); }
    static float DistanceSquared(Vector2 a, Vector2 b) { return (a - b).LengthSquared(); }
    static float Dot(Vector2 a, Vector2 b) { return a.X * b.X + a.Y * b.Y; }
    static Vector2 Lerp(Vector2 a, Vector2 b, float t) { return Vector2(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t); }
    static Vector2 Min(Vector2 a, Vector2 b) { return Vector2(std::min(a.X, b.X), std::min(a.Y, b.Y)); }
    static Vector2 Max(Vector2 a, Vector2 b) { return Vector2(std::max(a.X, b.X), std::max(a.Y, b.Y)); }
    static Vector2 Clamp(Vector2 v, Vector2 a, Vector2 b) { return Min(Max(v, a), b); }
    static Vector2 Transform(Vector2 v, const Matrix& m);
    static Vector2 Negate(Vector2 v) { return Vector2(-v.X, -v.Y); }
    friend Vector2 operator+(Vector2 a, Vector2 b) { return Vector2(a.X + b.X, a.Y + b.Y); }
    friend Vector2 operator-(Vector2 a, Vector2 b) { return Vector2(a.X - b.X, a.Y - b.Y); }
    friend Vector2 operator*(Vector2 a, Vector2 b) { return Vector2(a.X * b.X, a.Y * b.Y); }
    friend Vector2 operator*(Vector2 a, float s) { return Vector2(a.X * s, a.Y * s); }
    friend Vector2 operator*(float s, Vector2 a) { return Vector2(a.X * s, a.Y * s); }
    friend Vector2 operator/(Vector2 a, float s) { float f = 1.0f / s; return Vector2(a.X * f, a.Y * f); }
    friend Vector2 operator/(Vector2 a, Vector2 b) { return Vector2(a.X / b.X, a.Y / b.Y); }
    friend Vector2 operator-(Vector2 a) { return Vector2(-a.X, -a.Y); }
    friend bool operator==(Vector2 a, Vector2 b) { return a.X == b.X && a.Y == b.Y; }
    friend bool operator!=(Vector2 a, Vector2 b) { return !(a == b); }
    Vector2& operator+=(Vector2 b) { X += b.X; Y += b.Y; return *this; }
    Vector2& operator-=(Vector2 b) { X -= b.X; Y -= b.Y; return *this; }
    Vector2& operator*=(float s) { X *= s; Y *= s; return *this; }
    Vector2& operator*=(Vector2 b) { X *= b.X; Y *= b.Y; return *this; }
    Vector2& operator/=(float s) { X /= s; Y /= s; return *this; }
    String* ToString();
    int32_t GetHashCode() { return ::sys::hash_float(X) ^ (::sys::hash_float(Y) * 31); }
    bool Equals(::System::Object* o) { return ::sys::Equals(*this, o); }
};

struct Vector3
{
    float X = 0, Y = 0, Z = 0;
    Vector3() {}
    Vector3(float x, float y, float z) : X(x), Y(y), Z(z) {}
    Vector3(Vector2 v, float z) : X(v.X), Y(v.Y), Z(z) {}
    static Vector3 get_Zero() { return Vector3(); }
    static Vector3 get_One() { return Vector3(1, 1, 1); }
    friend Vector3 operator+(Vector3 a, Vector3 b) { return Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z); }
    friend Vector3 operator-(Vector3 a, Vector3 b) { return Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z); }
    friend Vector3 operator*(Vector3 a, float s) { return Vector3(a.X * s, a.Y * s, a.Z * s); }
    friend bool operator==(Vector3 a, Vector3 b) { return a.X == b.X && a.Y == b.Y && a.Z == b.Z; }
    friend bool operator!=(Vector3 a, Vector3 b) { return !(a == b); }
    String* ToString();
    int32_t GetHashCode() { return ::sys::hash_float(X) ^ ::sys::hash_float(Y) ^ ::sys::hash_float(Z); }
};

struct Vector4
{
    float X = 0, Y = 0, Z = 0, W = 0;
    Vector4() {}
    Vector4(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}
    String* ToString();
    int32_t GetHashCode() { return ::sys::hash_float(X) ^ ::sys::hash_float(W); }
};

struct Point
{
    int32_t X = 0, Y = 0;
    Point() {}
    Point(int32_t x, int32_t y) : X(x), Y(y) {}
    static Point get_Zero() { return Point(); }
    friend bool operator==(Point a, Point b) { return a.X == b.X && a.Y == b.Y; }
    friend bool operator!=(Point a, Point b) { return !(a == b); }
    friend Point operator+(Point a, Point b) { return Point(a.X + b.X, a.Y + b.Y); }
    friend Point operator-(Point a, Point b) { return Point(a.X - b.X, a.Y - b.Y); }
    String* ToString();
    int32_t GetHashCode() { return X ^ (Y * 7919); }
    bool Equals(::System::Object* o) { return ::sys::Equals(*this, o); }
};

struct Rectangle
{
    int32_t X = 0, Y = 0, Width = 0, Height = 0;
    Rectangle() {}
    Rectangle(int32_t x, int32_t y, int32_t w, int32_t h) : X(x), Y(y), Width(w), Height(h) {}
    static Rectangle get_Empty() { return Rectangle(); }
    int32_t get_Left() const { return X; }
    int32_t get_Right() const { return X + Width; }
    int32_t get_Top() const { return Y; }
    int32_t get_Bottom() const { return Y + Height; }
    Point get_Center() const { return Point(X + Width / 2, Y + Height / 2); }
    Point get_Location() const { return Point(X, Y); }
    void set_Location(Point p) { X = p.X; Y = p.Y; }
    bool get_IsEmpty() const { return X == 0 && Y == 0 && Width == 0 && Height == 0; }
    bool Contains(int32_t x, int32_t y) const { return X <= x && x < X + Width && Y <= y && y < Y + Height; }
    bool Contains(Point p) const { return Contains(p.X, p.Y); }
    bool Contains(Vector2 p) const { return X <= p.X && p.X < X + Width && Y <= p.Y && p.Y < Y + Height; }
    bool Contains(Rectangle r) const { return X <= r.X && r.X + r.Width <= X + Width && Y <= r.Y && r.Y + r.Height <= Y + Height; }
    bool Intersects(Rectangle r) const { return r.X < X + Width && X < r.X + r.Width && r.Y < Y + Height && Y < r.Y + r.Height; }
    void Offset(int32_t dx, int32_t dy) { X += dx; Y += dy; }
    void Offset(Point p) { X += p.X; Y += p.Y; }
    void Inflate(int32_t h, int32_t v) { X -= h; Y -= v; Width += h * 2; Height += v * 2; }
    static Rectangle Intersect(Rectangle a, Rectangle b);
    static Rectangle Union(Rectangle a, Rectangle b);
    friend bool operator==(Rectangle a, Rectangle b) { return a.X == b.X && a.Y == b.Y && a.Width == b.Width && a.Height == b.Height; }
    friend bool operator!=(Rectangle a, Rectangle b) { return !(a == b); }
    String* ToString();
    int32_t GetHashCode() { return X ^ Y ^ Width ^ Height; }
};

struct Color
{
    uint8_t r = 0, g = 0, b = 0, a = 0;
    Color() {}
    Color(int32_t R, int32_t G, int32_t B) : r(clampb(R)), g(clampb(G)), b(clampb(B)), a(255) {}
    Color(int32_t R, int32_t G, int32_t B, int32_t A) : r(clampb(R)), g(clampb(G)), b(clampb(B)), a(clampb(A)) {}
    Color(float R, float G, float B) : r(clampf(R)), g(clampf(G)), b(clampf(B)), a(255) {}
    Color(float R, float G, float B, float A) : r(clampf(R)), g(clampf(G)), b(clampf(B)), a(clampf(A)) {}
    Color(Vector3 v) : Color(v.X, v.Y, v.Z) {}
    Color(Vector4 v) : Color(v.X, v.Y, v.Z, v.W) {}
    Color(Color c, float alpha) : r(c.r), g(c.g), b(c.b), a(clampf(alpha)) {}
    Color(Color c, int32_t alpha) : r(c.r), g(c.g), b(c.b), a(clampb(alpha)) {}
    static uint8_t clampb(int32_t v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
    static uint8_t clampf(float v) { float x = v * 255.0f; if (!(x > 0)) return 0; if (x > 255) return 255; return (uint8_t)(x + 0.5f); }
    uint8_t get_R() const { return r; } void set_R(uint8_t v) { r = v; }
    uint8_t get_G() const { return g; } void set_G(uint8_t v) { g = v; }
    uint8_t get_B() const { return b; } void set_B(uint8_t v) { b = v; }
    uint8_t get_A() const { return a; } void set_A(uint8_t v) { a = v; }
    uint32_t get_PackedValue() const { return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24); }
    Vector3 ToVector3() const { return Vector3(r / 255.0f, g / 255.0f, b / 255.0f); }
    Vector4 ToVector4() const { return Vector4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f); }
    static Color Lerp(Color a, Color b, float t);
    static Color FromNonPremultiplied(int32_t R, int32_t G, int32_t B, int32_t A) { return Color(R * A / 255, G * A / 255, B * A / 255, A); }
    static Color Multiply(Color c, float s) { return c * s; }
    friend Color operator*(Color c, float s)
    {
        Color o; o.r = clampf(c.r / 255.0f * s); o.g = clampf(c.g / 255.0f * s); o.b = clampf(c.b / 255.0f * s); o.a = clampf(c.a / 255.0f * s);
        // XNA uses truncating integer math: (int)(c * s)
        auto m = [s](uint8_t v) -> uint8_t { float x = v * s; if (!(x > 0)) return 0; if (x > 255) return 255; return (uint8_t)x; };
        o.r = m(c.r); o.g = m(c.g); o.b = m(c.b); o.a = m(c.a);
        return o;
    }
    friend bool operator==(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }
    friend bool operator!=(Color a, Color b) { return !(a == b); }
    String* ToString();
    int32_t GetHashCode() { return (int32_t)get_PackedValue(); }
    bool Equals(::System::Object* o) { return ::sys::Equals(*this, o); }
#define XC(N, R, G, B, A) static Color get_##N() { return Color((int32_t)R, (int32_t)G, (int32_t)B, (int32_t)A); }
    XC(Transparent, 0, 0, 0, 0) XC(White, 255, 255, 255, 255) XC(Black, 0, 0, 0, 255) XC(Red, 255, 0, 0, 255)
    XC(Lime, 0, 255, 0, 255) XC(Green, 0, 128, 0, 255) XC(Blue, 0, 0, 255, 255) XC(Yellow, 255, 255, 0, 255)
    XC(Cyan, 0, 255, 255, 255) XC(Aqua, 0, 255, 255, 255) XC(Magenta, 255, 0, 255, 255) XC(Gray, 128, 128, 128, 255)
    XC(LightGray, 211, 211, 211, 255) XC(DarkGray, 169, 169, 169, 255) XC(DarkRed, 139, 0, 0, 255) XC(Orange, 255, 165, 0, 255)
    XC(Purple, 128, 0, 128, 255) XC(Pink, 255, 192, 203, 255) XC(Gold, 255, 215, 0, 255) XC(CornflowerBlue, 100, 149, 237, 255)
    XC(DarkBlue, 0, 0, 139, 255) XC(DarkGreen, 0, 100, 0, 255) XC(Brown, 165, 42, 42, 255) XC(Silver, 192, 192, 192, 255)
    XC(LimeGreen, 50, 205, 50, 255) XC(HotPink, 255, 105, 180, 255) XC(SkyBlue, 135, 206, 235, 255) XC(Teal, 0, 128, 128, 255)
    XC(Violet, 238, 130, 238, 255) XC(Navy, 0, 0, 128, 255) XC(Maroon, 128, 0, 0, 255) XC(Olive, 128, 128, 0, 255)
    XC(DimGray, 105, 105, 105, 255) XC(OrangeRed, 255, 69, 0, 255) XC(Indigo, 75, 0, 130, 255) XC(Crimson, 220, 20, 60, 255)
#undef XC
};

struct Matrix
{
    float M11 = 0, M12 = 0, M13 = 0, M14 = 0, M21 = 0, M22 = 0, M23 = 0, M24 = 0, M31 = 0, M32 = 0, M33 = 0, M34 = 0, M41 = 0, M42 = 0, M43 = 0, M44 = 0;
    Matrix() {}
    static Matrix get_Identity() { Matrix m; m.M11 = m.M22 = m.M33 = m.M44 = 1; return m; }
    static Matrix CreateTranslation(float x, float y, float z) { Matrix m = get_Identity(); m.M41 = x; m.M42 = y; m.M43 = z; return m; }
    static Matrix CreateTranslation(Vector3 v) { return CreateTranslation(v.X, v.Y, v.Z); }
    static Matrix CreateScale(float s) { Matrix m = get_Identity(); m.M11 = m.M22 = m.M33 = s; return m; }
    static Matrix CreateScale(float x, float y, float z) { Matrix m = get_Identity(); m.M11 = x; m.M22 = y; m.M33 = z; return m; }
    static Matrix CreateScale(Vector3 v) { return CreateScale(v.X, v.Y, v.Z); }
    static Matrix CreateRotationZ(float r) { Matrix m = get_Identity(); float c = std::cos(r), s = std::sin(r); m.M11 = c; m.M12 = s; m.M21 = -s; m.M22 = c; return m; }
    static Matrix Invert(Matrix m);
    static Matrix Lerp(Matrix a, Matrix b, float t);
    friend Matrix operator*(const Matrix& a, const Matrix& b);
    Vector3 get_Translation() const { return Vector3(M41, M42, M43); }
    String* ToString();
    int32_t GetHashCode() { return ::sys::hash_float(M11) ^ ::sys::hash_float(M41); }
};

struct MathHelper
{
    static constexpr float Pi = 3.14159274f;
    static float Clamp(float v, float a, float b) { v = v > b ? b : v; v = v < a ? a : v; return v; }
    static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
    static float Max(float a, float b) { return a > b ? a : b; }
    static float Min(float a, float b) { return a < b ? a : b; }
    static float Distance(float a, float b) { return std::fabs(a - b); }
    static float ToRadians(float d) { return d * 0.0174532924f; }
    static float ToDegrees(float r) { return r * 57.2957764f; }
    static float WrapAngle(float a);
    static float SmoothStep(float a, float b, float t) { t = Clamp(t, 0, 1); return Lerp(a, b, t * t * (3 - 2 * t)); }
};

struct GameTime final : Object
{
    ::System::TimeSpan elapsed, total;
    ::System::TimeSpan get_ElapsedGameTime() { return elapsed; }
    ::System::TimeSpan get_TotalGameTime() { return total; }
    bool get_IsRunningSlowly() { return false; }
};

struct GameWindow final : Object
{
    String* title = nullptr;
    void set_Title(String* t) { title = t; }
    String* get_Title() { return title; }
    Rectangle get_ClientBounds();
    bool get_AllowUserResizing() { return false; }
    void set_AllowUserResizing(bool) {}
};

namespace Graphics { struct GraphicsDevice; }
namespace Content { struct ContentManager; }

struct Game;

struct GraphicsDeviceManager final : Object
{
    Game* game;
    int32_t PreferredBackBufferWidth = 1280, PreferredBackBufferHeight = 720;
    bool isFullScreen = false;
    ::sys::Fn<void(Object*, ::System::EventArgs*)>* DeviceReset = nullptr;
    ::sys::Fn<void(Object*, ::System::EventArgs*)>* DeviceCreated = nullptr;
    explicit GraphicsDeviceManager(Game* g);
    void ApplyChanges();
    bool get_IsFullScreen() { return isFullScreen; }
    void set_IsFullScreen(bool v) { isFullScreen = v; }
    void ToggleFullScreen() { isFullScreen = !isFullScreen; ApplyChanges(); }
    int32_t get_PreferredBackBufferWidth() { return PreferredBackBufferWidth; }
    void set_PreferredBackBufferWidth(int32_t v) { PreferredBackBufferWidth = v; }
    int32_t get_PreferredBackBufferHeight() { return PreferredBackBufferHeight; }
    void set_PreferredBackBufferHeight(int32_t v) { PreferredBackBufferHeight = v; }
    bool get_SynchronizeWithVerticalRetrace() { return true; }
    void set_SynchronizeWithVerticalRetrace(bool) {}
    Graphics::GraphicsDevice* get_GraphicsDevice();
};

struct Game : Object, ::System::IDisposable
{
    Content::ContentManager* content = nullptr;
    GameWindow* window = nullptr;
    bool isFixedTimeStep = true;
    bool isMouseVisible = false;
    bool exitRequested = false;
    ::System::TimeSpan targetElapsedTime = ::System::TimeSpan::FromSeconds(1.0 / 60.0);
    Game();
    virtual ~Game() {}
    Content::ContentManager* get_Content() { return content; }
    Graphics::GraphicsDevice* get_GraphicsDevice();
    GameWindow* get_Window() { return window; }
    bool get_IsActive() { return true; }
    bool get_IsFixedTimeStep() { return isFixedTimeStep; }
    void set_IsFixedTimeStep(bool v) { isFixedTimeStep = v; }
    bool get_IsMouseVisible() { return isMouseVisible; }
    void set_IsMouseVisible(bool v) { isMouseVisible = v; }
    ::System::TimeSpan get_TargetElapsedTime() { return targetElapsedTime; }
    void set_TargetElapsedTime(::System::TimeSpan t) { targetElapsedTime = t; }
    void Run();
    void Exit() { exitRequested = true; }
    void Dispose() override {}
    virtual void Initialize();
    virtual void LoadContent() {}
    virtual void UnloadContent() {}
    virtual void Update(GameTime* t) {}
    virtual void Draw(GameTime* t) {}
    virtual void OnExiting(Object* sender, ::System::EventArgs* args) {}
    virtual bool BeginDraw() { return true; }
    virtual void EndDraw() {}
};

namespace Graphics {

enum class SpriteSortMode : int32_t { Deferred = 0, Immediate = 1, Texture = 2, BackToFront = 3, FrontToBack = 4 };
enum class SpriteEffects : int32_t { None = 0, FlipHorizontally = 1, FlipVertically = 2 };
enum class SurfaceFormat : int32_t { Color = 0 };
enum class Blend : int32_t { One, Zero, SourceColor, InverseSourceColor, SourceAlpha, InverseSourceAlpha, DestinationColor, InverseDestinationColor, DestinationAlpha, InverseDestinationAlpha, BlendFactor, InverseBlendFactor, SourceAlphaSaturation };
enum class BlendFunction : int32_t { Add, Subtract, ReverseSubtract, Max, Min };
enum class CompareFunction : int32_t { Always, Never, Less, LessEqual, Equal, GreaterEqual, Greater, NotEqual };
enum class TextureFilter : int32_t { Linear, Point, Anisotropic, LinearMipPoint, PointMipLinear, MinLinearMagPointMipLinear, MinLinearMagPointMipPoint, MinPointMagLinearMipLinear, MinPointMagLinearMipPoint };
enum class TextureAddressMode : int32_t { Wrap, Clamp, Mirror, Border };
enum class CullMode : int32_t { None, CullClockwiseFace, CullCounterClockwiseFace };
enum class PrimitiveType : int32_t { TriangleList, TriangleStrip, LineList, LineStrip };

struct GraphicsResource : Object
{
    bool disposed = false;
    virtual void Dispose() { disposed = true; }
    bool get_IsDisposed() { return disposed; }
};

struct BlendState final : GraphicsResource
{
    Blend ColorSourceBlend = Blend::One, ColorDestinationBlend = Blend::Zero, AlphaSourceBlend = Blend::One, AlphaDestinationBlend = Blend::Zero;
    BlendFunction ColorBlendFunction = BlendFunction::Add, AlphaBlendFunction = BlendFunction::Add;
    int sdlMode = -1;   // cached SDL_BlendMode
    BlendState() {}
    BlendState(Blend cs, Blend cd, Blend as, Blend ad) : ColorSourceBlend(cs), ColorDestinationBlend(cd), AlphaSourceBlend(as), AlphaDestinationBlend(ad) {}
    static BlendState* AlphaBlend;
    static BlendState* Additive;
    static BlendState* Opaque;
    static BlendState* NonPremultiplied;
    void set_ColorSourceBlend(Blend b) { ColorSourceBlend = b; sdlMode = -1; }
    void set_ColorDestinationBlend(Blend b) { ColorDestinationBlend = b; sdlMode = -1; }
    void set_AlphaSourceBlend(Blend b) { AlphaSourceBlend = b; sdlMode = -1; }
    void set_AlphaDestinationBlend(Blend b) { AlphaDestinationBlend = b; sdlMode = -1; }
    void set_ColorBlendFunction(BlendFunction b) { ColorBlendFunction = b; sdlMode = -1; }
    void set_AlphaBlendFunction(BlendFunction b) { AlphaBlendFunction = b; sdlMode = -1; }
    Blend get_ColorSourceBlend() { return ColorSourceBlend; }
    int SDLMode();
};
struct SamplerState final : GraphicsResource
{
    TextureFilter Filter = TextureFilter::Point;
    TextureAddressMode AddressU = TextureAddressMode::Clamp, AddressV = TextureAddressMode::Clamp, AddressW = TextureAddressMode::Clamp;
    static SamplerState* PointClamp;
    static SamplerState* PointWrap;
    static SamplerState* LinearClamp;
    static SamplerState* LinearWrap;
    static SamplerState* AnisotropicClamp;
};
struct DepthStencilState final : GraphicsResource
{
    static DepthStencilState* None;
    static DepthStencilState* Default;
    static DepthStencilState* DepthRead;
};
struct RasterizerState final : GraphicsResource
{
    CullMode cull = CullMode::None;
    bool ScissorTestEnable = false;
    static RasterizerState* CullNone;
    static RasterizerState* CullClockwise;
    static RasterizerState* CullCounterClockwise;
};

struct Texture : GraphicsResource {};

struct Texture2D : Texture, ::sys::Finalizable
{
    SDL_Texture* tex = nullptr;
    int32_t width = 0, height = 0;
    bool isTarget = false;
    std::vector<uint32_t> cpuPixels;     // only kept when needed
    std::string sourcePath;              // to reload pixels for GetData
    int scaleMode = 0;
    Texture2D() {}
    Texture2D(GraphicsDevice* gd, int32_t w, int32_t h);
    Texture2D(GraphicsDevice* gd, int32_t w, int32_t h, bool mip, SurfaceFormat fmt);
    ~Texture2D();
    void Dispose() override;
    int32_t get_Width() { return width; }
    int32_t get_Height() { return height; }
    Rectangle get_Bounds() { return Rectangle(0, 0, width, height); }
    static Texture2D* FromStream(GraphicsDevice* gd, ::System::IO::Stream* s);
    static Texture2D* FromPixels(int32_t w, int32_t h, const uint32_t* rgba, bool target = false);
    void SetPixels(const uint32_t* rgba);
    void ReadPixels(std::vector<uint32_t>& out);
    template<class T> void SetData(::System::Array<T>* data) { static_assert(sizeof(T) == 4, "Color data"); SetPixels((const uint32_t*)data->data); }
    template<class T> void GetData(::System::Array<T>* data)
    {
        static_assert(sizeof(T) == 4, "Color data");
        std::vector<uint32_t> px; ReadPixels(px);
        size_t n = std::min((size_t)data->length, px.size());
        memcpy(data->data, px.data(), n * 4);
    }
};

struct RenderTarget2D final : Texture2D
{
    RenderTarget2D(GraphicsDevice* gd, int32_t w, int32_t h);
    bool get_IsContentLost() { return false; }
};

struct EffectParameter final : Object
{
    String* name = nullptr;
    float value = 0;
    void SetValue(float v) { value = v; }
    float GetValueSingle() { return value; }
};
struct EffectParameterCollection final : Object
{
    ::System::Collections::Generic::List<EffectParameter*>* items = nullptr;
    EffectParameterCollection();
    EffectParameter* get_Item(String* name);
};
enum class EffectKind : int32_t { Unknown, Lighting, ScreenWave };
struct Effect final : GraphicsResource
{
    EffectKind kind = EffectKind::Unknown;
    EffectParameterCollection* parameters = nullptr;
    Effect() { parameters = ::sys::gcnew<EffectParameterCollection>(); }
    EffectParameterCollection* get_Parameters() { return parameters; }
    float Param(const char* name);
};

struct Viewport
{
    int32_t x = 0, y = 0, width = 0, height = 0;
    Viewport() {}
    Viewport(int32_t X, int32_t Y, int32_t w, int32_t h) : x(X), y(Y), width(w), height(h) {}
    int32_t get_X() const { return x; } void set_X(int32_t v) { x = v; }
    int32_t get_Y() const { return y; } void set_Y(int32_t v) { y = v; }
    int32_t get_Width() const { return width; } void set_Width(int32_t v) { width = v; }
    int32_t get_Height() const { return height; } void set_Height(int32_t v) { height = v; }
    Rectangle get_Bounds() const { return Rectangle(x, y, width, height); }
    String* ToString();
    int32_t GetHashCode() { return x ^ y ^ width ^ height; }
};

struct PresentationParameters final : Object
{
    int32_t BackBufferWidth = 1280, BackBufferHeight = 720;
    int32_t get_BackBufferWidth() { return BackBufferWidth; }
    int32_t get_BackBufferHeight() { return BackBufferHeight; }
};
struct DisplayMode final : Object
{
    int32_t width = 1280, height = 720;
    int32_t get_Width() { return width; }
    int32_t get_Height() { return height; }
};

struct TextureCollection final : Object
{
    Texture* slots[8] = {};
    Texture* get_Item(int32_t i) { return slots[i & 7]; }
    void set_Item(int32_t i, Texture* t) { slots[i & 7] = t; }
};
struct SamplerStateCollection final : Object
{
    SamplerState* slots[8] = {};
    SamplerState* get_Item(int32_t i) { return slots[i & 7]; }
    void set_Item(int32_t i, SamplerState* t) { slots[i & 7] = t; }
};

struct GraphicsDevice final : Object
{
    RenderTarget2D* currentTarget = nullptr;
    Viewport viewport;
    TextureCollection* textures;
    SamplerStateCollection* samplers;
    PresentationParameters* pp;
    DisplayMode* displayMode;
    BlendState* blendState = nullptr;
    DepthStencilState* depthState = nullptr;
    RasterizerState* rasterState = nullptr;
    Rectangle scissor;
    GraphicsDevice();
    void Clear(Color c);
    void SetRenderTarget(RenderTarget2D* rt);
    RenderTarget2D* GetCurrentTarget() { return currentTarget; }
    Viewport get_Viewport() { return viewport; }
    void set_Viewport(Viewport v);
    TextureCollection* get_Textures() { return textures; }
    SamplerStateCollection* get_SamplerStates() { return samplers; }
    PresentationParameters* get_PresentationParameters() { return pp; }
    DisplayMode* get_DisplayMode() { return displayMode; }
    BlendState* get_BlendState() { return blendState; }
    void set_BlendState(BlendState* b) { blendState = b; }
    DepthStencilState* get_DepthStencilState() { return depthState; }
    void set_DepthStencilState(DepthStencilState* d) { depthState = d; }
    RasterizerState* get_RasterizerState() { return rasterState; }
    void set_RasterizerState(RasterizerState* r) { rasterState = r; }
    Rectangle get_ScissorRectangle() { return scissor; }
    void set_ScissorRectangle(Rectangle r) { scissor = r; }
    void Present();
    static GraphicsDevice* Instance;
    void ApplyTarget();   // re-applies current SDL render target + viewport (implementation detail)
};

// Call once after SDL / the renderer is up (plat::init()). Creates the static BlendState/SamplerState/...
// instances, registers them (and GraphicsDevice::Instance) as GC roots and probes SDL blend-op direction.
void xna_graphics_init();

struct SpriteFont final : Object
{
    Texture2D* texture = nullptr;
    std::vector<char16_t> chars;
    std::vector<Rectangle> glyphs, cropping;
    std::vector<Vector3> kerning;
    int32_t lineSpacing = 0;
    float spacing = 0;
    int32_t defaultChar = -1;
    int32_t get_LineSpacing() { return lineSpacing; }
    void set_LineSpacing(int32_t v) { lineSpacing = v; }
    float get_Spacing() { return spacing; }
    void set_Spacing(float v) { spacing = v; }
    int32_t Index(char16_t c);
    Vector2 MeasureString(String* s);
    Vector2 MeasureString(::System::Text::StringBuilder* s) { return MeasureString(s->ToString()); }
};

struct SpriteBatch final : GraphicsResource
{
    GraphicsDevice* device;
    // ---- batch state (implementation detail, graphics.cpp)
    bool begun = false;
    SpriteSortMode sortMode = SpriteSortMode::Deferred;
    BlendState* blend = nullptr;
    SamplerState* sampler = nullptr;
    Effect* effect = nullptr;
    Matrix matrix;
    void* items = nullptr;            // GC-allocated block of sprite records (scanned conservatively)
    int32_t itemCount = 0, itemCap = 0;
    // src in texels; dest position/size in pixels; origin in texels of src; fx = SpriteEffects bits
    void PushSprite(Texture2D* t, float srcX, float srcY, float srcW, float srcH, float dstX, float dstY, float dstW, float dstH,
                    Color c, float originX, float originY, float rot, int fx, float depth);
    void Flush();
    explicit SpriteBatch(GraphicsDevice* gd) : device(gd) {}
    void Begin();
    void Begin(SpriteSortMode m, BlendState* b);
    void Begin(SpriteSortMode m, BlendState* b, SamplerState* s, DepthStencilState* d, RasterizerState* r);
    void Begin(SpriteSortMode m, BlendState* b, SamplerState* s, DepthStencilState* d, RasterizerState* r, Effect* e);
    void Begin(SpriteSortMode m, BlendState* b, SamplerState* s, DepthStencilState* d, RasterizerState* r, Effect* e, Matrix transform);
    void End();
    void Draw(Texture2D* t, Vector2 pos, Color c);
    void Draw(Texture2D* t, Rectangle dest, Color c);
    void Draw(Texture2D* t, Vector2 pos, ::System::Nullable<Rectangle> src, Color c);
    void Draw(Texture2D* t, Rectangle dest, ::System::Nullable<Rectangle> src, Color c);
    void Draw(Texture2D* t, Vector2 pos, ::System::Nullable<Rectangle> src, Color c, float rot, Vector2 origin, float scale, SpriteEffects fx, float depth);
    void Draw(Texture2D* t, Vector2 pos, ::System::Nullable<Rectangle> src, Color c, float rot, Vector2 origin, Vector2 scale, SpriteEffects fx, float depth);
    void Draw(Texture2D* t, Rectangle dest, ::System::Nullable<Rectangle> src, Color c, float rot, Vector2 origin, SpriteEffects fx, float depth);
    void DrawString(SpriteFont* f, String* s, Vector2 pos, Color c);
    void DrawString(SpriteFont* f, String* s, Vector2 pos, Color c, float rot, Vector2 origin, float scale, SpriteEffects fx, float depth);
    void DrawString(SpriteFont* f, String* s, Vector2 pos, Color c, float rot, Vector2 origin, Vector2 scale, SpriteEffects fx, float depth);
    void DrawString(SpriteFont* f, ::System::Text::StringBuilder* s, Vector2 pos, Color c) { DrawString(f, s->ToString(), pos, c); }
};

// Unused-by-port (XNAFileDialog) placeholders so signatures resolve
struct VertexBuffer : GraphicsResource {};

} // namespace Graphics

namespace Content {
struct ContentManager final : Object
{
    String* RootDirectory = nullptr;
    String* get_RootDirectory() { return RootDirectory; }
    void set_RootDirectory(String* s) { RootDirectory = s; }
    template<class T> T Load(String* name);
    void Unload() {}
    Graphics::SpriteFont* LoadFont(String* name);
    Graphics::Effect* LoadEffect(String* name);
};
template<class T> T ContentManager::Load(String* name)
{
    if constexpr (std::is_same_v<T, Graphics::SpriteFont*>) return LoadFont(name);
    else if constexpr (std::is_same_v<T, Graphics::Effect*>) return LoadEffect(name);
    else { static_assert(sizeof(T) == 0, "unsupported content type"); }
}
}

namespace Input {

enum class ButtonState : int32_t { Released = 0, Pressed = 1 };
enum class KeyState : int32_t { Up = 0, Down = 1 };
enum class Buttons : int32_t
{
    DPadUp = 1, DPadDown = 2, DPadLeft = 4, DPadRight = 8, Start = 16, Back = 32, LeftStick = 64, RightStick = 128,
    LeftShoulder = 256, RightShoulder = 512, BigButton = 2048, A = 4096, B = 8192, X = 16384, Y = 32768,
    LeftThumbstickLeft = 2097152, RightTrigger = 4194304, LeftTrigger = 8388608, RightThumbstickUp = 16777216,
    RightThumbstickDown = 33554432, RightThumbstickRight = 67108864, RightThumbstickLeft = 134217728,
    LeftThumbstickUp = 268435456, LeftThumbstickDown = 536870912, LeftThumbstickRight = 1073741824
};
enum class Keys : int32_t
{
    None = 0, Back = 8, Tab = 9, Enter = 13, Pause = 19, CapsLock = 20, Escape = 27, Space = 32, PageUp = 33, PageDown = 34,
    End = 35, Home = 36, Left = 37, Up = 38, Right = 39, Down = 40, Select = 41, Print = 42, Execute = 43, PrintScreen = 44,
    Insert = 45, Delete = 46, Help = 47, D0 = 48, D1, D2, D3, D4, D5, D6, D7, D8, D9,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftWindows = 91, RightWindows = 92, Apps = 93, Sleep = 95, NumPad0 = 96, NumPad1, NumPad2, NumPad3, NumPad4, NumPad5,
    NumPad6, NumPad7, NumPad8, NumPad9, Multiply = 106, Add = 107, Separator = 108, Subtract = 109, Decimal = 110, Divide = 111,
    F1 = 112, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
    NumLock = 144, Scroll = 145, LeftShift = 160, RightShift = 161, LeftControl = 162, RightControl = 163, LeftAlt = 164,
    RightAlt = 165, BrowserBack = 166, BrowserForward = 167, BrowserRefresh = 168, BrowserStop = 169, BrowserSearch = 170,
    BrowserFavorites = 171, BrowserHome = 172, VolumeMute = 173, VolumeDown = 174, VolumeUp = 175, MediaNextTrack = 176,
    MediaPreviousTrack = 177, MediaStop = 178, MediaPlayPause = 179, LaunchMail = 180, SelectMedia = 181, LaunchApplication1 = 182,
    LaunchApplication2 = 183, OemSemicolon = 186, OemPlus = 187, OemComma = 188, OemMinus = 189, OemPeriod = 190,
    OemQuestion = 191, OemTilde = 192, ChatPadGreen = 202, ChatPadOrange = 203, OemOpenBrackets = 219, OemPipe = 220,
    OemCloseBrackets = 221, OemQuotes = 222, Oem8 = 223, OemBackslash = 226, ProcessKey = 229, OemCopy = 242, OemAuto = 243,
    OemEnlW = 244, Attn = 246, Crsel = 247, Exsel = 248, EraseEof = 249, Play = 250, Zoom = 251, Pa1 = 253, OemClear = 254,
    ImeConvert = 28, ImeNoConvert = 29, Kana = 21, Kanji = 25
};

struct KeyboardState
{
    uint32_t keys[8] = {};
    KeyState get_Item(Keys k) const { int i = (int)k; return (i >= 0 && i < 256 && (keys[i >> 5] & (1u << (i & 31)))) ? KeyState::Down : KeyState::Up; }
    bool IsKeyDown(Keys k) const { return get_Item(k) == KeyState::Down; }
    bool IsKeyUp(Keys k) const { return !IsKeyDown(k); }
    ::System::Array<Keys>* GetPressedKeys() const;
    void Set(Keys k) { int i = (int)k; if (i >= 0 && i < 256) keys[i >> 5] |= 1u << (i & 31); }
};
struct Keyboard { static KeyboardState GetState(); static KeyboardState GetState(PlayerIndex) { return GetState(); } };

struct MouseState
{
    int32_t x = 0, y = 0, wheel = 0;
    ButtonState left = ButtonState::Released, middle = ButtonState::Released, right = ButtonState::Released;
    MouseState() {}
    MouseState(int32_t X, int32_t Y, int32_t w, ButtonState l, ButtonState m, ButtonState r, ButtonState, ButtonState) : x(X), y(Y), wheel(w), left(l), middle(m), right(r) {}
    int32_t get_X() const { return x; }
    int32_t get_Y() const { return y; }
    int32_t get_ScrollWheelValue() const { return wheel; }
    ButtonState get_LeftButton() const { return left; }
    ButtonState get_MiddleButton() const { return middle; }
    ButtonState get_RightButton() const { return right; }
    friend bool operator==(const MouseState& a, const MouseState& b) { return memcmp(&a, &b, sizeof(MouseState)) == 0; }
    int32_t GetHashCode() { return x ^ y; }
};
struct Mouse { static MouseState GetState(); static void SetPosition(int32_t, int32_t) {} };

struct GamePadDPad
{
    ButtonState up = ButtonState::Released, down = ButtonState::Released, left = ButtonState::Released, right = ButtonState::Released;
    ButtonState get_Up() const { return up; }
    ButtonState get_Down() const { return down; }
    ButtonState get_Left() const { return left; }
    ButtonState get_Right() const { return right; }
    int32_t GetHashCode() { return 0; }
};
struct GamePadThumbSticks
{
    Vector2 left, right;
    Vector2 get_Left() const { return left; }
    Vector2 get_Right() const { return right; }
    int32_t GetHashCode() { return 0; }
};
struct GamePadTriggers
{
    float left = 0, right = 0;
    float get_Left() const { return left; }
    float get_Right() const { return right; }
    int32_t GetHashCode() { return 0; }
};
struct GamePadButtons
{
    int32_t buttons = 0;
    int32_t GetHashCode() { return buttons; }
};
struct GamePadState
{
    bool connected = false;
    int32_t buttons = 0;
    GamePadDPad dpad;
    GamePadThumbSticks sticks;
    GamePadTriggers triggers;
    bool get_IsConnected() const { return connected; }
    GamePadDPad get_DPad() const { return dpad; }
    GamePadThumbSticks get_ThumbSticks() const { return sticks; }
    GamePadTriggers get_Triggers() const { return triggers; }
    GamePadButtons get_Buttons() const { GamePadButtons b; b.buttons = buttons; return b; }
    bool IsButtonDown(Buttons b) const;
    bool IsButtonUp(Buttons b) const { return !IsButtonDown(b); }
    int32_t get_PacketNumber() const { return 0; }
    int32_t GetHashCode() { return buttons; }
};
struct GamePadCapabilities { bool IsConnected = false; bool get_IsConnected() { return IsConnected; } };
enum class GamePadDeadZone : int32_t { None, IndependentAxes, Circular };
struct GamePad
{
    static GamePadState GetState(PlayerIndex i);
    static GamePadState GetState(PlayerIndex i, GamePadDeadZone) { return GetState(i); }
    static bool SetVibration(PlayerIndex i, float l, float r);
    static String* GetGUIDEXT(PlayerIndex i);
    static void SetLightBarEXT(PlayerIndex i, Color c) {}
    static GamePadCapabilities GetCapabilities(PlayerIndex i) { GamePadCapabilities c; c.IsConnected = GetState(i).connected; return c; }
};

} // namespace Input

namespace Audio {

enum class SoundState : int32_t { Playing = 0, Paused = 1, Stopped = 2 };
enum class AudioStopOptions : int32_t { AsAuthored = 0, Immediate = 1 };

// Opens the SDL audio device (call after SDL_Init; SDL_INIT_AUDIO is initialised if needed)
// and closes it. Without a device, sound effects / music are silently not played.
void xna_audio_init();
void xna_audio_shutdown();

struct SoundData; // native sample data (not GC)

struct NoAudioHardwareException : ::System::Exception { NoAudioHardwareException() {} explicit NoAudioHardwareException(String* m) : ::System::Exception(m) {} };

struct SoundEffectInstance;
struct SoundEffect final : Object, ::System::IDisposable
{
    SoundData* data = nullptr;
    bool disposed = false;
    static SoundEffect* FromStream(::System::IO::Stream* s);
    SoundEffectInstance* CreateInstance();
    bool Play();
    bool Play(float volume, float pitch, float pan);
    void Dispose() override { disposed = true; }
    bool get_IsDisposed() { return disposed; }
    ::System::TimeSpan get_Duration();
    static float get_MasterVolume();
    static void set_MasterVolume(float v);
};

struct SoundEffectInstance final : Object, ::System::IDisposable, ::sys::Finalizable
{
    SoundEffect* effect = nullptr;
    int voice = -1;            // mixer voice handle
    float volume = 1, pitch = 0, pan = 0;
    bool looped = false;
    bool disposed = false;
    explicit SoundEffectInstance(SoundEffect* e) : effect(e) {}
    ~SoundEffectInstance();
    void Play();
    void Pause();
    void Resume();
    void Stop();
    void Stop(bool immediate) { Stop(); }
    void Dispose() override;
    SoundState get_State();
    float get_Volume() { return volume; }
    void set_Volume(float v);
    float get_Pitch() { return pitch; }
    void set_Pitch(float v);
    float get_Pan() { return pan; }
    void set_Pan(float v);
    bool get_IsLooped() { return looped; }
    void set_IsLooped(bool v) { looped = v; }
    bool get_IsDisposed() { return disposed; }
};

struct AudioEngine;
struct AudioCategory
{
    AudioEngine* engine = nullptr;
    String* name = nullptr;
    int index = -1;              // category index in the engine's .xgs
    String* get_Name() const { return name; }
    void SetVolume(float v);
    void Stop(AudioStopOptions o);
    void Pause();
    void Resume();
    int32_t GetHashCode() { return 0; }
};

struct AudioEngine final : Object, ::System::IDisposable
{
    float musicVolume = 1.0f;
    float categoryGain = 1.0f;       // from the .xgs (music category volume)
    void* native = nullptr;          // xaudio::XactEngine (audio.cpp)
    explicit AudioEngine(String* settingsFile);
    void Update();
    void Dispose() override;
    AudioCategory GetCategory(String* name);
};

struct WaveBank final : Object, ::System::IDisposable
{
    void* native = nullptr;
    WaveBank(AudioEngine* e, String* path);
    void Dispose() override {}
};

struct Cue final : Object
{
    String* name = nullptr;
    void Play() {}
    void Stop(AudioStopOptions) {}
    bool get_IsPlaying() { return true; }
};

struct SoundBank final : Object, ::System::IDisposable
{
    AudioEngine* engine;
    void* native = nullptr;
    SoundBank(AudioEngine* e, String* path);
    void PlayCue(String* name);
    Cue* GetCue(String* name);
    void Dispose() override {}
};

} // namespace Audio

}}} // namespace Microsoft::Xna::Framework
