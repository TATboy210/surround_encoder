// Copyright 2026 The surround_encoder authors. MIT license.

#include "ac3_encoder.h"

#include <cstring>

namespace surround_encoder {

namespace {
const char* AvErrorString(int err) {
  static thread_local char buf[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(err, buf, sizeof(buf));
  return buf;
}
}  // namespace

Ac3Encoder::~Ac3Encoder() { Close(); }

bool Ac3Encoder::Open(int channels, int sample_rate, int64_t bitrate,
                      std::string* error) {
  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AC3);
  if (codec == nullptr) {
    *error = "FFmpeg build does not contain the AC-3 encoder";
    return false;
  }

  codec_ctx_ = avcodec_alloc_context3(codec);
  if (codec_ctx_ == nullptr) {
    *error = "failed to allocate codec context";
    return false;
  }

  codec_ctx_->bit_rate = bitrate;
  codec_ctx_->sample_rate = sample_rate;
  codec_ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;  // AC-3 encoder is planar float
  codec_ctx_->time_base = AVRational{1, sample_rate};
  if (channels == 2) {
    av_channel_layout_default(&codec_ctx_->ch_layout, 2);
  } else {
    AVChannelLayout layout = AV_CHANNEL_LAYOUT_5POINT1;
    av_channel_layout_copy(&codec_ctx_->ch_layout, &layout);
  }

  if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
    *error = "failed to open the AC-3 encoder (bitrate/channels unsupported?)";
    Close();
    return false;
  }

  frame_size_ = codec_ctx_->frame_size;  // 1536 for AC-3
  channels_ = channels;

  frame_ = av_frame_alloc();
  frame_->format = AV_SAMPLE_FMT_FLTP;  // AC-3 encoder is planar float only
  frame_->sample_rate = sample_rate;
  frame_->nb_samples = frame_size_;
  frame_->pts = 0;
  av_channel_layout_copy(&frame_->ch_layout, &codec_ctx_->ch_layout);
  if (av_frame_get_buffer(frame_, 0) < 0) {
    *error = "failed to allocate encoder frame";
    Close();
    return false;
  }

  packet_ = av_packet_alloc();
  if (packet_ == nullptr) {
    *error = "failed to allocate packet";
    Close();
    return false;
  }
  return true;
}

void Ac3Encoder::Close() {
  if (packet_ != nullptr) {
    av_packet_free(&packet_);
  }
  if (frame_ != nullptr) {
    av_frame_free(&frame_);
  }
  if (codec_ctx_ != nullptr) {
    avcodec_free_context(&codec_ctx_);
  }
  pending_.clear();
  next_pts_ = 0;
}

bool Ac3Encoder::EncodeFrame(AVFrame* frame, std::string* error) {
  int ret = avcodec_send_frame(codec_ctx_, frame);
  while (ret >= 0) {
    ret = avcodec_receive_packet(codec_ctx_, packet_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      return true;
    }
    if (ret < 0) {
      *error = std::string("encoder error: ") + AvErrorString(ret);
      return false;
    }
    if (packet_callback_) {
      packet_callback_(packet_->data, static_cast<size_t>(packet_->size));
    }
    av_packet_unref(packet_);
  }
  return true;
}

bool Ac3Encoder::Encode(const float* interleaved, int frame_count,
                        std::string* error) {
  if (codec_ctx_ == nullptr) {
    *error = "encoder is not open";
    return false;
  }
  const size_t samples = static_cast<size_t>(frame_count) * channels_;
  pending_.insert(pending_.end(), interleaved, interleaved + samples);

  const size_t block = static_cast<size_t>(frame_size_) * channels_;
  while (pending_.size() >= block) {
    // Deinterleave pending block into the planar frame buffers.
    const float* src = pending_.data();
    for (int ch = 0; ch < channels_; ++ch) {
      float* dst = reinterpret_cast<float*>(frame_->data[ch]);
      for (int i = 0; i < frame_size_; ++i) {
        dst[i] = src[static_cast<size_t>(i) * channels_ + ch];
      }
    }
    frame_->pts = next_pts_;
    next_pts_ += frame_size_;
    if (!EncodeFrame(frame_, error)) {
      return false;
    }
    pending_.erase(pending_.begin(),
                   pending_.begin() + static_cast<long>(block));
  }
  return true;
}

bool Ac3Encoder::Flush(std::string* error) {
  if (codec_ctx_ == nullptr || pending_.empty()) {
    return true;
  }
  // Zero-pad the tail into a full frame so no samples are lost.
  const size_t block = static_cast<size_t>(frame_size_) * channels_;
  pending_.resize(block, 0.0f);
  const float* src = pending_.data();
  for (int ch = 0; ch < channels_; ++ch) {
    float* dst = reinterpret_cast<float*>(frame_->data[ch]);
    for (int i = 0; i < frame_size_; ++i) {
      dst[i] = src[static_cast<size_t>(i) * channels_ + ch];
    }
  }
  frame_->pts = next_pts_;
  next_pts_ += frame_size_;
  pending_.clear();
  if (!EncodeFrame(frame_, error)) {
    return false;
  }
  // Drain the encoder.
  int ret = avcodec_send_frame(codec_ctx_, nullptr);
  while (ret >= 0) {
    ret = avcodec_receive_packet(codec_ctx_, packet_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;
    }
    if (ret < 0) {
      *error = std::string("encoder flush error: ") + AvErrorString(ret);
      return false;
    }
    if (packet_callback_) {
      packet_callback_(packet_->data, static_cast<size_t>(packet_->size));
    }
    av_packet_unref(packet_);
  }
  return true;
}

}  // namespace surround_encoder
