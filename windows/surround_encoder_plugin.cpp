#include "surround_encoder_plugin.h"

// This must be included before many other Windows headers.
#include <windows.h>

#include <flutter/plugin_registrar_windows.h>

#include <memory>
#include <string>

#include "../src/common/capture_source.h"
#include "../src/common/encoder_pipeline.h"
#include "messages.g.h"

namespace surround_encoder {

// static
void SurroundEncoderPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows *registrar) {
  auto plugin = std::make_unique<SurroundEncoderPlugin>(registrar->messenger());
  SurroundEncoderPluginApi::SetUp(registrar->messenger(), plugin.get());
  registrar->AddPlugin(std::move(plugin));
}

SurroundEncoderPlugin::SurroundEncoderPlugin(
    flutter::BinaryMessenger *messenger)
    : event_api_(messenger) {}

SurroundEncoderPlugin::~SurroundEncoderPlugin() {
  std::string ignored;
  pipeline_.Stop(&ignored);
}

// Sends a pipeline event to Dart. Safe from any thread: the Windows
// BinaryMessenger send path is thread-safe.
void SurroundEncoderPlugin::SendEvent(const PipelineEvent& pipeline) {
  EncoderEventData event;
  switch (pipeline.state) {
    case PipelineState::kIdle:
      event.set_state(EncoderState::kIdle);
      break;
    case PipelineState::kStarting:
      event.set_state(EncoderState::kStarting);
      break;
    case PipelineState::kRunning:
      event.set_state(EncoderState::kRunning);
      break;
    case PipelineState::kStopped:
      event.set_state(EncoderState::kStopped);
      break;
    case PipelineState::kError:
      event.set_state(EncoderState::kError);
      break;
  }
  if (!pipeline.message.empty()) {
    event.set_message(pipeline.message);
  }
  event.set_encoded_seconds(pipeline.encoded_seconds);
  event.set_bitrate_kbps(pipeline.bitrate_kbps);
  event_api_.OnEncoderEvent(
      event, []() {}, [](const FlutterError&) {});
}

void SurroundEncoderPlugin::ListCaptureDevices(
    std::function<void(ErrorOr<::flutter::EncodableList> reply)> result) {
  ::flutter::EncodableList list;
  // Pigeon serializes data classes as positional lists; build them directly
  // because the generated ToEncodableList is private.
  for (const DeviceInfo& info : surround_encoder::ListCaptureDevices()) {
    ::flutter::EncodableValue device(::flutter::EncodableList{
        ::flutter::EncodableValue(
            static_cast<int>(info.kind == SourceKind::kSystem
                                 ? CaptureSourceKind::kSystem
                                 : CaptureSourceKind::kMicrophone)),
        ::flutter::EncodableValue(info.id),
        ::flutter::EncodableValue(info.name),
        ::flutter::EncodableValue(info.is_default),
    });
    list.push_back(std::move(device));
  }
  result(ErrorOr<::flutter::EncodableList>(std::move(list)));
}

void SurroundEncoderPlugin::GetCapabilities(
    std::function<void(ErrorOr<PlatformCapabilities> reply)> result) {
  PlatformCapabilities caps;
  caps.set_platform_name(PlatformName());
  // WASAPI loopback is available on every supported Windows version.
  caps.set_supports_system_capture(true);
  caps.set_supports_microphone(true);
  result(ErrorOr<PlatformCapabilities>(std::move(caps)));
}

void SurroundEncoderPlugin::Start(
    const EncoderConfig& config,
    std::function<void(std::optional<FlutterError> reply)> result) {
  std::lock_guard<std::mutex> lock(api_mutex_);
  if (pipeline_.is_running()) {
    result(FlutterError("start_failed", "a session is already running"));
    return;
  }

  PipelineConfig pipeline;
  if (config.capture_system()) {
    pipeline.capture_system = *config.capture_system();
  }
  if (config.capture_microphone()) {
    pipeline.capture_mic = *config.capture_microphone();
  }
  if (config.system_device_id() && !config.system_device_id()->empty()) {
    pipeline.system_device_id = *config.system_device_id();
  }
  if (config.microphone_device_id() &&
      !config.microphone_device_id()->empty()) {
    pipeline.mic_device_id = *config.microphone_device_id();
  }
  pipeline.out_channels =
      config.layout() && *config.layout() == ChannelLayout::kSurround51 ? 6
                                                                        : 2;
  pipeline.sample_rate =
      config.sample_rate() ? static_cast<int>(*config.sample_rate()) : 48000;
  pipeline.bitrate =
      config.bitrate() ? *config.bitrate() : 448000;
  pipeline.codec = config.codec() && *config.codec() == CodecKind::kDca
                       ? EncoderKind::kDca
                       : EncoderKind::kAc3;

  PipelineSink sink = PipelineSink::kStream;
  if (config.sink_kind()) {
    switch (*config.sink_kind()) {
      case SinkKind::kFile:
        sink = PipelineSink::kFile;
        break;
      case SinkKind::kUdp:
        sink = PipelineSink::kUdp;
        break;
      case SinkKind::kStream:
        sink = PipelineSink::kStream;
        break;
      default:
        result(FlutterError("start_failed", "unknown sink kind"));
        return;
    }
  }
  pipeline.sink = sink;
  if (sink == PipelineSink::kFile) {
    if (!config.file_path() || config.file_path()->empty()) {
      result(FlutterError("start_failed", "file sink requires a path"));
      return;
    }
    pipeline.file_path = *config.file_path();
  } else if (sink == PipelineSink::kUdp) {
    if (!config.udp_host() || config.udp_host()->empty() ||
        !config.udp_port()) {
      result(FlutterError("start_failed", "udp sink requires host and port"));
      return;
    }
    pipeline.udp_host = *config.udp_host();
    pipeline.udp_port = *config.udp_port();
  }

  if (!pipeline.capture_system && !pipeline.capture_mic) {
    result(FlutterError("start_failed",
                        "at least one capture source must be enabled"));
    return;
  }

  pipeline_.set_event_callback(
      [this](const PipelineEvent& event) { SendEvent(event); });
  pipeline_.set_packet_callback([this](const uint8_t* data, size_t size) {
    event_api_.OnEncodedPacket(
        std::vector<uint8_t>(data, data + size), []() {},
        [](const FlutterError&) {});
  });

  std::string error;
  if (!pipeline_.Start(pipeline, &error)) {
    result(FlutterError("start_failed", error));
    return;
  }
  result(std::nullopt);
}

void SurroundEncoderPlugin::Stop(
    std::function<void(std::optional<FlutterError> reply)> result) {
  std::lock_guard<std::mutex> lock(api_mutex_);
  std::string error;
  if (!pipeline_.Stop(&error) && !error.empty()) {
    result(FlutterError("stop_failed", error));
    return;
  }
  result(std::nullopt);
}

}  // namespace surround_encoder
