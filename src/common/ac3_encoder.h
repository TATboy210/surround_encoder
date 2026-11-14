// Copyright 2026 The surround_encoder authors. MIT license.
//
// FFmpeg-backed AC-3 encoder. Accepts interleaved float32 PCM at 48 kHz and
// emits complete AC-3 elementary packets (1536 samples per frame).

#ifndef SURROUND_ENCODER_COMMON_AC3_ENCODER_H_
#define SURROUND_ENCODER_COMMON_AC3_ENCODER_H_

#include "encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace surround_encoder {

class Ac3Encoder : public AudioEncoder {
 public:
  Ac3Encoder() = default;
  ~Ac3Encoder() override;

  Ac3Encoder(const Ac3Encoder&) = delete;
  Ac3Encoder& operator=(const Ac3Encoder&) = delete;

  // `channels` must be 2 (stereo) or 6 (5.1). `sample_rate` must be 48000.
  bool Open(int channels, int sample_rate, int64_t bitrate,
            std::string* error) override;
  void Close() override;

  int frame_size() const override { return frame_size_; }

  void set_packet_callback(PacketCallback callback) override {
    packet_callback_ = std::move(callback);
  }

  // Queues interleaved float32 samples and encodes every complete 1536-sample
  // frame. Returns false and fills `error` on encoder failure.
  bool Encode(const float* interleaved, int frame_count,
              std::string* error) override;

  // Flushes buffered samples (zero-padded to a full frame where needed).
  bool Flush(std::string* error) override;

 private:
  bool EncodeFrame(AVFrame* frame, std::string* error);

  AVCodecContext* codec_ctx_ = nullptr;
  AVPacket* packet_ = nullptr;
  AVFrame* frame_ = nullptr;
  int channels_ = 0;
  int frame_size_ = 0;
  int64_t next_pts_ = 0;
  // Interleaved float32 samples awaiting a complete encoder frame.
  std::vector<float> pending_;
  PacketCallback packet_callback_;
};

}  // namespace surround_encoder

#endif  // SURROUND_ENCODER_COMMON_AC3_ENCODER_H_
