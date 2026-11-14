// Copyright 2026 The surround_encoder authors. MIT license.
//
// The encoder pipeline: capture sources -> resample/upmix -> mix -> AC-3
// encode -> sink (MPEG-TS file/UDP, or raw packets to the caller).

#ifndef SURROUND_ENCODER_COMMON_ENCODER_PIPELINE_H_
#define SURROUND_ENCODER_COMMON_ENCODER_PIPELINE_H_

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

#include "ac3_encoder.h"
#include "capture_source.h"
#include "ts_muxer.h"

namespace surround_encoder {

enum class PipelineSink {
  kFile,
  kUdp,
  kStream,  // raw packets delivered to the packet callback
};

enum class PipelineState {
  kIdle,
  kStarting,
  kRunning,
  kStopped,
  kError,
};

struct PipelineConfig {
  bool capture_system = true;
  bool capture_mic = false;
  std::string system_device_id;  // empty = default
  std::string mic_device_id;
  int out_channels = 6;   // 2 or 6
  int sample_rate = 48000;
  int64_t bitrate = 448000;
  PipelineSink sink = PipelineSink::kStream;
  std::string file_path;
  std::string udp_host;
  int udp_port = 0;
};

struct PipelineEvent {
  PipelineState state = PipelineState::kIdle;
  std::string message;
  double encoded_seconds = 0.0;
  double bitrate_kbps = 0.0;
};

using PipelineEventCallback = std::function<void(const PipelineEvent&)>;
using PipelinePacketCallback = std::function<void(const uint8_t*, size_t)>;

// Owns one encode session. Not thread-safe for concurrent Start/Stop; the
// plugin layer serializes calls.
class EncoderPipeline {
 public:
  EncoderPipeline() = default;
  ~EncoderPipeline();

  EncoderPipeline(const EncoderPipeline&) = delete;
  EncoderPipeline& operator=(const EncoderPipeline&) = delete;

  void set_event_callback(PipelineEventCallback callback) {
    event_callback_ = std::move(callback);
  }
  void set_packet_callback(PipelinePacketCallback callback) {
    packet_callback_ = std::move(callback);
  }

  bool Start(const PipelineConfig& config, std::string* error);
  // Stops the session. Returns false if the final flush/trailer failed.
  bool Stop(std::string* error);

  bool is_running() const { return running_.load(); }

 private:
  struct Source {
    std::unique_ptr<ICaptureSource> capture;
    SwrContext* swr = nullptr;
    CaptureFormat in_format;
    // Interleaved float32 samples already converted to the output format,
    // guarded by `mutex` of the pipeline.
    std::deque<float> fifo;
  };

  void EmitState(PipelineState state, const std::string& message);
  void EmitStats();
  void OnSourceChunk(size_t source_index, const float* interleaved,
                     size_t frame_count, CaptureFormat format);
  void TryEncodeLocked(std::string* error);

  PipelineConfig config_;
  std::vector<std::unique_ptr<Source>> sources_;
  Ac3Encoder encoder_;
  TsMuxer muxer_;

  std::mutex mutex_;  // guards sources_/encoder_/muxer_ interaction
  std::atomic<bool> running_{false};

  int64_t encoded_samples_ = 0;
  int64_t stats_bytes_ = 0;
  int64_t stats_bytes_at_last_emit_ = 0;
  int64_t stats_emit_counter_ = 0;
  int64_t mux_packet_index_ = 0;
  bool mux_write_failed_ = false;

  PipelineEventCallback event_callback_;
  PipelinePacketCallback packet_callback_;
};

}  // namespace surround_encoder

#endif  // SURROUND_ENCODER_COMMON_ENCODER_PIPELINE_H_
