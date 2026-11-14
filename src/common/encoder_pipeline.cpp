// Copyright 2026 The surround_encoder authors. MIT license.

#include "encoder_pipeline.h"

namespace surround_encoder {

EncoderPipeline::~EncoderPipeline() {
  std::string ignored;
  Stop(&ignored);
}

void EncoderPipeline::EmitState(PipelineState state,
                                const std::string& message) {
  if (!event_callback_) {
    return;
  }
  PipelineEvent event;
  event.state = state;
  event.message = message;
  event.encoded_seconds =
      static_cast<double>(encoded_samples_) / config_.sample_rate;
  event.bitrate_kbps =
      static_cast<double>(stats_bytes_ - stats_bytes_at_last_emit_) * 8.0 /
      1000.0;
  stats_bytes_at_last_emit_ = stats_bytes_;
  event_callback_(event);
}

void EncoderPipeline::EmitStats() {
  if (!event_callback_) {
    return;
  }
  PipelineEvent event;
  event.state = PipelineState::kRunning;
  event.encoded_seconds =
      static_cast<double>(encoded_samples_) / config_.sample_rate;
  event.bitrate_kbps =
      static_cast<double>(stats_bytes_ - stats_bytes_at_last_emit_) * 8.0 /
      1000.0;
  stats_bytes_at_last_emit_ = stats_bytes_;
  event_callback_(event);
}

void EncoderPipeline::OnSourceChunk(size_t source_index, const float* data,
                                    size_t frame_count, CaptureFormat format) {
  if (!running_.load()) {
    return;
  }
  std::string failure;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    Source* source = sources_[source_index].get();
    if (source == nullptr) {
      return;
    }

    if (source->swr == nullptr) {
      // Lazily create the resampler/upmixer once the backend reports its
      // actual format.
      AVChannelLayout in_layout;
      av_channel_layout_default(&in_layout, static_cast<int>(format.channels));
      AVChannelLayout out_layout;
      av_channel_layout_default(&out_layout, config_.out_channels);
      const int ret = swr_alloc_set_opts2(
          &source->swr, &out_layout, AV_SAMPLE_FMT_FLT, config_.sample_rate,
          &in_layout, AV_SAMPLE_FMT_FLT, format.sample_rate, 0, nullptr);
      av_channel_layout_uninit(&in_layout);
      av_channel_layout_uninit(&out_layout);
      if (ret < 0 || swr_init(source->swr) < 0) {
        failure = "failed to initialize resampler";
      }
      source->in_format = format;
    }

    if (failure.empty()) {
      const int max_out_frames = static_cast<int>(frame_count) * 4 + 256;
      std::vector<float> converted(static_cast<size_t>(max_out_frames) *
                                   config_.out_channels);
      float* out_ptr = converted.data();
      const int out_frames = swr_convert(
          source->swr, reinterpret_cast<uint8_t**>(&out_ptr), max_out_frames,
          reinterpret_cast<const uint8_t**>(&data),
          static_cast<int>(frame_count));
      if (out_frames < 0) {
        // Conversion failure: drop this chunk rather than tear down the
        // session.
      } else {
        source->fifo.insert(
            source->fifo.end(), converted.begin(),
            converted.begin() + static_cast<long>(out_frames) *
                                    config_.out_channels);

        std::string encode_error;
        TryEncodeLocked(&encode_error);
        if (!encode_error.empty()) {
          failure = encode_error;
        }
      }
    }
  }  // mutex_ released before Stop() to avoid self-deadlock.

  if (!failure.empty()) {
    running_.store(false);
    std::string cleanup_error;
    Stop(&cleanup_error);
    EmitState(PipelineState::kError, failure);
  }
}

void EncoderPipeline::TryEncodeLocked(std::string* error) {
  // Frames available for mixing = min across sources that currently hold
  // data; sources without data contribute silence to stay in sync.
  size_t available = SIZE_MAX;
  bool any = false;
  for (const auto& source : sources_) {
    const size_t frames =
        source->fifo.size() / static_cast<size_t>(config_.out_channels);
    if (frames > 0) {
      any = true;
      available = std::min(available, frames);
    }
  }
  if (!any || available == SIZE_MAX) {
    return;
  }
  const size_t channels = static_cast<size_t>(config_.out_channels);
  std::vector<float> mixed(available * channels, 0.0f);
  for (const auto& source : sources_) {
    for (size_t i = 0; i < available * channels; ++i) {
      if (i < source->fifo.size()) {
        mixed[i] += source->fifo[i];
      }
    }
  }
  for (const auto& source : sources_) {
    source->fifo.erase(
        source->fifo.begin(),
        source->fifo.begin() + static_cast<long>(available * channels));
  }
  // Soft-clip the mixed signal.
  for (float& s : mixed) {
    if (s > 1.0f) {
      s = 1.0f;
    } else if (s < -1.0f) {
      s = -1.0f;
    }
  }

  if (!encoder_->Encode(mixed.data(), static_cast<int>(available), error)) {
    return;
  }
  encoded_samples_ += static_cast<int64_t>(available);
  ++stats_emit_counter_;
  if (stats_emit_counter_ >=
      static_cast<int64_t>(config_.sample_rate) / encode_frame_size_) {
    stats_emit_counter_ = 0;
    EmitStats();
  }
}

bool EncoderPipeline::Start(const PipelineConfig& config, std::string* error) {
  // Start/Stop are serialized by the plugin layer; no external lock here so
  // internal cleanup paths may call Stop() freely.
  if (running_.load()) {
    *error = "a session is already running";
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    encoded_samples_ = 0;
    stats_bytes_ = 0;
    stats_bytes_at_last_emit_ = 0;
    stats_emit_counter_ = 0;
    mux_packet_index_ = 0;
    mux_write_failed_ = false;

    EmitState(PipelineState::kStarting, "");

    // Create the codec backend selected by the caller.
    switch (config_.codec) {
      case EncoderKind::kDca:
        encoder_ = std::make_unique<DcaEncoder>();
        break;
      case EncoderKind::kAc3:
      default:
        encoder_ = std::make_unique<Ac3Encoder>();
        break;
    }

    // Encoded packets arrive while mutex_ is held (via TryEncodeLocked or
    // Stop's flush), so this callback must not re-lock.
    encoder_->set_packet_callback([this](const uint8_t* data, size_t size) {
      stats_bytes_ += static_cast<int64_t>(size);
      if (config_.sink == PipelineSink::kStream) {
        if (packet_callback_) {
          packet_callback_(data, size);
        }
        return;
      }
      std::string mux_error;
      const int64_t frame_pts =
          mux_packet_index_ * static_cast<int64_t>(encode_frame_size_);
      if (!muxer_.WritePacket(data, size, frame_pts, &mux_error)) {
        mux_write_failed_ = true;
      }
      ++mux_packet_index_;
    });

    if (!encoder_->Open(config_.out_channels, config_.sample_rate,
                        config_.bitrate, error)) {
      EmitState(PipelineState::kError, *error);
      return false;
    }
    encode_frame_size_ = encoder_->frame_size();

    const AVCodecID codec_id =
        config_.codec == EncoderKind::kDca ? AV_CODEC_ID_DTS : AV_CODEC_ID_AC3;
    if (config_.sink == PipelineSink::kFile) {
      if (!muxer_.Open(config_.file_path, codec_id,
                       config_.sample_rate, config_.out_channels,
                       config_.bitrate, error)) {
        EmitState(PipelineState::kError, *error);
        return false;
      }
    } else if (config_.sink == PipelineSink::kUdp) {
      const std::string url =
          "udp://" + config_.udp_host + ":" + std::to_string(config_.udp_port);
      if (!muxer_.Open(url, codec_id, config_.sample_rate,
                       config_.out_channels, config_.bitrate, error)) {
        EmitState(PipelineState::kError, *error);
        return false;
      }
    }
  }

  // Build capture sources (no lock needed: capture threads do not exist yet).
  auto make_source = [&](bool is_system, const std::string& device_id,
                         std::string* src_error) -> bool {
    SourceSpec spec;
    spec.kind = is_system ? SourceKind::kSystem : SourceKind::kMicrophone;
    spec.device_id = device_id;
    auto capture = CreateCaptureSource(spec, src_error);
    if (capture == nullptr) {
      return false;
    }
    auto source = std::make_unique<Source>();
    source->capture = std::move(capture);
    sources_.push_back(std::move(source));
    return true;
  };

  if (config_.capture_system) {
    if (!make_source(true, config_.system_device_id, error)) {
      std::string cleanup_error;
      Stop(&cleanup_error);
      EmitState(PipelineState::kError, *error);
      return false;
    }
  }
  if (config_.capture_mic) {
    if (!make_source(false, config_.mic_device_id, error)) {
      std::string cleanup_error;
      Stop(&cleanup_error);
      EmitState(PipelineState::kError, *error);
      return false;
    }
  }

  // Mark running before starting capture so chunk callbacks are accepted.
  running_.store(true);
  for (size_t i = 0; i < sources_.size(); ++i) {
    std::string src_error;
    if (!sources_[i]->capture->Start(
            [this, i](const float* data, size_t frames, CaptureFormat format) {
              OnSourceChunk(i, data, frames, format);
            },
            &src_error)) {
      running_.store(false);
      std::string cleanup_error;
      Stop(&cleanup_error);
      *error = src_error;
      EmitState(PipelineState::kError, src_error);
      return false;
    }
  }

  EmitState(PipelineState::kRunning, "");
  return true;
}

bool EncoderPipeline::Stop(std::string* error) {
  // Signal callbacks to return early, then stop and join capture threads
  // before taking the mutex (they may currently hold it).
  running_.store(false);
  for (auto& source : sources_) {
    if (source->capture) {
      source->capture->Stop();
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  bool ok = true;
  std::string flush_error;
  if (encoder_ != nullptr && !encoder_->Flush(&flush_error)) {
    ok = false;
    *error = flush_error;
  }
  {
    std::string mux_error;
    if (!muxer_.Close(&mux_error) && error->empty()) {
      ok = false;
      *error = mux_error;
    }
  }
  for (auto& source : sources_) {
    if (source->swr != nullptr) {
      swr_free(&source->swr);
    }
  }
  sources_.clear();
  if (encoder_ != nullptr) {
    encoder_->Close();
  }
  encoder_.reset();

  EmitState(PipelineState::kStopped, "");
  return ok;
}

}  // namespace surround_encoder
