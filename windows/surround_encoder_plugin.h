#ifndef FLUTTER_PLUGIN_SURROUND_ENCODER_PLUGIN_H_
#define FLUTTER_PLUGIN_SURROUND_ENCODER_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>

#include <memory>
#include <mutex>

#include "../src/common/encoder_pipeline.h"
#include "messages.g.h"

namespace surround_encoder {

class SurroundEncoderPlugin : public flutter::Plugin,
                              public SurroundEncoderPluginApi {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar);

  explicit SurroundEncoderPlugin(flutter::BinaryMessenger *messenger);

  virtual ~SurroundEncoderPlugin();

  // Disallow copy and assign.
  SurroundEncoderPlugin(const SurroundEncoderPlugin&) = delete;
  SurroundEncoderPlugin& operator=(const SurroundEncoderPlugin&) = delete;

  // SurroundEncoderPluginApi:
  void ListCaptureDevices(
      std::function<void(ErrorOr<::flutter::EncodableList> reply)> result)
      override;
  void GetCapabilities(
      std::function<void(ErrorOr<PlatformCapabilities> reply)> result)
      override;
  void Start(
      const EncoderConfig& config,
      std::function<void(std::optional<FlutterError> reply)> result) override;
  void Stop(
      std::function<void(std::optional<FlutterError> reply)> result) override;

 private:
  void SendEvent(const PipelineEvent& pipeline);

  flutter::BinaryMessenger* messenger_;
  SurroundEncoderEventApi event_api_;
  EncoderPipeline pipeline_;
  std::mutex api_mutex_;
};

}  // namespace surround_encoder

#endif  // FLUTTER_PLUGIN_SURROUND_ENCODER_PLUGIN_H_
