#!/usr/bin/env bash
set -euo pipefail
trap 'chown -R "$HOST_UID:$HOST_GID" /output' EXIT
apt-get update
apt-get install -y --no-install-recommends \
    cmake ninja-build gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 \
    binutils-mingw-w64-x86-64 file ca-certificates build-essential ruby rake python3
cp -a /input /tmp/tic80
# Janet's MinGW CMake rule expects this name but builds a native generator.
ln -s /usr/bin/make /usr/local/bin/mingw32-make
cmake -S /tmp/tic80 -B /tmp/winbuild -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/recipe/mingw.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_PRO=ON -DBUILD_WITH_ALL=ON -DBUILD_STATIC=ON -DBUILD_SDLGPU=ON \
    -DGITBASH=/bin/bash -DCMAKE_EXE_LINKER_FLAGS=-flto=8 \
    "-DCMAKE_CXX_STANDARD_LIBRARIES=-lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -lwinpthread"
cmake --build /tmp/winbuild --target tic80 --parallel 8
cp /tmp/winbuild/bin/tic80.exe /output/tic80-pro.exe
cp /tmp/winbuild/CMakeCache.txt /output/CMakeCache.txt
x86_64-w64-mingw32-nm /output/tic80-pro.exe > /output/symbols.txt
{
    file /output/tic80-pro.exe
    for flag in PRO WITH_ALL WITH_LUA WITH_MOON WITH_FENNEL WITH_YUE WITH_JS WITH_RUBY WITH_PYTHON WITH_SCHEME WITH_SQUIRREL WITH_WREN WITH_JANET WITH_WASM; do
        grep -x "BUILD_${flag}:BOOL=ON" /output/CMakeCache.txt
    done
    python3 /recipe/verify-runtimes.py /output
    x86_64-w64-mingw32-objdump -p /output/tic80-pro.exe | sed -n '/DLL Name:/p'
} | tee /output/verification.txt
cd /output
sha256sum tic80-pro.exe > SHA256SUMS
