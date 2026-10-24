// Host fixture for libhelpers' named app connection; no TV service is used.
#include <cstdlib>

struct LSHandle {};

namespace {
LSHandle handle;
}

class HLunaServiceBridge {
 public:
  static HLunaServiceBridge* instance();
  LSHandle* PrivateHandle();
};

HLunaServiceBridge* HLunaServiceBridge::instance() {
  static HLunaServiceBridge bridge;
  return &bridge;
}

LSHandle* HLunaServiceBridge::PrivateHandle() {
  return std::getenv("YTAF_TEST_NO_HANDLE") ? nullptr : &handle;
}

extern "C" LSHandle* TestNativeHandle() { return &handle; }
