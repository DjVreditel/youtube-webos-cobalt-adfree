#!/usr/bin/env bash
# Cross-compile the existing Cobalt AAC decoder dependency with the pinned SDK.
set -euo pipefail
: "${FDK_SOURCE:?}" "${FDK_BUILD:?}" "${WEBOS_SDK_ROOT:?}"
parallel="${FDK_PARALLEL:-4}"
[[ "$parallel" =~ ^[1-9][0-9]*$ ]] || exit 2
cmake="${CMAKE:-cmake}"
compiler="$WEBOS_SDK_ROOT/bin/arm-webos-linux-gnueabi"
sysroot="$WEBOS_SDK_ROOT/arm-webos-linux-gnueabi/sysroot"
flags='-march=armv7-a -mfpu=neon -mfloat-abi=softfp'
"$cmake" -S "$FDK_SOURCE" -B "$FDK_BUILD" \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=armv7 \
  -DCMAKE_C_COMPILER="$compiler-gcc" -DCMAKE_CXX_COMPILER="$compiler-g++" \
  -DCMAKE_SYSROOT="$sysroot" \
  -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" \
  -DCMAKE_BUILD_TYPE=MinSizeRel -DBUILD_SHARED_LIBS=ON -DBUILD_PROGRAMS=OFF
"$cmake" --build "$FDK_BUILD" --parallel "$parallel"
mkdir -p "$FDK_BUILD/bundle"
cp -L "$FDK_BUILD/libfdk-aac.so" "$FDK_BUILD/bundle/libfdk-aac.so"
"$compiler-strip" --strip-unneeded "$FDK_BUILD/bundle/libfdk-aac.so"
cp "$FDK_SOURCE/NOTICE" "$FDK_BUILD/bundle/fdk-aac.txt"
