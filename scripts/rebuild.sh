#!/bin/sh
# regenerate C++ from C# and rebuild (arg: debug|desktop)
set -e
cd "$(dirname "$0")/../tools/cs2cpp"
export DOTNET_ROOT=/opt/homebrew/Cellar/dotnet/10.0.107/libexec
dotnet build -c Release 2>&1 | grep -E " error " || true
dotnet bin/Release/net10.0/cs2cpp.dll ../../cs ../../gen "${TF_REFS:-../../../TF-8P/TowerFall.app/Contents/MacOS}"
make -C ../../build/${1:-debug} -j8 2>&1 | grep -E "error|warning: unused" | head -30 || true
