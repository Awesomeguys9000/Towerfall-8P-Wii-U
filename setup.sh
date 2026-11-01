#!/bin/sh
# Generate the game-derived parts of the port from YOUR OWN copy of TowerFall (8-player build).
# Nothing from the game is stored in this repository.
#
#   ./setup.sh /path/to/TF-8P
#
# The folder must contain the macOS/Linux (FNA) build, i.e. TowerFall.exe next to FNA.dll and the
# Mono assemblies (mscorlib.dll, System*.dll). For the macOS app bundle these are in
# TowerFall.app/Contents/MacOS. Requires: .NET SDK 8+ (dotnet), python3.
set -e
PORT="$(cd "$(dirname "$0")" && pwd)"
GAME="$1"
if [ -z "$GAME" ] || [ ! -d "$GAME" ]; then
    echo "usage: $0 /path/to/TowerFall-folder"; exit 1
fi
GAME="$(cd "$GAME" && pwd)"

# locate the FNA build's assemblies
REFS=""
for d in "$GAME/TowerFall.app/Contents/MacOS" "$GAME" "$GAME/lib"; do
    if [ -f "$d/FNA.dll" ] && [ -f "$d/TowerFall.exe" ] && [ -f "$d/mscorlib.dll" ]; then REFS="$d"; break; fi
done
if [ -z "$REFS" ]; then
    echo "Could not find TowerFall.exe + FNA.dll + mscorlib.dll in $GAME (need the macOS/Linux FNA build)."; exit 1
fi
echo "Using game assemblies in: $REFS"

# dotnet location (Homebrew installs need DOTNET_ROOT for global tools)
if [ -z "$DOTNET_ROOT" ]; then
    DN="$(command -v dotnet || true)"
    if [ -n "$DN" ]; then
        REAL="$(python3 -c "import os,sys;print(os.path.realpath(sys.argv[1]))" "$DN")"
        CAND="$(dirname "$(dirname "$REAL")")/libexec"
        [ -d "$CAND/shared" ] && export DOTNET_ROOT="$CAND"
    fi
fi

# 1. decompile
if ! command -v ilspycmd >/dev/null 2>&1 && [ ! -x "$HOME/.dotnet/tools/ilspycmd" ]; then
    dotnet tool install -g ilspycmd
fi
ILSPY="$(command -v ilspycmd || echo "$HOME/.dotnet/tools/ilspycmd")"
rm -rf "$PORT/decomp"
"$ILSPY" -p -o "$PORT/decomp" "$REFS/TowerFall.exe"

# 2. apply the port's edits
python3 "$PORT/tools/port_edits.py" "$PORT/decomp" "$PORT/cs"

# 3. translate C# -> C++
( cd "$PORT/tools/cs2cpp" && dotnet build -c Release >/dev/null && dotnet bin/Release/*/cs2cpp.dll ../../cs ../../gen "$REFS" )

# 4. Wii U icon / splash images (cropped from the game's own menu atlas)
python3 "$PORT/wiiu/make_assets.py" "$GAME/Content/Atlas"

echo
echo "Done. Now build: see README.md (Mac test build or Wii U build)."
