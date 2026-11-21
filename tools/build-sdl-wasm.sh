#!/usr/bin/env bash
# tools/build-sdl-wasm.sh — SDL 3, built for WebAssembly with wasi-sdk, for
# windows and games in a browser (make wasm uses it when it's there).
#
#     tools/build-sdl-wasm.sh [wasi-sdk folder]
#
# It fetches SDL's source (the release below, from GitHub) into
# build/wasm-sdl/src, mends it for WASI (tools/sdl-wasi.patch: no threads,
# as Emscripten's SDL has none either; and nothing loaded at run time),
# and builds it with only what a browser can use: drawing in software,
# into a picture the page shows (the offscreen video driver), and events.
# What it makes: build/wasm-sdl/lib/libSDL3.a and build/wasm-sdl/include.
set -euo pipefail

SDL_VERSION=3.2.24
here=$(cd "$(dirname "$0")/.." && pwd)
sdk=${1:-${WASI_SDK:-}}
if [ -z "$sdk" ]; then
	for d in "$HOME"/tools/wasi-sdk-* /opt/wasi-sdk; do
		[ -x "$d/bin/clang" ] && sdk=$d
	done
fi
if [ -z "$sdk" ] || [ ! -x "$sdk/bin/clang" ]; then
	echo "build-sdl-wasm.sh: no wasi-sdk (give its folder, or unpack it into ~/tools)" >&2
	exit 1
fi
for tool in git cmake; do
	command -v $tool > /dev/null || { echo "build-sdl-wasm.sh: needs $tool" >&2; exit 1; }
done

out=$here/build/wasm-sdl
src=$out/src
if [ ! -d "$src" ]; then
	mkdir -p "$out"
	git -c advice.detachedHead=false clone -q --depth 1 --branch "release-$SDL_VERSION" https://github.com/libsdl-org/SDL.git "$src"
	git -C "$src" apply "$here/tools/sdl-wasi.patch"
fi

generator=()
command -v ninja > /dev/null && generator=(-G Ninja)
cmake -S "$src" -B "$out/build" "${generator[@]}" \
	-DCMAKE_TOOLCHAIN_FILE="$sdk/share/cmake/wasi-sdk-p1.cmake" -DWASI_SDK_PREFIX="$sdk" \
	-DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX="$out" \
	-DCMAKE_C_FLAGS="-D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_MMAN -D_WASI_EMULATED_GETPID" \
	-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF \
	-DSDL_PTHREADS=OFF \
	-DSDL_AUDIO=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_HIDAPI=OFF -DSDL_SENSOR=OFF -DSDL_POWER=OFF \
	-DSDL_CAMERA=OFF -DSDL_GPU=OFF -DSDL_DIALOG=OFF \
	-DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF \
	-DSDL_PIPEWIRE=OFF -DSDL_DUMMYVIDEO=ON -DSDL_OFFSCREEN=ON > "$out/cmake.log"
cmake --build "$out/build" > "$out/build.log"
cmake --install "$out/build" > /dev/null
echo "build/wasm-sdl: SDL $SDL_VERSION for WebAssembly"
