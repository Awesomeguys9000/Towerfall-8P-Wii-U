# TowerFall 8P for Wii U

Play the 8-player version of **TowerFall** on your Wii U: up to 8 archers on one screen using the
Wii U GamePad, Wii Remotes, Pro Controllers and more.

This is an unofficial fan port. It contains **no game files**. You build it yourself from your own
copy of TowerFall, and the free **TowerFall 8P Builder** app does all of the work for you.

> ### Get the game: [**Buy TowerFall 8-Player on itch.io**](https://maddymakesgamesinc.itch.io/towerfall-8-player)
>
> The port needs your own copy of **TowerFall 8-Player**, the official eight-player version of
> TowerFall by Maddy Makes Games and Extremely OK Games. It's $4 (or more, if you'd like to support
> them) at **[maddymakesgamesinc.itch.io/towerfall-8-player](https://maddymakesgamesinc.itch.io/towerfall-8-player)**. After buying, download
> **`TF-8P.zip`**.

- [Credits and thanks](#credits-and-thanks)
- [Installation guide (start here)](#installation-guide)
- [Controls](wiiu/README.md#controllers)
- [Troubleshooting](#troubleshooting)
- [For developers](#for-developers)

## Credits and thanks

**None of this would be possible without the people below.** TowerFall is their game: its design,
code, art, music and sound are entirely their work. This port only carries that work over to the
Wii U, and it could only be built because of the tools and libraries that other people wrote and
shared for free. Thank you all.

If you enjoy TowerFall, please support the people who made it by [buying the game](https://maddymakesgamesinc.itch.io/towerfall-8-player).

### TowerFall Ascension and TowerFall 8-Player

Made by **Maddy Makes Games** (formerly Matt Makes Games) and **Extremely OK Games**.
[TowerFall 8-Player](https://maddymakesgamesinc.itch.io/towerfall-8-player) is the official eight-player version of TowerFall Ascension.
From the game's own credits:

| | |
|---|---|
| Director, designer, programmer | **Maddy Thorson** |
| Pixel artist, animator | **Pedro Medeiros** (MiniBoss) |
| Character designer, concept artist | **Amora Bettany** |
| Music | **Alec Holowka** |
| Sound design | **Kevin Regamey** and **Jeff Tangsoc** (Power Up Audio) |
| Trailers | **Kert Gartner** |
| Trial devtimes | **Kevin Regamey** and **Chris Spangler** |
| Porting | **Sickhead Games** and **Ethan Lee** |
| Localization | **Words of Magic**: Thierry, Josué Monchan, Thomas Faust, Renata Forteleoni, Alex Camilleri, Hannes Flor, Amora Bettany |

The game's credits also thank Noel Berry, Beau Blyth, Shawn Bouchard, Ashly Burch, Jamie Cheng,
Ryan Clark, Cori Dickinson, Kert Gartner, Tim Graupmann, Sara Gross, Thomas Hourdel, Rami Ismail,
Chevy Ray Johnston, Will Kirkby, Felix Kramer, Paul Lacen, Greg Lobanov, Jean-François Major, Corey
Nolan, Chris Plante, Kyle Pulver, John Sanders, Anita Sarkeesian, Kellee Santiago, Chris Spangler,
Nick Suttner, Nathan Vella, Matthew Wegner, Cellar Door, Cipher Prime, Folsom Street Foundry, Humble
Bundle, MonoGame and /r/TowerFall.

This port uses the 8-player build of TowerFall Ascension (version 1.3.2), including Dark World.
TowerFall and all of its content belong to their creators. This project is not affiliated with or
endorsed by them.

### Tools and libraries this port is built on

| Project | By | Used for |
|---|---|---|
| [FNA](https://fna-xna.github.io/) | Ethan Lee and contributors | The framework the Mac/Linux game runs on; the port recreates it on the Wii U |
| [ILSpy](https://github.com/icsharpcode/ILSpy) | ICSharpCode team | Reading the game's code from your copy |
| [Roslyn](https://github.com/dotnet/roslyn) and [.NET](https://dotnet.microsoft.com/) | Microsoft and the .NET Foundation | Translating the C# code to C++ |
| [devkitPro](https://devkitpro.org/), devkitPPC | devkitPro team | The compiler toolchain for the Wii U |
| [wut](https://github.com/devkitPro/wut) and wut-tools | wut contributors | Wii U system libraries and app packaging (`.wuhb`) |
| [SDL2](https://www.libsdl.org/) and SDL_mixer | Sam Lantinga and SDL contributors | Graphics, audio and input, on the Wii U and the Mac |
| [SDL2 for Wii U](https://github.com/devkitPro/SDL) | Wii U homebrew contributors | Running SDL2 on the Wii U |
| [Aroma](https://aroma.foryour.cafe/) | Maschell and contributors | The Wii U homebrew environment the game runs in |
| [Bloopair](https://github.com/GaryOderNichts/Bloopair) | GaryOderNichts | Using Switch, PlayStation and other Bluetooth controllers on the Wii U |
| [stb_image](https://github.com/nothings/stb) | Sean Barrett | Loading images |
| [Cemu](https://cemu.info/) | Cemu team | Testing on a computer |
| [CMake](https://cmake.org/), [Ninja](https://ninja-build.org/) | Kitware; Evan Martin and contributors | Running the build |
| [Python](https://www.python.org/), Tkinter, [PyInstaller](https://pyinstaller.org/), [certifi](https://github.com/certifi/python-certifi) | Python Software Foundation and contributors | The Builder app |

Thanks as well to the whole Wii U homebrew community, whose guides, research and open-source work
make projects like this possible.

The Wii U port itself, the C# to C++ translator and the Builder app are by
**[Awesomeguys9000](https://github.com/Awesomeguys9000)**, developed with help from Claude.

## Installation guide

New to Wii U homebrew? No problem. Follow these steps in order. It takes about 30 minutes the first
time, most of it waiting for downloads.

### What you need

- **A Wii U with Aroma installed.** Aroma is the homebrew environment that runs this game. If you
  don't have it yet, follow the free step-by-step guide at
  [wiiu.hacks.guide](https://wiiu.hacks.guide/). It also shows you how to prepare your SD card.
- **The SD card** from your Wii U (the one Aroma is on), plus a way to plug it into your computer.
- **A computer** running Windows 10/11, macOS or Linux, with about 3 GB of free space.
- **TowerFall 8-Player**, bought from [itch.io](https://maddymakesgamesinc.itch.io/towerfall-8-player) (Step 1 below).

### Step 1: Buy and download TowerFall 8-Player

1. Go to **[TowerFall 8-Player on itch.io](https://maddymakesgamesinc.itch.io/towerfall-8-player)** and click **Buy Now** ($4 or more).
2. After paying, itch.io shows the download files. Download **`TF-8P.zip`** (about 250 MB). Use this
   one whatever computer you have, even a Mac or Linux PC: it holds the files the Builder needs.
3. Unzip it. You get a folder called `TF-8P` with `Content`, `DarkWorldContent` and `TowerFall.app`
   inside. Leave it somewhere easy to find, such as your Downloads or Documents folder.

You don't have to install or run the game on your computer; the Builder only reads these files.

### Step 2: Download the TowerFall 8P Builder

Go to the [Releases page](https://github.com/Awesomeguys9000/Towerfall-8P-Wii-U/releases/latest)
and download the file for your computer:

| Your computer | Download | Then |
|---|---|---|
| Windows | `TowerFall8P-Builder-Windows-x64.zip` | Unzip it and open `TowerFall 8P Builder.exe`. If Windows shows "Windows protected your PC", click **More info → Run anyway**. |
| Mac with Apple Silicon (M1, M2, …) | `TowerFall8P-Builder-macOS-arm64.zip` | Unzip it and open `TowerFall 8P Builder`. |
| Mac with Intel | `TowerFall8P-Builder-macOS-x64.zip` | Unzip it and open `TowerFall 8P Builder`. If macOS says it can't check the app, right-click it and choose **Open**. |
| Linux | `TowerFall8P-Builder-Linux-x64.zip` | Unzip it and run `towerfall-8p-builder`. |

Not sure which Mac you have? Click the Apple menu → **About This Mac** and look at "Chip"
(Apple M-something) or "Processor" (Intel).

You don't need to install anything else first: the app includes everything it needs to run.

### Step 3: Choose your TowerFall folder

In the Builder, click **Choose…** next to "Your TowerFall folder" and pick the `TF-8P` folder.
A green tick and **Found TowerFall** appear when it's the right one.

### Step 4: Install devkitPro (first time only)

devkitPro is the free toolkit that builds Wii U software. It's the one thing the Builder can't
include, so it helps you install it. If "devkitPro Wii U toolchain" has a red cross:

1. Click **Install devkitPro…** and then **OK**.
2. Follow what happens on your computer:
   - **Windows:** the devkitPro installer opens. Click through it, and when it asks which
     components to install, tick **Wii U Development**.
   - **Mac:** the devkitPro installer opens. Click through it and enter your Mac password when
     asked.
   - **Linux:** the Builder shows three commands. Paste them into a terminal.
3. Click **Install devkitPro…** once more. It installs the Wii U packages; on Mac and Linux it asks
   for your password again.
4. Click **Check again**. Everything should now show a green tick or a down arrow.

A down arrow next to .NET, CMake or Ninja is fine: the Builder downloads those by itself when you build.

### Step 5: Build and copy to your SD card

1. Put your Wii U's SD card into your computer.
2. In the Builder, tick **Copy to SD card**, click **Choose…** and pick the SD card. On Windows it's
   a drive like `E:`; on a Mac it's under **Volumes** (or in Finder's sidebar).
3. Click **Build**. The first build takes about 10 to 20 minutes. Later builds are faster.
4. When it says **Done!**, eject the SD card safely (Windows: "Safely Remove Hardware"; Mac: the
   eject button in Finder) and put it back in your Wii U.

Your saves and settings already on the SD card are kept, so you can run the Builder again any time,
for example after a new version comes out.

### Step 6: Play

Turn on your Wii U, start Aroma as usual, and open **TowerFall 8P** from the Wii U Menu.

To add players, sync more controllers: press the **SYNC** button on the console and then the red
sync button on the controller, or hold **−** on the GamePad for 2 seconds. All the controls and
button layouts are in the [controller guide](wiiu/README.md#controllers).

### Troubleshooting

- **The Build button is greyed out.** Check that your TowerFall folder has a green tick, devkitPro
  shows a green tick (Step 4), and the SD card is chosen (or untick "Copy to SD card").
- **"This looks like the Windows (XNA) version" or "No Content folder found".** Pick the whole
  `TF-8P` folder you unzipped from `TF-8P.zip` (Step 1), not a folder inside it.
- **The build failed.** The box at the bottom of the Builder shows what went wrong. Often running it
  again fixes download hiccups. If not, open an
  [issue](https://github.com/Awesomeguys9000/Towerfall-8P-Wii-U/issues) and paste the last part of
  that log.
- **No SD card at hand?** Leave "Copy to SD card" unticked. The Builder puts the files in a
  `TowerFall8P-Builder/SD card files` folder in your home folder and opens it. Copy everything inside
  it to the root of your SD card.
- **TowerFall isn't on the Wii U Menu.** Check that `wiiu/apps/towerfall.wuhb` exists on the SD
  card, and that you started Aroma.
- **Something goes wrong in the game.** The log of the last run is on the SD card at
  `wiiu/apps/towerfall/towerfall.log`. Please include it in a bug report.

The files the Builder makes contain your copy of the game, so please keep them to yourself.

## For developers

### How it works

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

### No game files in this repository

This repository contains **no game code or assets**, only the port's own code: the translator, the runtime, the Wii U platform layer and the build scripts. Everything game-derived is generated locally from your own copy of the TowerFall 8-player build (macOS/Linux FNA version):

| Folder | What | Where it comes from |
|---|---|---|
| `decomp/` | decompiled C# | `setup.sh` (ILSpy) |
| `cs/` | decompiled C# + port edits | `setup.sh` (`tools/port_edits.py`) |
| `gen/` | generated C++ | `setup.sh` (`tools/cs2cpp`) |
| `wiiu/*.png` | icon and splash screens | `setup.sh` (`wiiu/make_assets.py`, crops the logo from your menu atlas) |

These folders are git-ignored. Don't commit them.

### Building from the command line

The same build without a window, from a clone of this repo (needs Python 3 and devkitPro):

```sh
./build_wiiu.sh /path/to/your/TowerFall-folder                  # files go to dist/sd/
./build_wiiu.sh /path/to/your/TowerFall-folder /Volumes/SDCARD  # also copies to the card
python3 builder/app.py                                          # the window, run from source
```

### Making the Builder app

`.github/workflows/builder-app.yml` builds the Windows, macOS (Apple Silicon and Intel) and Linux
apps with PyInstaller. Run it from the Actions tab, or push a tag like `v1.0` to attach the zips to
a GitHub release. Locally: `pip install pyinstaller certifi && python builder/build_app.py`. The
app bundles only the port's own source (`builder/build_app.py` lists it), never game files.

### Building step by step

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

### Testing hooks (Mac: environment variables; Wii U: `autotest.txt` next to `Content/`)

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
