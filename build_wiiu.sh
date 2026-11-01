#!/bin/sh
# One command from YOUR copy of TowerFall (8-player build) to a ready-to-copy Wii U SD card.
#
#   ./build_wiiu.sh /path/to/TowerFall-folder            # -> dist/sd/ (copy its contents to the SD card root)
#   ./build_wiiu.sh /path/to/TowerFall-folder /Volumes/SD  # also copies straight onto the mounted SD card
#
# Steps: check tools -> setup.sh (decompile + translate your copy) -> Wii U build -> SD card layout.
# Needs: .NET SDK 8+ (dotnet), python3, cmake, rsync, and devkitPro with wiiu-dev, wiiu-sdl2, wiiu-sdl2_mixer.
set -e
PORT="$(cd "$(dirname "$0")" && pwd)"
GAME="$1"
SD="$2"

if [ -z "$GAME" ] || [ ! -d "$GAME" ]; then
    echo "usage: $0 /path/to/TowerFall-folder [/path/to/SD-card]"; exit 1
fi
GAME="$(cd "$GAME" && pwd)"
if [ -n "$SD" ] && [ ! -d "$SD" ]; then echo "SD card folder not found: $SD"; exit 1; fi

# ---- 1. check the game folder and tools
CONTENT=""
for d in "$GAME" "$GAME/TowerFall.app/Contents/Resources" "$GAME/TowerFall.app/Contents/MacOS"; do
    if [ -d "$d/Content" ]; then CONTENT="$d"; break; fi
done
[ -n "$CONTENT" ] || { echo "No Content/ folder found in $GAME. Point this at the TowerFall game folder."; exit 1; }

missing=""
for t in dotnet python3 cmake rsync; do command -v "$t" >/dev/null 2>&1 || missing="$missing $t"; done
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"
export PATH="$DEVKITPRO/tools/bin:$PATH"
[ -f "$DEVKITPRO/cmake/WiiU.cmake" ] || missing="$missing devkitPro(wiiu-dev)"
[ -d "$DEVKITPRO/portlibs/wiiu/include/SDL2" ] || missing="$missing wiiu-sdl2"
if [ -n "$missing" ]; then
    echo "Missing:$missing"
    echo "Install devkitPro (https://devkitpro.org/wiki/Getting_Started), then:"
    echo "  sudo dkp-pacman -S wiiu-dev wiiu-sdl2 wiiu-sdl2_mixer"
    echo "and the .NET SDK, CMake and Python 3 (e.g. brew install dotnet cmake python)."
    exit 1
fi

# ---- 2. generate the game-derived code from this copy
echo "== Generating the port from $GAME"
sh "$PORT/setup.sh" "$GAME"

# ---- 3. build the Wii U app
echo "== Building for Wii U"
cmake -S "$PORT" -B "$PORT/build/wiiu" -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/WiiU.cmake" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$PORT/build/wiiu" -j8

# ---- 4. SD card layout
echo "== Staging the SD card files in dist/sd"
APP="$PORT/dist/sd/wiiu/apps/towerfall"
mkdir -p "$APP"
cp "$PORT/build/wiiu/towerfall.wuhb" "$PORT/dist/sd/wiiu/apps/towerfall.wuhb"
rsync -a --delete "$CONTENT/Content/" "$APP/Content/"
if [ -d "$CONTENT/DarkWorldContent" ]; then
    rsync -a --delete "$CONTENT/DarkWorldContent/" "$APP/DarkWorldContent/"
else
    echo "warning: no DarkWorldContent/ found next to Content/; Dark World will be missing."
fi

if [ -n "$SD" ]; then
    echo "== Copying to $SD"
    # no --delete at the top level: keeps save/, controls.txt and other apps on the card
    mkdir -p "$SD/wiiu/apps/towerfall"
    cp "$PORT/dist/sd/wiiu/apps/towerfall.wuhb" "$SD/wiiu/apps/towerfall.wuhb"
    rsync -a --delete "$APP/Content/" "$SD/wiiu/apps/towerfall/Content/"
    [ -d "$APP/DarkWorldContent" ] && rsync -a --delete "$APP/DarkWorldContent/" "$SD/wiiu/apps/towerfall/DarkWorldContent/"
    sync
    echo "Done. Eject the SD card and launch TowerFall from the Wii U Menu (Aroma)."
else
    echo "Done. Copy everything inside dist/sd/ to the root of your SD card."
fi
echo "dist/ contains your game's files and artwork: keep it for personal use, don't share it."
