# Bundled software AAC for livestream startup

An LG C3 capture showed AAC and video packets reaching Cobalt, followed by
failure to create an audio decoder. The available firmware-library paths did
not provide a usable decoder to the ARMv7 app. Playback failed before the first
frame. This does not identify why each firmware decoder path was unavailable.

The starterless build now supplies FDK-AAC 2.0.3 at pinned source commit
`716f4394641d53f0d79c9ddac3fa93b03a49f278`, cross-compiled with the same webOS SDK,
ARMv7/NEON/softfp flags and sysroot as the app. Cobalt already has an FDK-AAC
software audio adapter; the patch changes dependency loading, not audio framing,
timing or decoder selection. Only webOS prefers the private
`$ORIGIN/lib/libfdk-aac.so`; a failed private load retains the original system
library lookup and existing factory fallbacks. Symbol validation is unchanged.

The build stages the library, source revision and complete upstream `NOTICE`
under `lib/` and `content/licenses/`. Both the runtime archive and final IPK
retain them. Packaging a new binary without any of these assets fails. Older
runtime artifacts without the private-loader hook retain their existing inputs.
Shared Opus A/V, legacy video playback and `YTAF_SHARED_AV=0` remain unchanged.
This does not add compressed AAC feeding to Starfish.

The same fix passed a full ARM Gold build in the downstream fork, and its tester
confirmed that the previously failing livestream now plays on an LG C3:

- [Build and host decode validation](https://github.com/danzig666/youtube-webos-cobalt-adfree/actions/runs/40034711761)
- [Tested downstream release](https://github.com/danzig666/youtube-webos-cobalt-adfree/releases/tag/v2.6.31-beta.1)
- [Reported livestream](https://www.youtube.com/watch?v=YaJgf-Pw7S4)

This device result does not establish support for every TV, livestream, live
DVR seek or extended playback session.

## Host regression tests

With the overlay installed into a Cobalt 23.lts.6 checkout:

```sh
python3 scripts/test-fdk-aac-decoder.py /path/to/cobalt
python3 scripts/test-bundled-aac-package.py
```

The loader test compiles the actual patched loader against Cobalt's AAC headers
and checks missing libraries, private loading, firmware fallback, private-library
preference and rejection of missing symbols. Firmware lookup is isolated so the
test also works on hosts with FDK installed. It uses the existing host C++
compiler and needs no TV or SDK.

To also exercise real decoding, build the pinned FDK source for the host:

```sh
cmake -S /path/to/fdk-aac -B /tmp/fdk-aac-host \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DBUILD_PROGRAMS=OFF
cmake --build /tmp/fdk-aac-host --parallel 4
python3 scripts/test-fdk-aac-decoder.py /path/to/cobalt \
  --library /tmp/fdk-aac-host/libfdk-aac.so
```

This executes Cobalt's actual initialization, feed, decode, stream-information,
output-delay and teardown methods with a generated AAC-LC fixture. It verifies
non-silent 48 kHz stereo PCM and decoder recreation. Scheduling and output storage
are replaced at the fixture boundary; this is not a full player/clock test.
Ordinary CI runs both the real decode test and packaging/archive regressions.
