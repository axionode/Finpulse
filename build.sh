#!/usr/bin/env bash
# Cross-build Finpulse.exe on Linux with MinGW-w64 (sudo apt install g++-mingw-w64-x86-64-posix cmake ninja-build)
set -euo pipefail
cd "$(dirname "$0")"
CFG="${1:-Release}"
cmake -S . -B build/mingw -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE="$CFG" > /dev/null
cmake --build build/mingw
c++ -O2 -std=c++20 tools/check_imports.cpp -o build/check_imports
build/check_imports build/mingw/Finpulse.exe
mkdir -p dist && cp build/mingw/Finpulse.exe dist/Finpulse.exe
ls -l dist/Finpulse.exe
