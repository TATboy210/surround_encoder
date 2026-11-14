// Copyright 2026 The surround_encoder authors. MIT license.
//
// MPEG-TS muxer for encoded elementary streams. Supports local files and
// udp://host:port destinations.

#ifndef SURROUND_ENCODER_COMMON_TS_MUXER_H_
#define SURROUND_ENCODER_COMMON_TS_MUXER_H_

#include <cstdint>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
}

namespace surround_encoder {

class TsMuxer {
 public:
  TsMuxer() = default;
  ~TsMuxer();

  TsMuxer(const TsMuxer&) = delete;
  TsMuxer& operator=(const TsMuxer&) = delete;

  // `url` is either a local file path or an `udp://host:port` destination.
  // `codec_id`, `sample_rate`, `channels` and `bit_rate` describe the audio
  // elementary stream to wrap.
  bool Open(const std::string& url, AVCodecID codec_id, int sample_rate,
            int channels, int64_t bit_rate, std::string* error);

  // Writes one encoded packet. `pts` is in 1/sample_rate units.
  bool WritePacket(const uint8_t* data, size_t size, int64_t pts,
                   std::string* error);

  bool Close(std::string* error);

  bool is_open() const { return format_ctx_ != nullptr; }

 private:
  AVFormatContext* format_ctx_ = nullptr;
  AVStream* stream_ = nullptr;
  int sample_rate_ = 48000;
  int64_t packet_index_ = 0;
};

}  // namespace surround_encoder

#endif  // SURROUND_ENCODER_COMMON_TS_MUXER_H_
