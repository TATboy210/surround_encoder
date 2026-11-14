// Copyright 2026 The surround_encoder authors. MIT license.
//
// WASAPI capture backend: system audio via loopback, microphone via shared
// capture. Windows implementation of the common capture abstraction.

#ifndef SURROUND_ENCODER_WINDOWS_WASAPI_CAPTURE_H_
#define SURROUND_ENCODER_WINDOWS_WASAPI_CAPTURE_H_

#include <atomic>
#include <string>
#include <thread>
#include <windows.h>

#include "../common/capture_source.h"

namespace surround_encoder {

class WasapiCapture : public ICaptureSource {
 public:
  explicit WasapiCapture(const SourceSpec& spec) : spec_(spec) {}
  ~WasapiCapture() override;

  bool Start(AudioChunkCallback callback, std::string* error) override;
  void Stop() override;

 private:
  static DWORD WINAPI ThreadMainStatic(LPVOID param);
  void ThreadMain();

  std::thread thread_;
  std::atomic<bool> running_{false};
  HANDLE stop_event_ = nullptr;

  SourceSpec spec_;
  AudioChunkCallback callback_;
  std::string thread_error_;
};

}  // namespace surround_encoder

#endif  // SURROUND_ENCODER_WINDOWS_WASAPI_CAPTURE_H_
