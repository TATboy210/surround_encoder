// Copyright 2026 The surround_encoder authors. MIT license.
//
// dcaenc-backed DTS Coherent Acoustics (DCA) encoder. dcaenc is an
// independent LGPL implementation of the public ETSI TS 102 366 standard.
// Accepts interleaved float32 PCM at 48 kHz and emits complete DCA frames
// (512 samples per frame). Input channel order matches the WAV/MPEG order
// (FL FR FC LFE BL BR), which is what the shared mixer produces.

#ifndef SURROUND_ENCODER_COMMON_DCA_ENCODER_H_
#define SURROUND_ENCODER_COMMON_DCA_ENCODER_H_

#include "encoder.h"

#include <vector>

extern "C" {
#include <dcaenc.h>
}

namespace surround_encoder {

class DcaEncoder : public AudioEncoder {
 public:
  DcaEncoder() = default;
  ~DcaEncoder() override;

  DcaEncoder(const DcaEncoder&) = delete;
  DcaEncoder& operator=(const DcaEncoder&) = delete;

  // `channels` must be 2 (stereo) or 6 (5.1). `sample_rate` must be 48000.
  // dcaenc snaps `bitrate` to the nearest valid DCA core bitrate.
  bool Open(int channels, int sample_rate, int64_t bitrate,
            std::string* error) override;
  void Close() override;

  int frame_size() const override { return frame_size_; }

  void set_packet_callback(PacketCallback callback) override {
    packet_callback_ = std::move(callback);
  }

  bool Encode(const float* interleaved, int frame_count,
              std::string* error) override;

  // Emits the trailing partial frame via dcaenc_destroy.
  bool Flush(std::string* error) override;

 private:
  dcaenc_context context_ = nullptr;
  int channels_ = 0;
  int frame_size_ = 512;
  // Interleaved float32 samples awaiting a complete encoder frame.
  std::vector<float> pending_;
  PacketCallback packet_callback_;
};

}  // namespace surround_encoder

#endif  // SURROUND_ENCODER_COMMON_DCA_ENCODER_H_
