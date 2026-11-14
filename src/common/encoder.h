// Copyright 2026 The surround_encoder authors. MIT license.
//
// Common audio-encoder interface implemented by the AC-3 (FFmpeg) and DCA
// (dcaenc) backends. All encoders consume interleaved float32 PCM at 48 kHz
// and emit complete elementary-stream packets through the packet callback.

#ifndef SURROUND_ENCODER_COMMON_ENCODER_H_
#define SURROUND_ENCODER_COMMON_ENCODER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace surround_encoder {

using PacketCallback = std::function<void(const uint8_t* data, size_t size)>;

enum class EncoderKind {
  kAc3,
  kDca,
};

class AudioEncoder {
 public:
  virtual ~AudioEncoder() = default;

  // `channels` is 2 (stereo) or 6 (5.1); `sample_rate` is 48000.
  virtual bool Open(int channels, int sample_rate, int64_t bitrate,
                    std::string* error) = 0;
  virtual void Close() = 0;

  // Samples per elementary-stream frame (AC-3: 1536, DCA: 512).
  virtual int frame_size() const = 0;

  virtual void set_packet_callback(PacketCallback callback) = 0;

  // Queues interleaved float32 samples and encodes every complete frame.
  virtual bool Encode(const float* interleaved, int frame_count,
                      std::string* error) = 0;

  // Flushes buffered samples (zero-padded to a full frame where needed).
  virtual bool Flush(std::string* error) = 0;
};

}  // namespace surround_encoder

#endif  // SURROUND_ENCODER_COMMON_ENCODER_H_
