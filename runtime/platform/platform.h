// Platform abstraction implemented by platform_desktop.cpp and platform_wiiu.cpp.
// Everything here is plain C++ (no GC objects).
#pragma once
#include <cstdint>
#include <string>

struct SDL_Window;
struct SDL_Renderer;

namespace plat {

// The game always renders into a 1280x720 virtual backbuffer; the platform scales it
// to the real outputs (on Wii U: TV and GamePad mirror the same image).
constexpr int BACKBUFFER_W = 1280;
constexpr int BACKBUFFER_H = 720;

bool init();                       // SDL init, window + renderer creation
void shutdown();
SDL_Window* window();
SDL_Renderer* renderer();
bool pump_events();                // process OS events; false => quit requested
void present();                    // present the frame (vsync)
// Requested backbuffer size from the game (GraphicsDeviceManager.ApplyChanges). On desktop this
// resizes the window (or goes fullscreen) and sets SDL_RenderSetLogicalSize(w,h). On Wii U the
// game is always "fullscreen": keep the logical size at 1280x720 and ignore the request.
void set_backbuffer(int w, int h, bool fullscreen);
void get_backbuffer(int& w, int& h);
bool is_wiiu();

std::string content_root();        // directory that contains "Content" and "DarkWorldContent"
std::string save_dir();            // writable directory for save data / configs
int64_t now_ns();                  // monotonic clock
void log(const char* msg);
// Loading screen: called for each file opened during startup (keeps HOME responsive on Wii U).
void loading_tick(const char* what);
void loading_done();
void sleep_ms(int ms);

// ---- input
constexpr int MAX_PADS = 8;
// Button bits use the XNA Microsoft::Xna::Framework::Input::Buttons values.
struct PadState
{
    bool connected = false;
    uint32_t buttons = 0;
    float lx = 0, ly = 0;          // left stick, XNA convention: +y is up
    float rx = 0, ry = 0;
    float lt = 0, rt = 0;          // triggers 0..1
};
void update_input();               // poll all devices once per frame
PadState pad(int index);           // index 0..7 (menu layer)
uint32_t pad_game_buttons(int index); // gameplay-layer XNA button bits (incl. trigger bits)
const char* pad_guid(int index);   // short id string, e.g. "wiiu_gamepad", "wiimote", "nunchuk", "classic", "procontroller", "xinput"
void pad_rumble(int index, float strength);
bool key_down(int xnaKey);         // XNA Keys value (desktop keyboard; always false on Wii U)
void mouse_state(int& x, int& y, int& wheel, bool& l, bool& m, bool& r);

} // namespace plat
