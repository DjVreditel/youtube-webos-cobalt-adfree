#include <dlfcn.h>

#include <cassert>
#include <cstring>

#include "lg_native_bus.h"

int main(int argc, char** argv) {
  using starboard::shared::webos::LgNativeAppBus;
  if (argc == 2 && std::strcmp(argv[1], "--unavailable") == 0) {
    assert(LgNativeAppBus() == nullptr);
    return 0;
  }
  void* library = dlopen("libhelpers.so.2", RTLD_NOW | RTLD_LOCAL);
  assert(library);
  using ExpectedHandle = LSHandle* (*)();
  auto expected =
      reinterpret_cast<ExpectedHandle>(dlsym(library, "TestNativeHandle"));
  assert(expected);
  assert(LgNativeAppBus() == expected());
  assert(LgNativeAppBus() == expected());
  dlclose(library);
  assert(LgNativeAppBus() == expected());
}
