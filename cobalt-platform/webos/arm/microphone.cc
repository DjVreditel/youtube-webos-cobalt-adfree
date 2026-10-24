#include "starboard/microphone.h"

#include <cstring>
#include <memory>

#include "starboard/common/log.h"
#include "starboard/webos/arm/lg_voice_capture.h"

struct SbMicrophoneIdPrivate {};
struct SbMicrophonePrivate {
  explicit SbMicrophonePrivate(int size) : capture(size) {}
  ::starboard::shared::webos::LgVoiceCapture capture;
};

namespace {
SbMicrophoneIdPrivate kRemote;
SbMicrophone g_microphone = kSbMicrophoneInvalid;
}  // namespace

int SbMicrophoneGetAvailable(SbMicrophoneInfo* infos, int capacity) {
  if (infos && capacity > 0) {
    std::memset(infos, 0, sizeof(*infos));
    infos->id = &kRemote;
    infos->type = kSbMicrophoneUnknown;
    infos->max_sample_rate_hz = 16000;
    infos->min_read_size = 320;
    std::strcpy(infos->label, "LG Magic Remote");
  }
  return 1;
}

bool SbMicrophoneIsSampleRateSupported(SbMicrophoneId id, int rate) {
  return id == &kRemote && rate == 16000;
}

SbMicrophone SbMicrophoneCreate(SbMicrophoneId id, int rate, int size) {
  if (g_microphone || !SbMicrophoneIsSampleRateSupported(id, rate) ||
      size < 320 || size > 4 * 1024 * 1024)
    return kSbMicrophoneInvalid;
  return g_microphone = new SbMicrophonePrivate(size);
}

bool SbMicrophoneOpen(SbMicrophone microphone) {
  if (!microphone || microphone != g_microphone) return false;
  bool opened = microphone->capture.Open();
  if (!opened)
    SB_LOG(WARNING) << "LG microphone: " << microphone->capture.Error();
  return opened;
}

int SbMicrophoneRead(SbMicrophone microphone, void* data, int size) {
  if (!microphone || microphone != g_microphone || size < 0) return -1;
  int read = microphone->capture.Read(data, size);
  if (read < 0 && !microphone->capture.Error().empty())
    SB_LOG(WARNING) << "LG microphone: " << microphone->capture.Error();
  return read;
}

bool SbMicrophoneClose(SbMicrophone microphone) {
  if (!microphone || microphone != g_microphone) return false;
  microphone->capture.Close();
  return true;
}

void SbMicrophoneDestroy(SbMicrophone microphone) {
  if (!microphone || microphone != g_microphone) return;
  delete microphone;
  g_microphone = kSbMicrophoneInvalid;
}
