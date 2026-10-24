// Minimal host-test declarations. Production uses the webOS SDK's LS2 headers.
#ifndef YTAF_TEST_SUPPORT_LUNASERVICE_H_
#define YTAF_TEST_SUPPORT_LUNASERVICE_H_

#include <glib.h>

struct LSHandle;
struct LSMessage;
using LSMessageToken = unsigned long;
constexpr LSMessageToken LSMESSAGE_TOKEN_INVALID = 0;
struct LSError {
  char* message;
};
using LSFilterFunc = bool (*)(LSHandle*, LSMessage*, void*);

extern "C" {
bool LSErrorInit(LSError* error);
void LSErrorFree(LSError* error);
bool LSRegister(const char* name, LSHandle** handle, LSError* error);
bool LSUnregister(LSHandle* handle, LSError* error);
bool LSGmainContextAttach(LSHandle* handle, GMainContext* context,
                          LSError* error);
GMainContext* LSGmainGetContext(LSHandle* handle, LSError* error);
bool LSCall(LSHandle* handle, const char* uri, const char* payload,
            LSFilterFunc callback, void* data, LSMessageToken* token,
            LSError* error);
bool LSCallOneReply(LSHandle* handle, const char* uri, const char* payload,
                    LSFilterFunc callback, void* data, LSMessageToken* token,
                    LSError* error);
bool LSCallCancel(LSHandle* handle, LSMessageToken token, LSError* error);
const char* LSMessageGetPayload(LSMessage* message);
}

#endif  // YTAF_TEST_SUPPORT_LUNASERVICE_H_
