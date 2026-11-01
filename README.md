# TowerFall 8P → Wii U port

A source port of TowerFall (8-player build) from C#/FNA to C++. It runs on the Wii U (Aroma homebrew) and on macOS for testing.
The original game is never modified: the port reads the game data straight from your copy (`TF-8P/`).

**Just want to play it?** Download the [TowerFall 8P Builder app](#easiest-the-towerfall-8p-builder-app-windows-macos-linux): pick your TowerFall folder, click Build, and it makes the SD card for you.

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

## Easiest: the TowerFall 8P Builder app (Windows, macOS, Linux)

You need the **TowerFall 8-player build, macOS/Linux (FNA) version**: a folder with `Content/`,
`DarkWorldContent/` and either `TowerFall.app` or `TowerFall.exe` next to `FNA.dll`.

1. Download the builder for your computer from the
   [Releases page](https://github.com/Awesomeguys9000/Towerfall-8P-Wii-U/releases) and unzip it.
   It has Python built in, so there's nothing else to install for the app itself.
   - **Windows:** `TowerFall 8P Builder.exe`. If SmartScreen warns, click *More info → Run anyway*.
   - **macOS:** `TowerFall 8P Builder.app` (Apple Silicon `arm64` or Intel `x64` zip). The first time,
     right-click it and choose *Open* (it isn't signed with an Apple developer ID).
   - **Linux:** `towerfall-8p-builder`.
2. Open it and choose your TowerFall folder.
3. If devkitPro shows a ✗, click **Install devkitPro…** and follow the prompts (it opens devkitPro's
   own installer, then installs the Wii U packages). This is the only thing you install yourself:
   devkitPro can't be bundled. .NET, CMake and Ninja are downloaded into the builder's folder
   automatically.
4. Optionally tick **Copy to SD card** and choose your card, then click **Build**. The first build
   takes a few minutes.
5. Put the card in a Wii U running [Aroma](https://aroma.foryour.cafe/) and launch TowerFall from the Wii U Menu.

Without a card selected, the files are prepared in `~/TowerFall8P-Builder/SD card files/`; copy
everything inside it to the root of your SD card. Saves and `controls.txt` already on the card are
kept. Those files hold your game's data and artwork, so keep them to yourself. Controls and SD card
details are in [`wiiu/README.md`](wiiu/README.md).

### From the command line

The same build without a window, from a clone of this repo (needs Python 3 and devkitPro):

```sh
./build_wiiu.sh /path/to/your/TowerFall-folder                  # files go to dist/sd/
./build_wiiu.sh /path/to/your/TowerFall-folder /Volumes/SDCARD  # also copies to the card
python3 builder/app.py                                          # the window, run from source
```

### Making the app (maintainers)

`.github/workflows/builder-app.yml` builds the Windows, macOS (Apple Silicon and Intel) and Linux
apps with PyInstaller. Run it from the Actions tab, or push a tag like `v1.0` to attach the zips to
a GitHub release. Locally: `pip install pyinstaller certifi && python builder/build_app.py`. The
app bundles only the port's own source (`builder/build_app.py` lists it), never game files.

## Building step by step

Requirements:
- **macOS:** .NET SDK 10+ (`dotnet`), Python 3, CMake, and SDL2 (Homebrew `sdl2` or `sdl2-compat`).
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
