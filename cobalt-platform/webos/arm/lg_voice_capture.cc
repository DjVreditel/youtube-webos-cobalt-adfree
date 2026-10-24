#include "starboard/webos/arm/lg_voice_capture.h"

#include <glib.h>
#include <json-c/json.h>
#include <luna-service2/lunaservice.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <poll.h>
#include <thread>
#include <vector>

#include "starboard/webos/arm/lg_native_bus.h"

namespace starboard {
namespace shared {
namespace webos {
namespace {
const char kRecord[] = "luna://com.webos.service.voiceconductor/recordVoice";
const char kStop[] =
    "luna://com.webos.service.voiceconductor/stopRecordingVoice";

std::string StringField(json_object* object, const char* key) {
  json_object* value = nullptr;
  if (!json_object_object_get_ex(object, key, &value) ||
      !json_object_is_type(value, json_type_string))
    return {};
  return json_object_get_string(value);
}
}  // namespace

struct LgVoiceCapture::Impl {
  explicit Impl(size_t capacity) : buffer(capacity & ~size_t(1)) {}
  std::vector<unsigned char> buffer;
  size_t head = 0;
  size_t count = 0;
  std::mutex mutex;
  std::condition_variable ready_cv;
  std::thread worker;
  bool ready = false;
  bool registered = false;
  bool stopping = false;
  bool stop_sent = false;
  bool ended = false;
  std::string error;
  GMainContext* context = nullptr;
  GMainLoop* loop = nullptr;
  LSHandle* bus = nullptr;
  GMainContext* bus_context = nullptr;
  LSMessageToken token = LSMESSAGE_TOKEN_INVALID;
  LSMessageToken stop_token = LSMESSAGE_TOKEN_INVALID;
  int audio_fd = -1;
  bool connecting = false;
  std::string pending_path;
  std::chrono::steady_clock::time_point connect_deadline;
  bool pending_byte = false;
  unsigned char first_byte = 0;
  std::string ticket;
  std::chrono::steady_clock::time_point deadline;

  void Fail(const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex);
    error = reason;
  }

  // Serialize calls and cancellation with libhelpers' dispatch thread.
  void OnBus(const std::function<void()>& function) {
    if (!bus_context) {
      function();
      return;
    }
    struct Task {
      std::function<void()> function;
      std::mutex mutex;
      std::condition_variable cv;
      bool done = false;
    } task;
    task.function = function;
    g_main_context_invoke(
        bus_context,
        [](gpointer opaque) -> gboolean {
          auto* task = static_cast<Task*>(opaque);
          task->function();
          {
            std::lock_guard<std::mutex> lock(task->mutex);
            task->done = true;
            task->cv.notify_one();
          }
          return G_SOURCE_REMOVE;
        },
        &task);
    std::unique_lock<std::mutex> lock(task.mutex);
    task.cv.wait(lock, [&task] { return task.done; });
  }

  static bool QueueRecord(LSHandle*, LSMessage* message, void* opaque) {
    auto* self = static_cast<Impl*>(opaque);
    struct Reply {
      Impl* self;
      std::string payload;
    };
    auto* reply = new Reply{self, LSMessageGetPayload(message)};
    GSource* source = g_idle_source_new();
    g_source_set_callback(
        source,
        [](gpointer opaque) -> gboolean {
          auto* reply = static_cast<Reply*>(opaque);
          reply->self->OnRecord(reply->payload);
          return G_SOURCE_REMOVE;
        },
        reply, [](gpointer opaque) { delete static_cast<Reply*>(opaque); });
    g_source_attach(source, self->context);
    g_source_unref(source);
    return true;
  }

  void OnRecord(const std::string& payload) {
    auto* self = this;
    json_object* object = json_tokener_parse(payload.c_str());
    if (!object) {
      self->Fail("voiceconductor returned invalid JSON");
      return;
    }
    json_object* result = nullptr;
    if (StringField(object, "event") == "error" ||
        (json_object_object_get_ex(object, "returnValue", &result) &&
         !json_object_get_boolean(result))) {
      json_object* code = nullptr;
      json_object_object_get_ex(object, "errorCode", &code);
      self->Fail("recordVoice failed (" +
                 std::string(code ? json_object_get_string(code) : "-1") +
                 "): " + StringField(object, "errorText").substr(0, 160));
    } else {
      std::string ticket = StringField(object, "voiceTicket");
      std::string path = StringField(object, "websocket");
      if (!ticket.empty()) self->ticket = ticket;
      if (self->stopping) self->RequestStopRecording();
      if (!self->stopping && !path.empty() && self->audio_fd < 0 &&
          self->pending_path.empty()) {
        self->pending_path = path;
        self->connect_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
      }
      std::string event = StringField(object, "event");
      if (event == "stop" || event == "end" || event == "cancel") {
        std::lock_guard<std::mutex> lock(self->mutex);
        self->ended = true;
      }
    }
    json_object_put(object);
  }

  void Connect(const std::string& path) {
    // Despite its JSON field name, the official app uses an AF_UNIX stream.
    if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) {
      Fail("Invalid voice socket path");
      return;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
      Fail("Cannot create voice socket");
      return;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    int result = connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (result < 0 && errno != EINPROGRESS) {
      const int saved_errno = errno;
      close(fd);
      if ((saved_errno != ENOENT && saved_errno != ECONNREFUSED &&
           saved_errno != EAGAIN) ||
          std::chrono::steady_clock::now() >= connect_deadline) {
        pending_path.clear();
        Fail("Cannot connect to voice socket");
      }
      return;
    }
    audio_fd = fd;
    connecting = (result < 0);
    if (!connecting) pending_path.clear();
  }

  // Nonblocking connect() can report EINPROGRESS. Confirm completion before
  // receiving audio, and retry transient failures until the deadline.
  void FinishConnect() {
    if (!connecting) return;
    if (std::chrono::steady_clock::now() >= connect_deadline) {
      close(audio_fd);
      audio_fd = -1;
      connecting = false;
      pending_path.clear();
      Fail("Voice socket connection timed out");
      return;
    }
    pollfd descriptor{audio_fd, POLLOUT, 0};
    int result = poll(&descriptor, 1, 0);
    if (result == 0 || (result < 0 && errno == EINTR)) return;
    int socket_error = 0;
    socklen_t length = sizeof(socket_error);
    if (result < 0 ||
        getsockopt(audio_fd, SOL_SOCKET, SO_ERROR, &socket_error, &length) < 0)
      socket_error = errno;
    if (socket_error != 0) {
      close(audio_fd);
      audio_fd = -1;
      connecting = false;
      if (socket_error != ENOENT && socket_error != ECONNREFUSED &&
          socket_error != EAGAIN) {
        pending_path.clear();
        Fail("Cannot connect to voice socket");
      }
      return;
    }
    connecting = false;
    pending_path.clear();
  }

  static gboolean Poll(gpointer opaque) {
    auto* self = static_cast<Impl*>(opaque);
    if (self->stopping) return G_SOURCE_CONTINUE;
    if (std::chrono::steady_clock::now() >= self->deadline) {
      self->Fail("Voice session exceeded 30 seconds");
      StopOnLoop(self);
      return G_SOURCE_REMOVE;
    }
    if (self->audio_fd < 0 && !self->pending_path.empty())
      self->Connect(self->pending_path);
    if (self->audio_fd < 0) return G_SOURCE_CONTINUE;
    self->FinishConnect();
    if (self->audio_fd < 0 || self->connecting) return G_SOURCE_CONTINUE;
    unsigned char bytes[4096];
    for (;;) {
      ssize_t size = recv(self->audio_fd, bytes, sizeof(bytes), 0);
      if (size > 0) {
        std::lock_guard<std::mutex> lock(self->mutex);
        // Preserve unread audio; discard incoming overflow (Starboard rule).
        for (ssize_t i = 0; i < size; ++i) {
          if (!self->pending_byte) {
            self->first_byte = bytes[i];
            self->pending_byte = true;
          } else {
            if (self->buffer.size() - self->count >= 2) {
              self->buffer[(self->head + self->count++) % self->buffer.size()] =
                  self->first_byte;
              self->buffer[(self->head + self->count++) % self->buffer.size()] =
                  bytes[i];
            }
            self->first_byte = 0;
            self->pending_byte = false;
          }
        }
        std::memset(bytes, 0, sizeof(bytes));
      } else if (size == 0) {
        std::lock_guard<std::mutex> lock(self->mutex);
        self->ended = true;
        close(self->audio_fd);
        self->audio_fd = -1;
        break;
      } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
          self->Fail("Voice socket read failed");
        break;
      }
    }
    return G_SOURCE_CONTINUE;
  }

  static bool StopReply(LSHandle*, LSMessage*, void* opaque) {
    auto* self = static_cast<Impl*>(opaque);
    g_main_context_invoke(self->context, Quit, self);
    return true;
  }

  static gboolean Quit(gpointer opaque) {
    g_main_loop_quit(static_cast<Impl*>(opaque)->loop);
    return G_SOURCE_REMOVE;
  }

  void RequestStopRecording() {
    if (!bus || ticket.empty() || stop_sent) return;
    stop_sent = true;
    LSError ls_error;
    LSErrorInit(&ls_error);
    {
      json_object* request = json_object_new_object();
      json_object_object_add(request, "voiceTicket",
                             json_object_new_string(ticket.c_str()));
      OnBus([&] {
        LSCallOneReply(
            bus, kStop,
            json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN),
            StopReply, this, &stop_token, &ls_error);
      });
      json_object_put(request);
    }
    LSErrorFree(&ls_error);
  }

  static gboolean StopOnLoop(gpointer opaque) {
    auto* self = static_cast<Impl*>(opaque);
    if (self->stopping) return G_SOURCE_REMOVE;
    self->stopping = true;
    self->RequestStopRecording();
    // Allow late recordVoice replies time to deliver their recording ticket.
    // Keep shutdown bounded if the service never responds.
    GSource* source = g_timeout_source_new(2000);
    g_source_set_callback(source, Quit, self, nullptr);
    g_source_attach(source, self->context);
    g_source_unref(source);
    return G_SOURCE_REMOVE;
  }

  void Run() {
    context = g_main_context_new();
    g_main_context_push_thread_default(context);
    loop = g_main_loop_new(context, false);
    LSError ls_error;
    LSErrorInit(&ls_error);
    bus = LgNativeAppBus();
    if (bus) bus_context = LSGmainGetContext(bus, &ls_error);
    bool ok = bus && bus_context;
    if (bus_context) g_main_context_ref(bus_context);
    if (ok) {
      OnBus([&] {
        ok = LSCall(bus, kRecord,
                    "{\"subscribe\":true,\"socketType\":\"socket\"}",
                    QueueRecord, this, &token, &ls_error);
      });
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      registered = ok;
      ready = true;
      if (!ok)
        error = std::string("LS2 initialization failed: ") +
                (ls_error.message ? ls_error.message : "unknown error");
    }
    LSErrorFree(&ls_error);
    ready_cv.notify_all();
    if (ok) {
      deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
      GSource* poll = g_timeout_source_new(10);
      g_source_set_callback(poll, Poll, this, nullptr);
      g_source_attach(poll, context);
      g_source_unref(poll);
      g_main_loop_run(loop);
    }
    if (audio_fd >= 0) {
      close(audio_fd);
      audio_fd = -1;
    }
    if (bus) {
      // The barrier also drains callbacks already running on the shared bus.
      OnBus([&] {
        for (auto value : {token, stop_token}) {
          LSErrorInit(&ls_error);
          if (value != LSMESSAGE_TOKEN_INVALID)
            LSCallCancel(bus, value, &ls_error);
          LSErrorFree(&ls_error);
        }
      });
      bus = nullptr;
    }
    if (bus_context) {
      g_main_context_unref(bus_context);
      bus_context = nullptr;
    }
    GMainContext* finished_context = context;
    GMainLoop* finished_loop = loop;
    {
      std::lock_guard<std::mutex> lock(mutex);
      context = nullptr;
      loop = nullptr;
      registered = false;
      ticket.clear();
      pending_path.clear();
      pending_byte = false;
      first_byte = 0;
      std::fill(buffer.begin(), buffer.end(), 0);
      head = count = 0;
    }
    // Destroy pending sources before their opaque pointer becomes invalid.
    g_main_context_pop_thread_default(finished_context);
    g_main_loop_unref(finished_loop);
    g_main_context_unref(finished_context);
  }
};

LgVoiceCapture::LgVoiceCapture(size_t capacity) : impl_(new Impl(capacity)) {}
LgVoiceCapture::~LgVoiceCapture() { Close(); }

bool LgVoiceCapture::Open() {
  if (impl_->buffer.size() < 2) return false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->registered && impl_->error.empty() && !impl_->ended) {
      std::fill(impl_->buffer.begin(), impl_->buffer.end(), 0);
      impl_->head = impl_->count = 0;
      return true;
    }
  }
  Close();
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ready = impl_->stopping = impl_->stop_sent = impl_->ended = false;
    impl_->error.clear();
    impl_->token = LSMESSAGE_TOKEN_INVALID;
    impl_->stop_token = LSMESSAGE_TOKEN_INVALID;
  }
  impl_->worker = std::thread([this] { impl_->Run(); });
  std::unique_lock<std::mutex> lock(impl_->mutex);
  impl_->ready_cv.wait(lock, [this] { return impl_->ready; });
  return impl_->registered;
}

void LgVoiceCapture::Close() {
  if (!impl_->worker.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->context && impl_->registered)
      g_main_context_invoke(impl_->context, Impl::StopOnLoop, impl_.get());
  }
  impl_->worker.join();
}

int LgVoiceCapture::Read(void* output, size_t size) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!output || !impl_->registered || !impl_->error.empty()) return -1;
  size_t amount = std::min(size & ~size_t(1), impl_->count & ~size_t(1));
  auto* bytes = static_cast<unsigned char*>(output);
  for (size_t i = 0; i < amount; ++i) {
    bytes[i] = impl_->buffer[impl_->head];
    impl_->buffer[impl_->head] = 0;
    impl_->head = (impl_->head + 1) % impl_->buffer.size();
  }
  impl_->count -= amount;
  return amount ? static_cast<int>(amount) : impl_->ended ? -1 : 0;
}

std::string LgVoiceCapture::Error() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->error;
}
}  // namespace webos
}  // namespace shared
}  // namespace starboard
