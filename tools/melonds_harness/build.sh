#!/bin/sh
# Build the headless harness against a melonDS checkout whose core library was
# built with: cmake -B build -G Ninja -DBUILD_QT_SDL=OFF -DENABLE_OGLRENDERER=OFF \
#             -DENABLE_GDBSTUB=OFF -DCMAKE_BUILD_TYPE=Release && ninja -C build
# usage: tools/melonds_harness/build.sh /path/to/melonDS [out]
set -e
M=${1:?melonDS checkout}
OUT=${2:-$(dirname "$0")/harness}
LIBS=$(find "$M/build" -name "*.a" | grep -v CMakeFiles)
g++ -O2 -std=c++20 -DJIT_ENABLED -DARCHITECTURE_x86_64 -DNDEBUG -I"$M/src" -I"$M/build/src" -I"$M/src/teakra/include" \
    "$(dirname "$0")/harness.cpp" -o "$OUT" \
    -Wl,--start-group $LIBS -Wl,--end-group -lpthread -lz -lzstd -lslirp -larchive -lenet 2>&1
echo "built $OUT"
