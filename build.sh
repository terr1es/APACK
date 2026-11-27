#!/bin/sh
# Builds apack against an existing Axiom build.
#
# Nothing here builds Axiom itself: this project consumes the compiler as a library, so a
# missing or stale Axiom build is reported rather than silently rebuilt. Rebuild that tree
# with `cmake --build ../Axiom-release/build` when its headers change.
set -e

CMAKE="${CMAKE:-/c/Program Files/CMake/bin/cmake.exe}"
CXX="${CXX:-C:/llvm-mingw/llvm-mingw-20261104-ucrt-x86_64/bin/clang++.exe}"

if [ ! -x "$CMAKE" ]; then
    echo "build.sh: cmake not found at '$CMAKE' (set CMAKE=...)" >&2
    exit 1
fi
if [ ! -x "$CXX" ]; then
    echo "build.sh: clang++ not found at '$CXX' (set CXX=...)" >&2
    exit 1
fi

"$CMAKE" -S . -B build -G "MinGW Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER="$CXX" \
    "$@"
"$CMAKE" --build build -j 8
echo "built: $(pwd)/build/apack.exe"
