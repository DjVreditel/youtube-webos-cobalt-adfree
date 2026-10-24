#include "starboard/webos/arm/lg_native_bus.h"

#include <dlfcn.h>

namespace starboard {
namespace shared {
namespace webos {
LSHandle* LgNativeAppBus() {
  // Keep the library loaded: its singleton owns the bus and dispatch thread.
  static void* library = dlopen("libhelpers.so.2", RTLD_NOW | RTLD_LOCAL);
  if (!library) return nullptr;
  using Instance = void* (*)();
  using Handle = LSHandle* (*)(void*);
  auto instance = reinterpret_cast<Instance>(
      dlsym(library, "_ZN18HLunaServiceBridge8instanceEv"));
  auto handle = reinterpret_cast<Handle>(
      dlsym(library, "_ZN18HLunaServiceBridge13PrivateHandleEv"));
  if (!instance || !handle) return nullptr;
  void* bridge = instance();
  return bridge ? handle(bridge) : nullptr;
}
}  // namespace webos
}  // namespace shared
}  // namespace starboard
