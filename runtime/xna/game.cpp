// Game loop, window, device manager and input wrappers.
#include "sys/runtime.h"
#include "../platform/platform.h"
#include <SDL.h>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;
using ::System::String;

namespace Microsoft { namespace Xna { namespace Framework {
namespace Graphics { void xna_graphics_init(); }
namespace Audio { void xna_audio_init(); void xna_audio_shutdown(); }
void xna_shutdown();
}}}

// ------------------------------------------------------------------ Game
Game::Game()
{
    content = ::sys::gcnew<Content::ContentManager>();
    window = ::sys::gcnew<GameWindow>();
}

Graphics::GraphicsDevice* Game::get_GraphicsDevice() { return Graphics::GraphicsDevice::Instance; }

void Game::Initialize()
{
    // XNA: base.Initialize() loads content
    LoadContent();
}

static Game* g_game = nullptr;

void Game::Run()
{
    g_game = this;
    ::sys::gc_pin(this);
    if (!plat::init()) { plat::log("platform init failed"); return; }
    Graphics::xna_graphics_init();
    Graphics::GraphicsDevice::Instance = ::sys::gcnew<Graphics::GraphicsDevice>();
    ::sys::gc_pin(Graphics::GraphicsDevice::Instance);
    Audio::xna_audio_init();
    plat::log("Initialize...");
    Initialize();
    plat::loading_done();
    plat::log("Running");
    auto* gt = ::sys::gcnew<GameTime>();
    ::sys::gc_pin(gt);
    int64_t last = plat::now_ns();
    int64_t total = 0;
    while (!exitRequested)
    {
        if (!plat::pump_events()) break;
        plat::update_input();
        int64_t now = plat::now_ns();
        int64_t dt = now - last;
        last = now;
        if (dt > 100000000LL) dt = 100000000LL;   // clamp hitches to 0.1s
        if (dt < 0) dt = 0;
        if (isFixedTimeStep) dt = targetElapsedTime.ticks * 100;
        total += dt;
        gt->elapsed = ::System::TimeSpan(dt / 100);
        gt->total = ::System::TimeSpan(total / 100);
        static int frameNo = 0;
        frameNo++;
        bool trace = frameNo <= 2 || frameNo % 1800 == 0;
        int64_t t0 = plat::now_ns();
        if (trace) { char b[96]; snprintf(b, sizeof b, "frame %d: update", frameNo); plat::log(b); }
        Update(gt);
        if (exitRequested) break;
        int64_t t1 = plat::now_ns();
        if (trace) plat::log("  draw");
        if (BeginDraw())
        {
            Draw(gt);
            EndDraw();
        }
        int64_t t2 = plat::now_ns();
        if (trace) plat::log("  present");
        plat::present();
        if (trace)
        {
            char b[160];
            snprintf(b, sizeof b, "  frame %d done: update %.2fms draw %.2fms present %.2fms heap %zuKB", frameNo,
                     (t1 - t0) / 1e6, (t2 - t1) / 1e6, (plat::now_ns() - t2) / 1e6, ::sys::gc_heap_bytes() / 1024);
            plat::log(b);
        }
    }
    plat::log(exitRequested ? "Game exited (Exit requested)" : "Game exited (platform quit)");
    // An exception here must not skip plat::shutdown(): on Wii U that is what finishes the
    // ProcUI exit and returns to the Wii U Menu.
    try
    {
        OnExiting(this, ::System::EventArgs::get_Empty());
    }
    catch (::System::Exception* e)
    {
        plat::log(("exception in OnExiting: " + e->ToString()->ToUtf8()).c_str());
    }
    xna_shutdown();
}

// Closes audio, then the platform (both idempotent). main() calls this again as a fallback when
// an exception escaped the game loop.
void Microsoft::Xna::Framework::xna_shutdown()
{
    Audio::xna_audio_shutdown();
    plat::shutdown();
}

// ------------------------------------------------------------------ window / device manager
Rectangle GameWindow::get_ClientBounds()
{
    int w, h;
    plat::get_backbuffer(w, h);
    return Rectangle(0, 0, w, h);
}

GraphicsDeviceManager::GraphicsDeviceManager(Game* g) : game(g) {}
void GraphicsDeviceManager::ApplyChanges()
{
    if (plat::is_wiiu())
    {
        PreferredBackBufferWidth = plat::BACKBUFFER_W;
        PreferredBackBufferHeight = plat::BACKBUFFER_H;
    }
    plat::set_backbuffer(PreferredBackBufferWidth, PreferredBackBufferHeight, isFullScreen);
    if (auto* gd = Graphics::GraphicsDevice::Instance)
    {
        int w, h;
        plat::get_backbuffer(w, h);
        gd->pp->BackBufferWidth = w;
        gd->pp->BackBufferHeight = h;
    }
}
Graphics::GraphicsDevice* GraphicsDeviceManager::get_GraphicsDevice() { return Graphics::GraphicsDevice::Instance; }

// ------------------------------------------------------------------ input
KeyboardState Keyboard::GetState()
{
    KeyboardState s;
    for (int k = 1; k < 256; k++) if (plat::key_down(k)) s.Set((Keys)k);
    return s;
}
::System::Array<Keys>* KeyboardState::GetPressedKeys() const
{
    std::vector<Keys> ks;
    for (int k = 1; k < 256; k++) if (keys[k >> 5] & (1u << (k & 31))) ks.push_back((Keys)k);
    auto* a = ::System::Array<Keys>::New((int32_t)ks.size());
    for (size_t i = 0; i < ks.size(); i++) a->data[i] = ks[i];
    return a;
}

MouseState Mouse::GetState()
{
    MouseState m;
    bool l, mi, r;
    plat::mouse_state(m.x, m.y, m.wheel, l, mi, r);
    m.left = l ? ButtonState::Pressed : ButtonState::Released;
    m.middle = mi ? ButtonState::Pressed : ButtonState::Released;
    m.right = r ? ButtonState::Pressed : ButtonState::Released;
    return m;
}

bool GamePadState::IsButtonDown(Buttons b) const
{
    uint32_t v = (uint32_t)b;
    uint32_t mask = (uint32_t)buttons;
    // derived buttons
    if (sticks.left.X < -0.5f) mask |= (uint32_t)Buttons::LeftThumbstickLeft;
    if (sticks.left.X > 0.5f) mask |= (uint32_t)Buttons::LeftThumbstickRight;
    if (sticks.left.Y > 0.5f) mask |= (uint32_t)Buttons::LeftThumbstickUp;
    if (sticks.left.Y < -0.5f) mask |= (uint32_t)Buttons::LeftThumbstickDown;
    if (sticks.right.X < -0.5f) mask |= (uint32_t)Buttons::RightThumbstickLeft;
    if (sticks.right.X > 0.5f) mask |= (uint32_t)Buttons::RightThumbstickRight;
    if (sticks.right.Y > 0.5f) mask |= (uint32_t)Buttons::RightThumbstickUp;
    if (sticks.right.Y < -0.5f) mask |= (uint32_t)Buttons::RightThumbstickDown;
    if (triggers.left > 0.5f) mask |= (uint32_t)Buttons::LeftTrigger;
    if (triggers.right > 0.5f) mask |= (uint32_t)Buttons::RightTrigger;
    return (mask & v) == v;
}

GamePadState GamePad::GetState(PlayerIndex i)
{
    GamePadState s;
    plat::PadState p = plat::pad((int)i);
    s.connected = p.connected;
    if (!p.connected) return s;
    s.buttons = (int32_t)p.buttons;
    auto bs = [&](Buttons b) { return (p.buttons & (uint32_t)b) ? ButtonState::Pressed : ButtonState::Released; };
    s.dpad.up = bs(Buttons::DPadUp);
    s.dpad.down = bs(Buttons::DPadDown);
    s.dpad.left = bs(Buttons::DPadLeft);
    s.dpad.right = bs(Buttons::DPadRight);
    s.sticks.left = Vector2(p.lx, p.ly);
    s.sticks.right = Vector2(p.rx, p.ry);
    s.triggers.left = p.lt;
    s.triggers.right = p.rt;
    return s;
}
bool GamePad::SetVibration(PlayerIndex i, float l, float r)
{
    plat::pad_rumble((int)i, std::max(l, r));
    return true;
}
String* GamePad::GetGUIDEXT(PlayerIndex i) { return String::FromUtf8(plat::pad_guid((int)i)); }
