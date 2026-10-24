#ifndef STARBOARD_WEBOS_ARM_LG_VOICE_CAPTURE_H_
#define STARBOARD_WEBOS_ARM_LG_VOICE_CAPTURE_H_

#include <cstddef>
#include <memory>
#include <string>

namespace starboard {
namespace shared {
namespace webos {

// PCM supplied by webOS voiceconductor: mono, signed 16-bit LE, 16000 Hz.
// Audio lives only in the bounded in-memory buffer and is erased on Close.
class LgVoiceCapture {
 public:
  explicit LgVoiceCapture(size_t capacity);
  ~LgVoiceCapture();
  bool Open();
  void Close();
  int Read(void* output, size_t size);
  std::string Error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace webos
}  // namespace shared
}  // namespace starboard
#endif  // STARBOARD_WEBOS_ARM_LG_VOICE_CAPTURE_H_
