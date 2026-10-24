#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cobalt_root="${COBALT_SOURCE_DIR:-$repo_root/workdir/cobalt-23.lts.6}"
sdk_volume="${WEBOS_LINUX_SDK_VOLUME:-ytaf-webos-linux-sdk}"
image="${WEBOS_CODEC_BUILD_IMAGE:-cobalt-build-webos-codecs:latest}"
source_dir="$cobalt_root/out/fdk-aac-source"
build_dir="$cobalt_root/out/fdk-aac-webos"
# FDK-AAC 2.0.3; Cobalt 23's existing adapter uses the compatible 2.0.2 API.
revision=716f4394641d53f0d79c9ddac3fa93b03a49f278
if [[ ! -d "$source_dir/.git" ]]; then
  mkdir -p "$source_dir"
  git -C "$source_dir" init -q
fi
if ! git -C "$source_dir" cat-file -e "$revision^{commit}" 2>/dev/null; then
  git -C "$source_dir" fetch --depth 1 https://github.com/mstorsjo/fdk-aac.git "$revision"
fi
git -C "$source_dir" checkout --detach "$revision"
test "$(git -C "$source_dir" rev-parse HEAD)" = "$revision"
git -C "$source_dir" diff --quiet HEAD --
"$repo_root/scripts/ensure-webos-codec-image.sh"
docker volume inspect "$sdk_volume" >/dev/null
mkdir -p "$build_dir"
docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" \
  -v "$repo_root:/workspace:ro" -v "$cobalt_root:/code" -v "$sdk_volume:/sdk:ro" \
  -e FDK_SOURCE=/code/out/fdk-aac-source -e FDK_BUILD=/code/out/fdk-aac-webos \
  -e WEBOS_SDK_ROOT=/sdk/arm-webos-linux-gnueabi_sdk-buildroot \
  -e FDK_PARALLEL="${NINJA_PARALLEL:-4}" \
  "$image" bash /workspace/scripts/run-fdk-aac-build.sh
printf 'version=2.0.3\nsource_sha=%s\n' "$revision" > "$build_dir/bundle/fdk-aac-build.txt"
echo "Bundled ARM FDK-AAC dependency: $build_dir/bundle/libfdk-aac.so"
