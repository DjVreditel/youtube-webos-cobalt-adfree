#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/ytaf-microphone-tests.XXXXXX")"
trap 'rm -rf "$test_dir"' EXIT
mkdir -p "$test_dir/include/starboard/webos"
ln -s "$repo_root/cobalt-platform/webos/arm" "$test_dir/include/starboard/webos/arm"

mkdir -p "$test_dir/helpers" "$test_dir/unsupported"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -shared -fPIC \
  "$repo_root/scripts/fixtures/native-helper.cc" \
  -o "$test_dir/helpers/libhelpers.so.2"
# A library without the required ABI must fail gracefully too.
printf 'extern "C" int unsupported() { return 0; }\n' | \
  "${CXX:-g++}" -x c++ -shared -fPIC - -o "$test_dir/unsupported/libhelpers.so.2"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror \
  -I"$test_dir/include" -I"$repo_root/cobalt-platform/webos/arm" \
  "$repo_root/scripts/test-lg-native-bus.cc" \
  "$repo_root/cobalt-platform/webos/arm/lg_native_bus.cc" -ldl \
  -o "$test_dir/test-lg-native-bus"
LD_LIBRARY_PATH="$test_dir/helpers" "$test_dir/test-lg-native-bus"
LD_LIBRARY_PATH="$test_dir/helpers" YTAF_TEST_NO_HANDLE=1 \
  "$test_dir/test-lg-native-bus" --unavailable
LD_LIBRARY_PATH="$test_dir/unsupported" "$test_dir/test-lg-native-bus" --unavailable

"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"$test_dir/include" -I"$repo_root/scripts/test-support" -I"$repo_root/cobalt-platform/webos/arm" \
  $(pkg-config --cflags glib-2.0 json-c) \
  "$repo_root/scripts/test-lg-voice-capture.cc" \
  "$repo_root/cobalt-platform/webos/arm/lg_voice_capture.cc" \
  $(pkg-config --libs glib-2.0 json-c) -o "$test_dir/test-lg-voice-capture"
"$test_dir/test-lg-voice-capture"
