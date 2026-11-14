// Copyright 2026 The surround_encoder authors. MIT license.
//
// PulseAudio capture backend (works with PipeWire through pipewire-pulse):
// system audio from the default sink's monitor source, microphone from the
// default source. libpulse resamples to our requested float32 48 kHz spec.

#ifndef SURROUND_ENCODER_LINUX_PULSE_CAPTURE_H_
#define SURROUND_ENCODER_LINUX_PULSE_CAPTURE_H_

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "../common/capture_source.h"

namespace surround_encoder {

class PulseCapture : public ICaptureSource {
 public:
  explicit PulseCapture(const SourceSpec& spec) : spec_(spec) {}
  ~PulseCapture() override;

  bool Start(AudioChunkCallback callback, std::string* error) override;
  void Stop() override;

 private:
  static void ThreadMainStatic(PulseCapture* self);
  void ThreadMain();

  SourceSpec spec_;
  AudioChunkCallback callback_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  CaptureFormat format_;
};

}  // namespace surround_encoder

#endif  // SURROUND_ENCODER_LINUX_PULSE_CAPTURE_H_
