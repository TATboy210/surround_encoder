// Copyright 2026 The surround_encoder authors. MIT license.
//
// Linux capture-device enumeration and factory, backed by libpulse.

#include <pulse/pulseaudio.h>

#include <memory>
#include <string>
#include <vector>

#include "../common/capture_source.h"
#include "pulse_capture.h"

namespace surround_encoder {

namespace {

struct WaitState {
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  bool ok = false;

  void Wait() {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [this] { return done; });
  }
  void Finish(bool success) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      done = true;
      ok = success;
    }
    cv.notify_all();
  }
};

struct DeviceCollector {
  std::vector<DeviceInfo>* devices;
  SourceKind kind;  // what this enumeration maps to
  std::string default_sink;
  std::string default_source;
  WaitState wait;
};

void StateCb(pa_context* context, void* userdata) {
  auto* wait = static_cast<WaitState*>(userdata);
  switch (pa_context_get_state(context)) {
    case PA_CONTEXT_READY:
      wait->Finish(true);
      break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
      wait->Finish(false);
      break;
    default:
      break;
  }
}

void SourceListCb(const pa_source_info* info, int eol, void* userdata) {
  if (eol != 0) {
    static_cast<DeviceCollector*>(userdata)->wait.Finish(true);
    return;
  }
  auto* collector = static_cast<DeviceCollector*>(userdata);
  if (info->monitor_of_sink != nullptr) {
    return;  // Monitors are surfaced through sink enumeration instead.
  }
  DeviceInfo device;
  device.kind = collector->kind;
  device.id = info->name;
  device.name = info->description != nullptr ? info->description : info->name;
  device.is_default = info->name == collector->default_source;
  collector->devices->push_back(std::move(device));
}

void SinkListCb(const pa_sink_info* info, int eol, void* userdata) {
  if (eol != 0) {
    static_cast<DeviceCollector*>(userdata)->wait.Finish(true);
    return;
  }
  auto* collector = static_cast<DeviceCollector*>(userdata);
  DeviceInfo device;
  device.kind = collector->kind;
  device.id = std::string(info->name) + ".monitor";
  device.name = info->description != nullptr
                    ? std::string(info->description) + " (system audio)"
                    : device.id;
  device.is_default = info->name == collector->default_sink;
  collector->devices->push_back(std::move(device));
}

void ServerInfoCb2(const pa_server_info* info, void* userdata) {
  auto* collector = static_cast<DeviceCollector*>(userdata);
  collector->default_sink = info->default_sink_name;
  collector->default_source = info->default_source_name;
}

// Runs one context operation to completion on `mainloop`.
void RunOperation(pa_mainloop* mainloop, pa_operation* op) {
  if (op == nullptr) {
    return;
  }
  while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
    pa_mainloop_iterate(mainloop, 1, nullptr);
  }
  pa_operation_unref(op);
}

}  // namespace

std::vector<DeviceInfo> ListCaptureDevices() {
  std::vector<DeviceInfo> devices;

  pa_mainloop* mainloop = pa_mainloop_new();
  if (mainloop == nullptr) {
    return devices;
  }
  pa_context* context =
      pa_context_new(pa_mainloop_get_api(mainloop), "surround_encoder");
  WaitState connect_wait;
  pa_context_set_state_callback(context, StateCb, &connect_wait);
  pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
  connect_wait.Wait();

  if (connect_wait.ok && pa_context_get_state(context) == PA_CONTEXT_READY) {
    DeviceCollector collector;
    collector.devices = &devices;
    collector.kind = SourceKind::kSystem;

    pa_operation* op =
        pa_context_get_server_info(context, ServerInfoCb2, &collector);
    RunOperation(mainloop, op);

    collector.kind = SourceKind::kSystem;
    op = pa_context_get_sink_info_list(context, SinkListCb, &collector);
    RunOperation(mainloop, op);

    collector.kind = SourceKind::kMicrophone;
    collector.wait = WaitState();
    op = pa_context_get_source_info_list(context, SourceListCb, &collector);
    RunOperation(mainloop, op);
  }

  pa_context_disconnect(context);
  pa_context_unref(context);
  pa_mainloop_free(mainloop);
  return devices;
}

std::unique_ptr<ICaptureSource> CreateCaptureSource(const SourceSpec& spec,
                                                    std::string* error) {
  (void)error;
  return std::make_unique<PulseCapture>(spec);
}

const char* PlatformName() { return "Linux"; }

}  // namespace surround_encoder
