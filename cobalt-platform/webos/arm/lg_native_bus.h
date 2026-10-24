#ifndef STARBOARD_WEBOS_ARM_LG_NATIVE_BUS_H_
#define STARBOARD_WEBOS_ARM_LG_NATIVE_BUS_H_

struct LSHandle;

namespace starboard {
namespace shared {
namespace webos {
// Borrow the app-named connection already managed by webOS libhelpers/SDL.
// The caller must never detach or unregister it.
LSHandle* LgNativeAppBus();
}  // namespace webos
}  // namespace shared
}  // namespace starboard
#endif  // STARBOARD_WEBOS_ARM_LG_NATIVE_BUS_H_
