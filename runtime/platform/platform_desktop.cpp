// Desktop (macOS/Linux/Windows) platform layer for testing the port.
#include "platform.h"
#include "autotest.h"
using namespace plat::autotest;
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <chrono>
#include <thread>
#include <sys/stat.h>
#include <vector>
#include <array>

namespace plat {

static SDL_Window* g_window = nullptr;
static SDL_Renderer* g_renderer = nullptr;
static SDL_GameController* g_pads[MAX_PADS] = {};
static int g_bbw = BACKBUFFER_W, g_bbh = BACKBUFFER_H;
static bool g_keys[256];
static int g_wheel = 0;

static std::string g_root;
static int g_frame = 0;
int& autotest_frame_counter() { return g_frame; }
const char* autotest_config(const char* key) { return getenv(key); }

bool is_wiiu() { return false; }

std::string content_root()
{
    if (!g_root.empty()) return g_root;
    const char* env = getenv("TOWERFALL_ROOT");
    if (env) { g_root = env; return g_root; }
    // default: the original game folder next to the port
    const char* candidates[] = {
        "../TF-8P", "../../TF-8P", "../../../TF-8P",
    };
    for (auto* c : candidates)
    {
        struct stat st;
        std::string p = std::string(c) + "/Content";
        if (stat(p.c_str(), &st) == 0) { g_root = c; return g_root; }
    }
    g_root = ".";
    return g_root;
}

std::string save_dir()
{
    const char* home = getenv("HOME");
    std::string d = home ? std::string(home) + "/Library/Application Support/TowerFallPort" : "./save";
    mkdir(d.c_str(), 0777);
    return d;
}

int64_t now_ns()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
void loading_tick(const char*) {}
void loading_done() {}
void log(const char* msg) { fprintf(stderr, "%s\n", msg); }
void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

bool init()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) { log(SDL_GetError()); return false; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    g_window = SDL_CreateWindow("TowerFall 8P (port)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1260, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_window) { log(SDL_GetError()); return false; }
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!g_renderer) { log(SDL_GetError()); return false; }
    SDL_RenderSetLogicalSize(g_renderer, g_bbw, g_bbh);
    SDL_RendererInfo info;
    SDL_GetRendererInfo(g_renderer, &info);
    fprintf(stderr, "Renderer: %s\n", info.name);
    return true;
}

void shutdown()
{
    static bool done = false;   // also called from main() as a fallback after an exception
    if (done) return;
    done = true;
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
}

SDL_Window* window() { return g_window; }
SDL_Renderer* renderer() { return g_renderer; }

static int MapKey(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 65 + (sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return 49 + (sc - SDL_SCANCODE_1);
    if (sc == SDL_SCANCODE_0) return 48;
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return 112 + (sc - SDL_SCANCODE_F1);
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return 97 + (sc - SDL_SCANCODE_KP_1);
    switch (sc)
    {
    case SDL_SCANCODE_KP_0: return 96;
    case SDL_SCANCODE_BACKSPACE: return 8;
    case SDL_SCANCODE_TAB: return 9;
    case SDL_SCANCODE_RETURN: return 13;
    case SDL_SCANCODE_ESCAPE: return 27;
    case SDL_SCANCODE_SPACE: return 32;
    case SDL_SCANCODE_PAGEUP: return 33;
    case SDL_SCANCODE_PAGEDOWN: return 34;
    case SDL_SCANCODE_END: return 35;
    case SDL_SCANCODE_HOME: return 36;
    case SDL_SCANCODE_LEFT: return 37;
    case SDL_SCANCODE_UP: return 38;
    case SDL_SCANCODE_RIGHT: return 39;
    case SDL_SCANCODE_DOWN: return 40;
    case SDL_SCANCODE_INSERT: return 45;
    case SDL_SCANCODE_DELETE: return 46;
    case SDL_SCANCODE_LSHIFT: return 160;
    case SDL_SCANCODE_RSHIFT: return 161;
    case SDL_SCANCODE_LCTRL: return 162;
    case SDL_SCANCODE_RCTRL: return 163;
    case SDL_SCANCODE_LALT: return 164;
    case SDL_SCANCODE_RALT: return 165;
    case SDL_SCANCODE_SEMICOLON: return 186;
    case SDL_SCANCODE_EQUALS: return 187;
    case SDL_SCANCODE_COMMA: return 188;
    case SDL_SCANCODE_MINUS: return 189;
    case SDL_SCANCODE_PERIOD: return 190;
    case SDL_SCANCODE_SLASH: return 191;
    case SDL_SCANCODE_GRAVE: return 192;
    case SDL_SCANCODE_LEFTBRACKET: return 219;
    case SDL_SCANCODE_BACKSLASH: return 220;
    case SDL_SCANCODE_RIGHTBRACKET: return 221;
    case SDL_SCANCODE_APOSTROPHE: return 222;
    case SDL_SCANCODE_KP_MULTIPLY: return 106;
    case SDL_SCANCODE_KP_PLUS: return 107;
    case SDL_SCANCODE_KP_MINUS: return 109;
    case SDL_SCANCODE_KP_PERIOD: return 110;
    case SDL_SCANCODE_KP_DIVIDE: return 111;
    case SDL_SCANCODE_KP_ENTER: return 13;
    default: return 0;
    }
}

bool pump_events()
{
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        switch (e.type)
        {
        case SDL_QUIT: return false;
        case SDL_KEYDOWN: case SDL_KEYUP:
        {
            int k = MapKey(e.key.keysym.scancode);
            if (k > 0 && k < 256) g_keys[k] = e.type == SDL_KEYDOWN;
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F11)
            {
                bool fs = SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(g_window, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            break;
        }
        case SDL_MOUSEWHEEL: g_wheel += e.wheel.y * 120; break;
        case SDL_CONTROLLERDEVICEADDED:
        {
            for (int i = 0; i < MAX_PADS; i++)
                if (!g_pads[i]) { g_pads[i] = SDL_GameControllerOpen(e.cdevice.which); break; }
            break;
        }
        case SDL_CONTROLLERDEVICEREMOVED:
        {
            for (int i = 0; i < MAX_PADS; i++)
                if (g_pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[i])) == e.cdevice.which)
                {
                    SDL_GameControllerClose(g_pads[i]);
                    g_pads[i] = nullptr;
                }
            break;
        }
        }
    }
    return true;
}

void present()
{
    // test hook: TF_SHOT="60,300" dumps those frames to TF_SHOT_DIR/frame_N.bmp
    static const char* shots = getenv("TF_SHOT");
    g_frame++;
    if (shots)
    {
        std::string list = std::string(",") + shots + ",";
        if (list.find("," + std::to_string(g_frame) + ",") != std::string::npos)
        {
            int w, h;
            SDL_GetRendererOutputSize(g_renderer, &w, &h);
            SDL_RenderSetLogicalSize(g_renderer, 0, 0);
            SDL_RenderSetViewport(g_renderer, nullptr);
            SDL_GetRendererOutputSize(g_renderer, &w, &h);
            SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(g_renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch);
            const char* dir = getenv("TF_SHOT_DIR");
            std::string path = std::string(dir ? dir : ".") + "/frame_" + std::to_string(g_frame) + ".bmp";
            SDL_RenderSetLogicalSize(g_renderer, g_bbw, g_bbh);
            SDL_SaveBMP(surf, path.c_str());
            SDL_FreeSurface(surf);
            fprintf(stderr, "saved %s\n", path.c_str());
        }
        const char* quit = getenv("TF_QUIT_AT");
        if (quit && g_frame >= atoi(quit)) exit(0);
    }
    SDL_RenderPresent(g_renderer);
}

void set_backbuffer(int w, int h, bool fullscreen)
{
    if (w <= 0 || h <= 0) return;
    g_bbw = w; g_bbh = h;
    if (g_renderer) SDL_RenderSetLogicalSize(g_renderer, w, h);
    if (g_window && !fullscreen && !(SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN_DESKTOP))
        SDL_SetWindowSize(g_window, w, h);
}
void get_backbuffer(int& w, int& h) { w = g_bbw; h = g_bbh; }

void update_input() {}

PadState pad(int index)
{
    PadState s;
    if (index >= 0 && index < VirtualPads()) return VirtualPad(index);
    index -= VirtualPads();
    if (index < 0 || index >= MAX_PADS || !g_pads[index]) return s;
    SDL_GameController* c = g_pads[index];
    if (!SDL_GameControllerGetAttached(c)) return s;
    s.connected = true;
    auto btn = [&](SDL_GameControllerButton b, uint32_t bit) { if (SDL_GameControllerGetButton(c, b)) s.buttons |= bit; };
    btn(SDL_CONTROLLER_BUTTON_DPAD_UP, 1);
    btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN, 2);
    btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 4);
    btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 8);
    btn(SDL_CONTROLLER_BUTTON_START, 16);
    btn(SDL_CONTROLLER_BUTTON_BACK, 32);
    btn(SDL_CONTROLLER_BUTTON_LEFTSTICK, 64);
    btn(SDL_CONTROLLER_BUTTON_RIGHTSTICK, 128);
    btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 256);
    btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 512);
    btn(SDL_CONTROLLER_BUTTON_GUIDE, 2048);
    btn(SDL_CONTROLLER_BUTTON_A, 4096);
    btn(SDL_CONTROLLER_BUTTON_B, 8192);
    btn(SDL_CONTROLLER_BUTTON_X, 16384);
    btn(SDL_CONTROLLER_BUTTON_Y, 32768);
    auto ax = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(c, a) / 32767.0f; };
    s.lx = ax(SDL_CONTROLLER_AXIS_LEFTX);
    s.ly = -ax(SDL_CONTROLLER_AXIS_LEFTY);
    s.rx = ax(SDL_CONTROLLER_AXIS_RIGHTX);
    s.ry = -ax(SDL_CONTROLLER_AXIS_RIGHTY);
    s.lt = ax(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    s.rt = ax(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    // FNA-style deadzone (IndependentAxes 0.24)
    auto dz = [](float v) { const float d = 0.24f; if (v > d) return (v - d) / (1 - d); if (v < -d) return (v + d) / (1 - d); return 0.0f; };
    s.lx = dz(s.lx); s.ly = dz(s.ly); s.rx = dz(s.rx); s.ry = dz(s.ry);
    return s;
}

uint32_t pad_game_buttons(int index)
{
    PadState s = pad(index);
    uint32_t b = s.buttons;
    if (s.lt > 0.1f) b |= 8388608u;   // LeftTrigger
    if (s.rt > 0.1f) b |= 4194304u;   // RightTrigger
    return b;
}

const char* pad_guid(int index) { if (const char* g = getenv("TF_GUID")) if (index < VirtualPads()) return g; return index < VirtualPads() ? "xinput" : (index - VirtualPads() < MAX_PADS && g_pads[index - VirtualPads()] ? "xinput" : ""); }
void pad_rumble(int index, float strength)
{
    if (index >= 0 && index < MAX_PADS && g_pads[index])
        SDL_GameControllerRumble(g_pads[index], (Uint16)(strength * 65535), (Uint16)(strength * 65535), strength > 0 ? 1000 : 0);
}
bool key_down(int k) { return k > 0 && k < 256 && (g_keys[k] || ScriptedKey(k)); }
void mouse_state(int& x, int& y, int& wheel, bool& l, bool& m, bool& r)
{
    Uint32 b = SDL_GetMouseState(&x, &y);
    wheel = g_wheel;
    l = b & SDL_BUTTON_LMASK; m = b & SDL_BUTTON_MMASK; r = b & SDL_BUTTON_RMASK;
}

} // namespace plat
