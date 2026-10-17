// Wii U (Aroma / wut + SDL2) implementation of the platform layer.
//
// Video:  SDL2's Wii U video driver + GX2 renderer. The window is created at the 1280x720
//         virtual backbuffer size; on present the Wii U renderer copies the window colour
//         buffer to BOTH scan buffers (TV and GamePad/DRC) with GX2CopyColorBufferToScanBuffer,
//         which scales it to the TV mode (480p/720p/1080p) and to the 854x480 GamePad screen.
//         (No SDL_WINDOW_WIIU_TV_ONLY / _GAMEPAD_ONLY flag => mirrored on both.)
// ProcUI: handled by SDL's video driver (it calls ProcUIInitEx because we don't). HOME menu ->
//         "Close software" is delivered as SDL_QUIT; SDL_Quit() then finishes the ProcUI exit
//         handshake. If the game quits by itself, SDL_Quit() does SYSLaunchMenu() + the
//         ProcUI loop, so returning from main() always goes back to the Wii U Menu cleanly.
// Input:  read directly with VPAD (GamePad) and KPAD (Wii Remotes / extensions / Pro
//         Controllers). SDL's joystick subsystem is deliberately NOT initialised: its Wii U
//         driver also calls VPADRead/KPADReadEx and would steal samples from us.
#include "platform.h"
#include "autotest.h"

#include <SDL.h>

#include <coreinit/time.h>
#include <coreinit/debug.h>
#include <coreinit/thread.h>
#include <coreinit/systeminfo.h>
#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <coreinit/im.h>
#include <sysapp/switch.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/log_module.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cmath>

namespace plat {

namespace {

// ---------------------------------------------------------------- XNA Buttons enum values
enum : uint32_t {
    XB_DPadUp        = 0x00000001,
    XB_DPadDown      = 0x00000002,
    XB_DPadLeft      = 0x00000004,
    XB_DPadRight     = 0x00000008,
    XB_Start         = 0x00000010,
    XB_Back          = 0x00000020,
    XB_LeftStick     = 0x00000040,
    XB_RightStick    = 0x00000080,
    XB_LeftShoulder  = 0x00000100,
    XB_RightShoulder = 0x00000200,
    XB_BigButton     = 0x00000800,
    XB_A             = 0x00001000,
    XB_B             = 0x00002000,
    XB_X             = 0x00004000,
    XB_Y             = 0x00008000,
    XB_RightTrigger  = 0x00400000,
    XB_LeftTrigger   = 0x00800000,
};

const char* SD_ROOT  = "fs:/vol/external01";
const char* APP_DIR  = "fs:/vol/external01/wiiu/apps/towerfall";

SDL_Window*   g_window   = nullptr;
SDL_Renderer* g_renderer = nullptr;
bool          g_quit     = false;
bool          g_sdl_up   = false;

// logging
bool  g_log_module = false;
bool  g_log_cafe   = false;
bool  g_log_udp    = false;
FILE* g_log_file   = nullptr;

// ---------------------------------------------------------------- input state
enum class PadType { None, GamePad, Wiimote, Nunchuk, Classic, Pro };

struct Slot {
    PadState state;
    PadType  type = PadType::None;
    bool     rumbleOn = false;      // currently requested
    bool     motorRunning = false;  // what we last told the hardware
    int      vpadRefresh = 0;       // frames until the GamePad rumble pattern is re-sent
    uint32_t gameButtons = 0;       // gameplay-layer XNA bits (see map_standard)
};
Slot g_slots[MAX_PADS];

constexpr int NUM_WPAD = 7;                // WPAD_CHAN_0..WPAD_CHAN_6 -> slots 1..7

inline float clamp1(float v) { return v < -1.f ? -1.f : (v > 1.f ? 1.f : v); }
inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

bool dir_exists(const std::string& p)
{
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void make_dir(const std::string& p)
{
    if (!dir_exists(p)) mkdir(p.c_str(), 0777);
}

void make_dirs(const std::string& path)
{
    // create every component after "fs:/vol/external01"
    std::string root = SD_ROOT;
    if (path.compare(0, root.size(), root) != 0) { make_dir(path); return; }
    size_t pos = root.size();
    while (pos < path.size()) {
        size_t next = path.find('/', pos + 1);
        if (next == std::string::npos) next = path.size();
        make_dir(path.substr(0, next));
        pos = next;
    }
}

// ---- button mapping (configurable through <app>/controls.txt)
// Game actions in XNA terms: jump = A (also menu confirm), shoot = X (also menu back),
// altshoot = B (also menu back), dodge = shoulders/triggers, arrows = Y, start, back.
struct ActionDef { const char* name; uint32_t bits; };
const ActionDef ACTIONS[] = {
    { "jump", XB_A }, { "shoot", XB_X }, { "altshoot", XB_B }, { "dodge", XB_RightShoulder }, { "dodge_l", XB_LeftShoulder },
    { "arrows", XB_Y }, { "start", XB_Start }, { "back", XB_Back }, { "none", 0 },
    // raw XNA buttons, for completeness
    { "xna_a", XB_A }, { "xna_b", XB_B }, { "xna_x", XB_X }, { "xna_y", XB_Y },
    { "xna_lb", XB_LeftShoulder }, { "xna_rb", XB_RightShoulder }, { "xna_lt", XB_LeftTrigger }, { "xna_rt", XB_RightTrigger },
};

struct ButtonMap { const char* name; uint32_t phys; uint32_t action; };

// Nintendo-style pads (GamePad, Pro Controller, Classic Controller, Bloopair pads): by label.
// This is the *gameplay* layer; menus always use A = confirm, B = back.
ButtonMap g_padMap[] = {
    { "A", 0, XB_X }, { "B", 0, XB_A }, { "X", 0, XB_Y }, { "Y", 0, XB_X },
    { "L", 0, XB_LeftShoulder }, { "R", 0, XB_RightShoulder }, { "ZL", 0, XB_LeftTrigger }, { "ZR", 0, XB_RightTrigger },
    { "PLUS", 0, XB_Start }, { "MINUS", 0, XB_Back }, { "STICK_L", 0, XB_LeftStick }, { "STICK_R", 0, XB_RightStick },
};
// Wii Remote held sideways.
ButtonMap g_wiimoteMap[] = {
    { "1", WPAD_BUTTON_1, XB_A }, { "2", WPAD_BUTTON_2, XB_X }, { "B", WPAD_BUTTON_B, XB_RightShoulder },
    { "A", WPAD_BUTTON_A, XB_Y }, { "MINUS", WPAD_BUTTON_MINUS, XB_Y }, { "PLUS", WPAD_BUTTON_PLUS, XB_Start },
};
// Wii Remote + Nunchuk (Z and C are resolved separately).
ButtonMap g_nunchukMap[] = {
    { "A", WPAD_BUTTON_A, XB_A }, { "B", WPAD_BUTTON_B, XB_X }, { "Z", 0, XB_RightShoulder }, { "C", 0, XB_Y },
    { "PLUS", WPAD_BUTTON_PLUS, XB_Start }, { "MINUS", WPAD_BUTTON_MINUS, XB_Back },
    { "1", WPAD_BUTTON_1, 0 }, { "2", WPAD_BUTTON_2, 0 },
};

const char* action_name(uint32_t bits)
{
    for (auto& a : ACTIONS) if (a.bits == bits) return a.name;
    if (bits == XB_LeftTrigger) return "dodge_l";
    if (bits == XB_RightTrigger) return "dodge";
    return "none";
}

void write_default_controls(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fputs("# TowerFall Wii U controls. Edit and restart the game. Lines: <device>.<button> = <action>\n"
               "# actions: jump (menu confirm), shoot (menu back), altshoot (menu back), dodge, dodge_l (dodge; menu left-page),\n"
               "#          arrows, start, back, none\n"
               "# devices: pad = GamePad / Pro Controller / Classic Controller / Bloopair pads (by button label;\n"
               "#          this sets the in-match layout - menus always use A = confirm, B = back)\n"
               "#          wiimote = Wii Remote held sideways, nunchuk = Wii Remote + Nunchuk\n\n", f);
    for (auto& m : g_padMap) std::fprintf(f, "pad.%s = %s\n", m.name, action_name(m.action));
    std::fputs("\n", f);
    for (auto& m : g_wiimoteMap) std::fprintf(f, "wiimote.%s = %s\n", m.name, action_name(m.action));
    std::fputs("\n", f);
    for (auto& m : g_nunchukMap) std::fprintf(f, "nunchuk.%s = %s\n", m.name, action_name(m.action));
    std::fclose(f);
}

std::string trim(std::string v)
{
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r' || v.back() == '\n')) v.pop_back();
    size_t i = 0;
    while (i < v.size() && (v[i] == ' ' || v[i] == '\t')) i++;
    return v.substr(i);
}

void load_controls()
{
    std::string path = std::string(APP_DIR) + "/controls.txt";
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) { write_default_controls(path); log("controls: wrote default controls.txt"); return; }
    char line[256];
    int applied = 0;
    while (std::fgets(line, sizeof line, f))
    {
        std::string l = trim(line);
        if (l.empty() || l[0] == '#') continue;
        size_t eq = l.find('='), dot = l.find('.');
        if (eq == std::string::npos || dot == std::string::npos || dot > eq) continue;
        std::string dev = trim(l.substr(0, dot)), btn = trim(l.substr(dot + 1, eq - dot - 1)), act = trim(l.substr(eq + 1));
        for (auto& c : dev) c = (char)std::tolower((unsigned char)c);
        for (auto& c : btn) c = (char)std::toupper((unsigned char)c);
        for (auto& c : act) c = (char)std::tolower((unsigned char)c);
        const ActionDef* ad = nullptr;
        for (auto& a : ACTIONS) if (act == a.name) ad = &a;
        if (!ad) { log(("controls: unknown action '" + act + "'").c_str()); continue; }
        ButtonMap* table = nullptr; size_t n = 0;
        if (dev == "pad") { table = g_padMap; n = sizeof(g_padMap) / sizeof(g_padMap[0]); }
        else if (dev == "wiimote") { table = g_wiimoteMap; n = sizeof(g_wiimoteMap) / sizeof(g_wiimoteMap[0]); }
        else if (dev == "nunchuk") { table = g_nunchukMap; n = sizeof(g_nunchukMap) / sizeof(g_nunchukMap[0]); }
        if (!table) { log(("controls: unknown device '" + dev + "'").c_str()); continue; }
        bool found = false;
        for (size_t i = 0; i < n; i++) if (btn == table[i].name) { table[i].action = ad->bits; found = true; applied++; }
        if (!found) log(("controls: unknown button '" + dev + "." + btn + "'").c_str());
    }
    std::fclose(f);
    char b[64]; std::snprintf(b, sizeof b, "controls: %d mappings loaded", applied); log(b);
}

// Physical bit layout of the label-mapped pads, in g_padMap order.
struct PadBits { uint32_t bits[12]; uint32_t up, down, left, right; };

// Menu layer (returned): fixed by label, A = confirm, B = back, like Nintendo games.
// Gameplay layer (g_lastGame): from g_padMap / controls.txt.
uint32_t g_lastGame = 0;
const uint32_t MENU_BITS[12] = { XB_A, XB_B, XB_X, XB_Y, XB_LeftShoulder, XB_RightShoulder, XB_LeftTrigger, XB_RightTrigger,
                                 XB_Start, XB_Back, XB_LeftStick, XB_RightStick };
uint32_t map_standard(uint32_t hold, const PadBits& m, float& lt, float& rt)
{
    uint32_t o = 0, g = 0;
    for (int i = 0; i < 12; i++)
        if (m.bits[i] && (hold & m.bits[i])) { o |= MENU_BITS[i]; g |= g_padMap[i].action; }
    if (hold & m.up)    o |= XB_DPadUp;
    if (hold & m.down)  o |= XB_DPadDown;
    if (hold & m.left)  o |= XB_DPadLeft;
    if (hold & m.right) o |= XB_DPadRight;
    if (o & XB_LeftTrigger)  lt = 1.f;
    if (o & XB_RightTrigger) rt = 1.f;
    g_lastGame = g;
    return o;
}

const PadBits VPAD_BITS = {
    { VPAD_BUTTON_A, VPAD_BUTTON_B, VPAD_BUTTON_X, VPAD_BUTTON_Y, VPAD_BUTTON_L, VPAD_BUTTON_R, VPAD_BUTTON_ZL, VPAD_BUTTON_ZR,
      VPAD_BUTTON_PLUS, VPAD_BUTTON_MINUS, VPAD_BUTTON_STICK_L, VPAD_BUTTON_STICK_R },
    VPAD_BUTTON_UP, VPAD_BUTTON_DOWN, VPAD_BUTTON_LEFT, VPAD_BUTTON_RIGHT,
};
const PadBits CLASSIC_BITS = {
    { WPAD_CLASSIC_BUTTON_A, WPAD_CLASSIC_BUTTON_B, WPAD_CLASSIC_BUTTON_X, WPAD_CLASSIC_BUTTON_Y, WPAD_CLASSIC_BUTTON_L, WPAD_CLASSIC_BUTTON_R,
      WPAD_CLASSIC_BUTTON_ZL, WPAD_CLASSIC_BUTTON_ZR, WPAD_CLASSIC_BUTTON_PLUS, WPAD_CLASSIC_BUTTON_MINUS, 0, 0 },
    WPAD_CLASSIC_BUTTON_UP, WPAD_CLASSIC_BUTTON_DOWN, WPAD_CLASSIC_BUTTON_LEFT, WPAD_CLASSIC_BUTTON_RIGHT,
};
const PadBits PRO_BITS = {
    { WPAD_PRO_BUTTON_A, WPAD_PRO_BUTTON_B, WPAD_PRO_BUTTON_X, WPAD_PRO_BUTTON_Y, WPAD_PRO_BUTTON_L, WPAD_PRO_BUTTON_R,
      WPAD_PRO_BUTTON_ZL, WPAD_PRO_BUTTON_ZR, WPAD_PRO_BUTTON_PLUS, WPAD_PRO_BUTTON_MINUS, WPAD_PRO_BUTTON_STICK_L, WPAD_PRO_BUTTON_STICK_R },
    WPAD_PRO_BUTTON_UP, WPAD_PRO_BUTTON_DOWN, WPAD_PRO_BUTTON_LEFT, WPAD_PRO_BUTTON_RIGHT,
};

// Wii Remote held sideways (D-pad on the left, 1/2 on the right; the IR end points left).
// Rotating the remote 90 deg counter-clockwise maps: physical Up -> screen left,
// Down -> right, Left -> down, Right -> up.
uint32_t map_wiimote_sideways(uint32_t h)
{
    uint32_t o = 0;
    if (h & WPAD_BUTTON_UP)    o |= XB_DPadLeft;
    if (h & WPAD_BUTTON_DOWN)  o |= XB_DPadRight;
    if (h & WPAD_BUTTON_LEFT)  o |= XB_DPadDown;
    if (h & WPAD_BUTTON_RIGHT) o |= XB_DPadUp;
    for (auto& m : g_wiimoteMap) if (h & m.phys) o |= m.action;
    // HOME is left to the system.
    return o;
}

// Wii Remote upright + Nunchuk.
uint32_t map_nunchuk(uint32_t h, uint32_t nh)
{
    uint32_t o = 0;
    if (h & WPAD_BUTTON_UP)    o |= XB_DPadUp;
    if (h & WPAD_BUTTON_DOWN)  o |= XB_DPadDown;
    if (h & WPAD_BUTTON_LEFT)  o |= XB_DPadLeft;
    if (h & WPAD_BUTTON_RIGHT) o |= XB_DPadRight;
    for (auto& m : g_nunchukMap) if (m.phys && (h & m.phys)) o |= m.action;
    uint32_t zc = (h | nh);
    if (zc & WPAD_NUNCHUK_BUTTON_Z) o |= g_nunchukMap[2].action;
    if (zc & WPAD_NUNCHUK_BUTTON_C) o |= g_nunchukMap[3].action;
    return o;
}

void set_motor(int index, bool on)
{
    Slot& s = g_slots[index];
    if (s.motorRunning == on) return;
    s.motorRunning = on;
    if (index == 0) {
        if (!on) VPADStopMotor(VPAD_CHAN_0);
        s.vpadRefresh = 0; // (re)send the pattern on the next update when turning on
    } else {
        WPADControlMotor((WPADChan)(index - 1), on ? TRUE : FALSE);
    }
}

void stop_all_motors()
{
    for (int i = 0; i < MAX_PADS; i++) {
        g_slots[i].rumbleOn = false;
        set_motor(i, false);
    }
}

// ---- controller pairing
// While an app runs, the console's SYNC button is the app's job: on the IM "sync" event we
// start WPAD pairing (the remote then pairs when its own red SYNC button is pressed).
// Fallback: holding MINUS on the GamePad for 2 s opens the system "Sync controllers" screen.
IOSHandle g_im = -1;
alignas(0x40) IMRequest g_imRequest;
alignas(0x40) IMEventMask g_imEvent;
volatile bool g_imFired = false;
volatile int g_imError = 0;
bool g_imArmed = false;
int g_minusHeldFrames = 0;

void im_callback(IOSError err, void*)
{
    g_imError = (int)err;
    g_imFired = true;
}

void im_arm()
{
    if (g_im < 0) return;
    std::memset(&g_imRequest, 0, sizeof(g_imRequest));
    g_imEvent = 0;
    g_imFired = false;
    g_imArmed = (int)IM_GetEventNotify(g_im, &g_imRequest, &g_imEvent, im_callback, nullptr) >= 0;
}

void sync_init()
{
    g_im = IM_Open();
    if (g_im < 0) { log("sync: IM_Open failed"); return; }
    im_arm();
}

void sync_update()
{
    if (g_imArmed && g_imFired)
    {
        g_imArmed = false;
        IMEventMask ev = g_imEvent;
        if (g_imError >= 0 && (ev & IM_EVENT_SYNC))
        {
            BOOL ok = WPADStartSyncDevice();
            log(ok ? "sync: SYNC button pressed, searching for controllers" : "sync: WPADStartSyncDevice failed");
        }
        if (!(ev & IM_EVENT_CANCELLED)) im_arm();
    }
}

void sync_shutdown()
{
    if (g_im < 0) return;
    if (g_imArmed) IM_CancelGetEventNotify(g_im, &g_imRequest, nullptr, nullptr);
    IM_Close(g_im);
    g_im = -1;
}

void update_gamepad()
{
    Slot& s = g_slots[0];
    VPADStatus st;
    VPADReadError err = VPAD_READ_SUCCESS;
    std::memset(&st, 0, sizeof(st));
    int32_t n = VPADRead(VPAD_CHAN_0, &st, 1, &err);

    if (err == VPAD_READ_NO_SAMPLES || err == VPAD_READ_BUSY) {
        return; // keep last state
    }
    if (n < 1 || err != VPAD_READ_SUCCESS) {
        // GamePad switched off / out of range
        s.state = PadState();
        s.type = PadType::None;
        return;
    }

    if (st.hold & VPAD_BUTTON_MINUS)
    {
        if (++g_minusHeldFrames == 120)
        {
            log("sync: opening the system controller sync screen");
            SYSSwitchToSyncControllerOnHBM();
        }
    }
    else g_minusHeldFrames = 0;

    PadState p;
    p.connected = true;
    p.buttons = map_standard(st.hold, VPAD_BITS, p.lt, p.rt);
    p.lx = clamp1(st.leftStick.x);
    p.ly = clamp1(st.leftStick.y);   // VPAD: +y is up (same as XNA)
    p.rx = clamp1(st.rightStick.x);
    p.ry = clamp1(st.rightStick.y);
    s.state = p;
    s.gameButtons = g_lastGame;
    s.type = PadType::GamePad;
}

void update_wpad(int chan)
{
    Slot& s = g_slots[chan + 1];
    WPADExtensionType ext = WPAD_EXT_CORE;
    WPADError perr = WPADProbe((WPADChan)chan, &ext);

    if (perr == WPAD_ERROR_NO_CONTROLLER || perr == WPAD_ERROR_INVALID) {
        if (s.type != PadType::None) {
            s.state = PadState();
            s.type = PadType::None;
            s.rumbleOn = false;
            s.motorRunning = false;
        }
        return;
    }
    if (perr != WPAD_ERROR_NONE) {
        // WPAD_ERROR_NOT_READY / busy: extension being swapped or still pairing.
        // Keep connected flag but release all inputs so nothing gets stuck.
        bool keepConn = s.state.connected;
        s.state = PadState();
        s.state.connected = keepConn;
        return;
    }

    KPADStatus k;
    KPADError kerr = KPAD_ERROR_OK;
    std::memset(&k, 0, sizeof(k));
    uint32_t n = KPADReadEx((KPADChan)chan, &k, 1, &kerr);
    if (n < 1 || kerr != KPAD_ERROR_OK) {
        if (kerr == KPAD_ERROR_INVALID_CONTROLLER) {
            s.state = PadState();
            s.type = PadType::None;
        }
        // NO_SAMPLES / BUSY: keep last state
        return;
    }

    // Prefer the extension type reported in the sample (it's what the union holds).
    uint8_t e = k.extensionType;
    PadState p;
    p.connected = true;
    PadType type;

    switch (e) {
    case WPAD_EXT_NUNCHUK:
    case WPAD_EXT_MPLUS_NUNCHUK:
        type = PadType::Nunchuk;
        p.buttons = map_nunchuk(k.hold, k.nunchuk.hold);
        p.lx = clamp1(k.nunchuk.stick.x);
        p.ly = clamp1(k.nunchuk.stick.y); // KPAD: +y is up
        break;
    case WPAD_EXT_CLASSIC:
    case WPAD_EXT_MPLUS_CLASSIC:
        type = PadType::Classic;
        p.buttons = map_standard(k.classic.hold, CLASSIC_BITS, p.lt, p.rt);
        p.lx = clamp1(k.classic.leftStick.x);
        p.ly = clamp1(k.classic.leftStick.y);
        p.rx = clamp1(k.classic.rightStick.x);
        p.ry = clamp1(k.classic.rightStick.y);
        break;
    case WPAD_EXT_PRO_CONTROLLER:
        type = PadType::Pro;
        p.buttons = map_standard(k.pro.hold, PRO_BITS, p.lt, p.rt);
        p.lx = clamp1(k.pro.leftStick.x);
        p.ly = clamp1(k.pro.leftStick.y);
        p.rx = clamp1(k.pro.rightStick.x);
        p.ry = clamp1(k.pro.rightStick.y);
        break;
    case WPAD_EXT_BALANCE_BOARD:
        // Not a usable game controller: don't occupy a player slot.
        s.state = PadState();
        s.type = PadType::None;
        return;
    case WPAD_EXT_CORE:
    case WPAD_EXT_MPLUS:
    default:
        // Bare Wii Remote (also guitar/drums etc: only the remote's own buttons are used)
        type = PadType::Wiimote;
        p.buttons = map_wiimote_sideways(k.hold);
        break;
    }

    if (type != s.type) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "input: WPAD chan %d -> slot %d ext=0x%02x", chan, chan + 1, (unsigned)e);
        log(buf);
    }
    s.state = p;
    // Wii Remote layouts are the same in menus and matches; Nintendo pads have a separate gameplay layer.
    s.gameButtons = (type == PadType::Classic || type == PadType::Pro) ? g_lastGame : p.buttons;
    s.type = type;
}

void update_rumble()
{
    for (int i = 0; i < MAX_PADS; i++) {
        Slot& s = g_slots[i];
        bool want = s.rumbleOn && s.state.connected;
        set_motor(i, want);
        if (i == 0 && s.motorRunning) {
            // VPADControlMotor plays a finite on/off pattern (one bit per tick);
            // re-send a long "all on" pattern regularly while rumble is requested.
            if (--s.vpadRefresh <= 0) {
                static uint8_t pattern[120];
                static bool init = false;
                if (!init) { std::memset(pattern, 0xFF, sizeof(pattern)); init = true; }
                VPADControlMotor(VPAD_CHAN_0, pattern, sizeof(pattern));
                s.vpadRefresh = 6;
            }
        }
    }
}

bool g_input_up = false;

void input_init()
{
    VPADInit();
    KPADInit();
    // 7 Wii Remote channels (default is 4). Must be 4 or 7.
    int32_t r = KPADSetMaxControllers(7);
    if (r != 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "input: KPADSetMaxControllers(7) failed (%d)", (int)r);
        log(buf);
    }
    WPADEnableURCC(TRUE);      // Wii U Pro Controller (and Bloopair-emulated pads)
    WPADEnableWiiRemote(TRUE); // Wii Remotes (+ Nunchuk / Classic)
    WPADEnableMotor(TRUE);
    load_controls();
    sync_init();
    g_input_up = true;
}

void log_file_open()
{
    make_dirs(APP_DIR);
    std::string path = std::string(APP_DIR) + "/towerfall.log";
    g_log_file = std::fopen(path.c_str(), "w");
}

} // namespace

// ================================================================ lifecycle

bool init()
{
    // Logging first so everything after can be diagnosed.
    g_log_module = WHBLogModuleInit();   // Aroma logging module (forwards to UDP)
    if (!g_log_module) {
        g_log_cafe = WHBLogCafeInit();
        g_log_udp  = WHBLogUdpInit();
    }
    log_file_open();
    log("TowerFall Wii U: init");

    // NOTE: no SDL_INIT_JOYSTICK / SDL_INIT_GAMECONTROLLER (see header comment).
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        log((std::string("SDL_Init failed: ") + SDL_GetError()).c_str());
        return false;
    }
    g_sdl_up = true;

    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

    // No WIIU_TV_ONLY / WIIU_GAMEPAD_ONLY flag: image goes to TV and GamePad.
    g_window = SDL_CreateWindow("TowerFall", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                BACKBUFFER_W, BACKBUFFER_H, SDL_WINDOW_SHOWN);
    if (!g_window) {
        log((std::string("SDL_CreateWindow failed: ") + SDL_GetError()).c_str());
        return false;
    }
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) {
        log((std::string("SDL_CreateRenderer failed: ") + SDL_GetError()).c_str());
        return false;
    }
    // The window *is* 1280x720 (the GX2 window colour buffer), so this is an identity
    // mapping; the TV (480p/720p/1080p) and GamePad (854x480) scaling is done by GX2 when
    // the colour buffer is copied to each scan buffer at present time.
    SDL_RenderSetLogicalSize(g_renderer, BACKBUFFER_W, BACKBUFFER_H);

    {
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(g_renderer, &info) == 0) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "renderer: %s flags=0x%x", info.name, (unsigned)info.flags);
            log(buf);
        }
    }

    input_init();
    log(("content root: " + content_root()).c_str());
    log(("save dir: " + save_dir()).c_str());
    if (!dir_exists(content_root() + "/Content"))
    {
        log("ERROR: game content not found");
        OSFatal("TowerFall: game data not found.\n\n"
                "Copy the Content and DarkWorldContent folders from your TowerFall\n"
                "(TF-8P) folder to the SD card at:\n\n"
                "    sd:/wiiu/apps/towerfall/Content\n"
                "    sd:/wiiu/apps/towerfall/DarkWorldContent\n\n"
                "Hold the POWER button to turn off the console.");
    }
    return true;
}

void shutdown()
{
    log("TowerFall Wii U: shutdown");
    if (g_input_up) {
        sync_shutdown();
        stop_all_motors();
        g_input_up = false;
    }
    if (g_renderer) { SDL_DestroyRenderer(g_renderer); g_renderer = nullptr; }
    if (g_window)   { SDL_DestroyWindow(g_window);     g_window = nullptr; }
    if (g_sdl_up)   { SDL_Quit(); g_sdl_up = false; }  // finishes the ProcUI exit sequence

    if (g_log_file) { std::fclose(g_log_file); g_log_file = nullptr; }
    if (g_log_udp)    { WHBLogUdpDeinit();    g_log_udp = false; }
    if (g_log_cafe)   { WHBLogCafeDeinit();   g_log_cafe = false; }
    if (g_log_module) { WHBLogModuleDeinit(); g_log_module = false; }
}

SDL_Window* window() { return g_window; }
SDL_Renderer* renderer() { return g_renderer; }
bool is_wiiu() { return true; }

// The Wii U output is fixed: the GX2 window colour buffer stays 1280x720 and is scaled by
// the hardware to the TV and the GamePad. Resolution / fullscreen requests are ignored.
void set_backbuffer(int, int, bool)
{
    if (g_renderer) SDL_RenderSetLogicalSize(g_renderer, BACKBUFFER_W, BACKBUFFER_H);
}

void get_backbuffer(int& w, int& h)
{
    w = BACKBUFFER_W;
    h = BACKBUFFER_H;
}

// ---- scripted test input (optional <content>/autotest.txt with KEY=VALUE lines)
int g_test_frame = 0;
int& autotest_frame_counter() { return g_test_frame; }
const char* autotest_config(const char* key)
{
    static std::vector<std::pair<std::string, std::string>> kv;
    static bool loaded = false;
    if (!loaded)
    {
        loaded = true;
        FILE* f = fopen((content_root() + "/autotest.txt").c_str(), "rb");
        if (f)
        {
            char line[4096];
            while (fgets(line, sizeof line, f))
            {
                std::string l = line;
                while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
                size_t eq = l.find('=');
                if (eq != std::string::npos) kv.push_back({ l.substr(0, eq), l.substr(eq + 1) });
            }
            fclose(f);
            log("autotest.txt loaded");
        }
    }
    for (auto& p : kv) if (p.first == key) return p.second.c_str();
    return nullptr;
}

bool pump_events()
{
    // SDL_PollEvent -> SDL_PumpEvents -> WIIU_PumpEvents -> ProcUIProcessMessages(TRUE).
    // While in the HOME menu that call blocks, so the game simply pauses.
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            log("SDL_QUIT received (HOME menu close / system exit)");
            g_quit = true;
            break;
        case SDL_APP_WILLENTERBACKGROUND:
            if (g_input_up) stop_all_motors();
            break;
        default:
            break;
        }
    }
    return !g_quit;
}

void present()
{
    if (g_renderer) SDL_RenderPresent(g_renderer);
    g_test_frame++;
    if (const char* q = autotest_config("TF_QUIT_AT")) if (g_test_frame >= atoi(q)) g_quit = true;
}

// ================================================================ paths / time / log

std::string content_root()
{
    static std::string cached;
    if (!cached.empty()) return cached;
    std::string root = APP_DIR;
    if (!dir_exists(root + "/Content") && dir_exists(root + "/TF-8P/Content"))
        root += "/TF-8P";
    cached = root;
    return cached;
}

std::string save_dir()
{
    static std::string cached;
    if (!cached.empty()) return cached;
    std::string dir = std::string(APP_DIR) + "/save";
    make_dirs(dir);
    cached = dir;
    return cached;
}

int64_t now_ns()
{
    // OSTicksToNanoseconds overflows after ~100 days of uptime; split instead.
    uint64_t t = (uint64_t)OSGetSystemTime();
    uint64_t rate = (uint64_t)OSTimerClockSpeed;
    uint64_t sec = t / rate;
    uint64_t rem = t % rate;
    return (int64_t)(sec * 1000000000ull + (rem * 1000000000ull) / rate);
}

void log(const char* msg)
{
    if (!msg) return;
    static int64_t t0 = 0;
    if (!t0) t0 = now_ns();
    char line[1200];
    std::snprintf(line, sizeof line, "[%8.3f] %s", (now_ns() - t0) / 1e9, msg);
    WHBLogPrint(line);
    if (g_log_file) {
        std::fputs(line, g_log_file);
        std::fputc('\n', g_log_file);
        // The SD card's FAT entry only gets the new size when the file is closed, so close
        // and reopen: the log then survives a hang + power-off.
        std::fclose(g_log_file);
        g_log_file = std::fopen((std::string(APP_DIR) + "/towerfall.log").c_str(), "a");
    }
}

// ---- loading screen: keeps the console responsive (HOME, power) while content loads
static bool g_loading = true;
static int g_load_files = 0;
static int64_t g_load_last = 0;
void loading_done()
{
    if (!g_loading) return;
    g_loading = false;
    char b[96];
    std::snprintf(b, sizeof b, "loading done: %d files", g_load_files);
    log(b);
}
void loading_tick(const char* what)
{
    if (!g_loading) return;
    g_load_files++;
    if (g_load_files % 100 == 0) { char b[300]; std::snprintf(b, sizeof b, "loading: %d files (%s)", g_load_files, what ? what : ""); log(b); }
    int64_t t = now_ns();
    if (t - g_load_last < 50000000LL || !g_renderer) return;   // at most 20 updates/s
    g_load_last = t;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) if (ev.type == SDL_QUIT) g_quit = true;
    SDL_Texture* prevTarget = SDL_GetRenderTarget(g_renderer);
    SDL_SetRenderTarget(g_renderer, nullptr);
    SDL_RenderSetViewport(g_renderer, nullptr);
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(g_renderer, 12, 8, 24, 255);
    SDL_RenderClear(g_renderer);
    // progress bar (about 950 files are read at startup)
    float f = g_load_files / 950.0f; if (f > 1) f = 1;
    SDL_Rect frame{ 340, 560, 600, 24 }, bar{ 344, 564, (int)(592 * f), 16 };
    SDL_SetRenderDrawColor(g_renderer, 90, 70, 140, 255);
    SDL_RenderDrawRect(g_renderer, &frame);
    SDL_SetRenderDrawColor(g_renderer, 230, 200, 90, 255);
    SDL_RenderFillRect(g_renderer, &bar);
    SDL_RenderPresent(g_renderer);
    SDL_SetRenderTarget(g_renderer, prevTarget);
}

void sleep_ms(int ms)
{
    if (ms <= 0) { OSYieldThread(); return; }
    OSSleepTicks(OSMillisecondsToTicks(ms));
}

// ================================================================ input

void update_input()
{
    if (!g_input_up) return;
    sync_update();
    update_gamepad();
    for (int c = 0; c < NUM_WPAD; c++) update_wpad(c);
    update_rumble();
}

uint32_t pad_game_buttons(int index)
{
    if (index >= 0 && index < autotest::VirtualPads()) return autotest::VirtualPad(index).buttons;
    if (index < 0 || index >= MAX_PADS || !g_slots[index].state.connected) return 0;
    return g_slots[index].gameButtons;
}

PadState pad(int index)
{
    if (index >= 0 && index < autotest::VirtualPads()) return autotest::VirtualPad(index);
    if (index < 0 || index >= MAX_PADS) return PadState();
    return g_slots[index].state;
}

const char* pad_guid(int index)
{
    if (index < 0 || index >= MAX_PADS) return "";
    switch (g_slots[index].type) {
    case PadType::GamePad: return "wiiu_gamepad";
    case PadType::Wiimote: return "wiimote";
    case PadType::Nunchuk: return "nunchuk";
    case PadType::Classic: return "classic";
    case PadType::Pro:     return "procontroller";
    default:               return "";
    }
}

void pad_rumble(int index, float strength)
{
    if (index < 0 || index >= MAX_PADS || !g_input_up) return;
    g_slots[index].rumbleOn = strength > 0.f;
    bool want = g_slots[index].rumbleOn && g_slots[index].state.connected;
    set_motor(index, want);   // GamePad pattern is (re)sent from update_input()
}

bool key_down(int) { return false; }

void mouse_state(int& x, int& y, int& wheel, bool& l, bool& m, bool& r)
{
    x = y = wheel = 0;
    l = m = r = false;
}

} // namespace plat
