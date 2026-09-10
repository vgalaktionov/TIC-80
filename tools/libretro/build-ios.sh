#!/usr/bin/env bash
# Build and link-check an iOS libretro core using the selected Xcode.
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
sdk=${SDK:-iphoneos}
arch=${ARCH:-arm64}
static=${LIBRETRO_STATIC:-OFF}
deployment=${IOS_DEPLOYMENT_TARGET:-13.0}
build_dir=${BUILD_DIR:-"$root/build/libretro-$sdk-$arch-$static"}

case "$sdk" in
    iphoneos) minimum_flag="-miphoneos-version-min=$deployment"; platform=IOS ;;
    iphonesimulator) minimum_flag="-mios-simulator-version-min=$deployment"; platform=IOSSIMULATOR ;;
    *) echo "SDK must be iphoneos or iphonesimulator" >&2; exit 1 ;;
esac

cmake -S "$root" -B "$build_dir" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="$sdk" \
    -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_SDL=OFF -DBUILD_LIBRETRO=ON -DBUILD_STATIC=ON \
    -DLIBRETRO_STATIC="$static" -DBUILD_WITH_ALL=ON "$@"
cmake --build "$build_dir" --target tic80_libretro --parallel "${JOBS:-8}"

if [[ "$static" == ON ]]; then
    # An archive can compile successfully while missing runtime implementations.
    # Link it independently of CMake's dependency list, as a frontend would.
    artifact="$build_dir/libretro-link-check.dylib"
    xcrun --sdk "$sdk" clang++ -arch "$arch" "$minimum_flag" \
        -dynamiclib -Wl,-u,_retro_init "$build_dir/lib/tic80_libretro.a" \
        -framework AVFoundation -framework AudioToolbox -framework Foundation \
        -o "$artifact"
else
    artifact="$build_dir/bin/tic80_libretro.dylib"
fi

xcrun lipo "$artifact" -verify_arch "$arch"
xcrun vtool -show-build "$artifact" > "$build_dir/libretro-platform.txt"
cat "$build_dir/libretro-platform.txt"
grep -q "platform $platform$" "$build_dir/libretro-platform.txt"
xcrun nm -gU "$artifact" > "$build_dir/libretro-symbols.txt"
for symbol in retro_init retro_deinit retro_api_version retro_set_environment \
    retro_set_video_refresh retro_set_audio_sample retro_set_audio_sample_batch \
    retro_set_input_poll retro_set_input_state retro_set_controller_port_device \
    retro_get_system_info retro_get_system_av_info retro_reset retro_run \
    retro_load_game retro_load_game_special retro_unload_game retro_get_region \
    retro_serialize_size retro_serialize retro_unserialize retro_get_memory_data \
    retro_get_memory_size retro_cheat_reset retro_cheat_set; do
    if ! grep -q " _$symbol$" "$build_dir/libretro-symbols.txt"; then
        echo "Missing libretro entry point: $symbol" >&2
        exit 1
    fi
done
for runtime in LUA:Lua MOON:Moon YUE:Yue FENNEL:Fennel WREN:Wren RUBY:Ruby \
    WASM:Wasm SCHEME:Scheme SQUIRREL:Squirrel PYTHON:Python JS:Js JANET:Janet; do
    option=${runtime%%:*}
    name=${runtime#*:}
    if grep -q "^BUILD_WITH_$option:BOOL=ON$" "$build_dir/CMakeCache.txt"; then
        if ! grep -q " _${name}ScriptConfig$" "$build_dir/libretro-symbols.txt"; then
            echo "Missing enabled scripting runtime: $name" >&2
            exit 1
        fi
    fi
done
echo "Verified $sdk $arch libretro core (static=$static) in $build_dir"
