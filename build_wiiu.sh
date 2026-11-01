#!/bin/sh
# One command from YOUR copy of TowerFall (8-player build) to a ready-to-copy Wii U SD card.
#
#   ./build_wiiu.sh /path/to/TowerFall-folder              # -> dist/sd/ (copy its contents to the SD card root)
#   ./build_wiiu.sh /path/to/TowerFall-folder /Volumes/SD  # also copies straight onto the mounted SD card
#
# Same steps as the TowerFall 8P Builder app (builder/pipeline.py). .NET, CMake and Ninja are
# downloaded when missing; devkitPro (wiiu-dev, wiiu-sdl2, wiiu-sdl2_mixer) must be installed.
exec python3 "$(dirname "$0")/builder/pipeline.py" "$@"
