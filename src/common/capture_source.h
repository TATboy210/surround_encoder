// Copyright 2026 The surround_encoder authors. MIT license.
//
// Platform-agnostic capture abstraction. Each platform backend implements
// `CreateCaptureSource` and delivers interleaved float32 PCM chunks.

#ifndef SURROUND_ENCODER_COMMON_CAPTURE_SOURCE_H_
#define SURROUND_ENCODER_COMMON_CAPTURE_SOURCE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace surround_encoder {

enum class SourceKind {
  kSystem,      // System playback (loopback / monitor)
  kMicrophone,  // Microphone capture
};

struct CaptureFormat {
  uint32_t sample_rate = 48000;
  uint32_t channels = 2;
};

// Device descriptor reported by the platform backend.
struct DeviceInfo {
  SourceKind kind = SourceKind::kSystem;
  std::string id;
  std::string name;
  bool is_default = false;
};

// Interleaved float32 PCM chunk, delivered on a backend-owned thread.
using AudioChunkCallback =
    std::function<void(const float* interleaved, size_t frame_count,
                       CaptureFormat format)>;

// One configured capture input.
struct SourceSpec {
  SourceKind kind = SourceKind::kSystem;
  // Empty string selects the platform default device.
  std::string device_id;
};

// A live capture source. `Start` begins delivering chunks through `callback`
// from a backend-owned thread; `Stop` joins that thread. Implementations must
// tolerate `Stop` being called while chunks are in flight.
class ICaptureSource {
 public:
  virtual ~ICaptureSource() = default;

  virtual bool Start(AudioChunkCallback callback, std::string* error) = 0;
  virtual void Stop() = 0;
};

// Enumerates capture devices on this platform (both system and microphone).
std::vector<DeviceInfo> ListCaptureDevices();

// Creates a capture source for `spec`. Returns nullptr and fills `error` on
// failure (device missing, unsupported platform, permission denied, ...).
std::unique_ptr<ICaptureSource> CreateCaptureSource(const SourceSpec& spec,
                                                    std::string* error);

// Human-readable platform name, e.g. "Windows", "macOS", "Linux".
const char* PlatformName();

}  // namespace surround_encoder

#endif  // SURROUND_ENCODER_COMMON_CAPTURE_SOURCE_H_
