# TowerFall 8P → Wii U port

A source port of TowerFall (8-player build) from C#/FNA to C++. It runs on the Wii U (Aroma homebrew) and on macOS for testing.
The original game is never modified: the port reads the game data straight from your copy (`TF-8P/`).

## How it works

```
TF-8P/TowerFall.app/.../TowerFall.exe  --ILSpy-->  decomp/          (decompiled C#, reference only)
                                                   cs/              (working copy of the C#, small port edits)
cs/  --tools/cs2cpp (Roslyn C# → C++ translator)-->  gen/            (generated C++; never edit by hand)
gen/ + runtime/  --CMake-->  build/desktop/towerfall   (macOS test build)
                            build/wiiu/towerfall.wuhb (Wii U)
```

- **`tools/cs2cpp/`**: a C# → C++ translator built on Roslyn. It works from Roslyn's semantic operation tree and handles:
  - classes as GC pointers and structs as values; properties become `get_X`/`set_X`;
  - `yield` iterators become C++20 coroutines; delegates and lambdas become GC-allocated closures;
  - C#-style two-phase construction (`__ctor`), so virtual calls from constructors dispatch like C#;
  - lazy static constructors, null checks that throw `NullReferenceException`, switch and pattern matching, `out`/`ref`;
  - reflection metadata (`gen/src/__reflection.cpp`), used by the save system, the variant list and enemy state machines.
- **`runtime/sys/`**: the .NET subset:
  - a conservative mark-and-sweep GC that never scans "atomic" allocations (strings, number arrays);
  - `String` (UTF-16) and .NET-ordered `List`/`Dictionary`/`HashSet`;
  - reflection, `System.IO` and `System.Xml`, plus an `XmlSerializer` for save files;
  - .NET's exact `Random` algorithm, number formatting, `TimeSpan`/`Version`, and so on.
- **`runtime/xna/`**: the XNA subset on SDL2's `SDL_Renderer`:
  - `SpriteBatch`, render targets, blend states and fonts (the XNB SpriteFont loader);
  - the two shaders reimplemented with blend modes (Dark World lighting) and row strips (slow-motion wave);
  - an SDL audio mixer, plus XACT music streamed from the original `.xwb` (MS-ADPCM);
  - input, and the game loop.
- **`runtime/platform/`**: `platform_desktop.cpp` (SDL window, keyboard, SDL gamepads) and `platform_wiiu.cpp` (VPAD/KPAD input, SD paths, logging).
  - `autotest.h` holds scripted virtual-controller test hooks.
- **`cs/Port/Native.cs`**: the few functions implemented natively (save path, platform name).

## You need your own copy of TowerFall

This repository contains **no game code or assets**, only the port's own code: the translator, the runtime, the Wii U platform layer and the build scripts. Everything game-derived is generated locally from your own copy of the TowerFall 8-player build (macOS/Linux FNA version):

| Folder | What | Where it comes from |
|---|---|---|
| `decomp/` | decompiled C# | `setup.sh` (ILSpy) |
| `cs/` | decompiled C# + port edits | `setup.sh` (`tools/port_edits.py`) |
| `gen/` | generated C++ | `setup.sh` (`tools/cs2cpp`) |
| `wiiu/*.png` | icon and splash screens | `setup.sh` (`wiiu/make_assets.py`, crops the logo from your menu atlas) |

These folders are git-ignored. Don't commit them.

## Building

Requirements:
- **macOS:** .NET SDK 8+ (`dotnet`), Python 3, CMake, and SDL2 (Homebrew `sdl2` or `sdl2-compat`).
- **Wii U:** devkitPro with `wiiu-dev`, `wiiu-sdl2` and `wiiu-sdl2_mixer`.

```sh
# 1. generate decomp/, cs/, gen/ and the Wii U images from your game folder
./setup.sh /path/to/TF-8P          # the folder with TowerFall.app (or TowerFall.exe + FNA.dll)

# 2a. Mac test build
cmake -S . -B build/desktop -DCMAKE_BUILD_TYPE=RelWithDebInfo && make -C build/desktop -j8
TOWERFALL_ROOT=/path/to/TF-8P build/desktop/towerfall

# 2b. Wii U build
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC PATH=/opt/devkitpro/tools/bin:$PATH
cmake -S . -B build/wiiu -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake -DCMAKE_BUILD_TYPE=Release
make -C build/wiiu -j8             # -> build/wiiu/towerfall.wuhb (see wiiu/README.md for the SD card)
```

`scripts/rebuild.sh [debug|desktop]` regenerates `gen/` from `cs/` and rebuilds a Mac configuration (set `TF_REFS` to the folder with FNA.dll if it isn't at `../TF-8P/TowerFall.app/Contents/MacOS`).

On the Mac, keyboard controls are the game defaults: arrows to move, C jump, X shoot, Z alt-shoot, Shift dodge, S arrows, Enter start. F11 toggles fullscreen. SDL game controllers also work.

## Testing hooks (Mac: environment variables; Wii U: `autotest.txt` next to `Content/`)

| Variable | Meaning |
|---|---|
| `TF_VPADS=n` | Pads 0..n-1 become scripted virtual pads |
| `TF_PADKEYS=frame:pad:buttonbits:frames[:lx:ly];...` | Scripted button presses (XNA `Buttons` bit values) |
| `TF_RANDOM_AT=frame` | From this frame on, virtual pads press random buttons (soak test) |
| `TF_KEYS=frame:xnaKey:frames;...` | Scripted keyboard presses (Mac) |
| `TF_SHOT=f1,f2,...` / `TF_SHOT_DIR` | Save those frames as BMP (Mac) |
| `TF_QUIT_AT=frame` | Quit after this frame |
| `TF_CENSUS=1` | Log live GC objects by type after each collection |

**Never put an `autotest.txt` on a real SD card** unless you want the virtual players.
