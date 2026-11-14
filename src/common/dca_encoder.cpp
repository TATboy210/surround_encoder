// Copyright 2026 The surround_encoder authors. MIT license.

#include "dca_encoder.h"

#include <cmath>
#include <cstring>

namespace surround_encoder {

DcaEncoder::~DcaEncoder() {
  std::string ignored;
  Flush(&ignored);
  Close();
}

bool DcaEncoder::Open(int channels, int sample_rate, int64_t bitrate,
                      std::string* error) {
  int channel_config;
  int flags = 0;
  if (channels == 2) {
    channel_config = DCAENC_CHANNELS_STEREO;
  } else if (channels == 6) {
    channel_config = DCAENC_CHANNELS_3FRONT_2REAR;
    flags = DCAENC_FLAG_LFE;
  } else {
    *error = "DCA encoding supports stereo or 5.1 only";
    return false;
  }

  // dcaenc expects S32 interleaved samples; a small slack allows the
  // conversion to exceed int32 range slightly before we clamp ourselves.
  context_ = dcaenc_create(sample_rate, channel_config,
                           static_cast<int>(bitrate), flags);
  if (context_ == nullptr) {
    *error = "failed to create the DCA encoder (unsupported bitrate? "
             "DCA core accepts 64-1509 kbps)";
    return false;
  }

  channels_ = channels;
  frame_size_ = dcaenc_input_size(context_);  // 512
  return true;
}

void DcaEncoder::Close() {
  if (context_ != nullptr) {
    // The final frame was flushed by Flush(); passing a null buffer here is
    // safe because nothing remains after destroy.
    uint8_t dummy[16384];
    dcaenc_destroy(context_, dummy);
    context_ = nullptr;
  }
  pending_.clear();
}

bool DcaEncoder::Encode(const float* interleaved, int frame_count,
                        std::string* error) {
  if (context_ == nullptr) {
    *error = "encoder is not open";
    return false;
  }
  const size_t samples = static_cast<size_t>(frame_count) * channels_;
  pending_.insert(pending_.end(), interleaved, interleaved + samples);

  const size_t block = static_cast<size_t>(frame_size_) * channels_;
  std::vector<int32_t> pcm(frame_size_ * channels_);
  std::vector<uint8_t> out(16384);
  while (pending_.size() >= block) {
    const float* src = pending_.data();
    for (size_t i = 0; i < frame_size_ * static_cast<size_t>(channels_); ++i) {
      float s = src[i];
      if (s > 1.0f) {
        s = 1.0f;
      } else if (s < -1.0f) {
        s = -1.0f;
      }
      // Round-to-nearest into full-scale int32.
      double v = static_cast<double>(s) * 2147483647.0;
      v = v >= 0.0 ? std::floor(v + 0.5) : std::ceil(v - 0.5);
      pcm[i] = static_cast<int32_t>(v);
    }
    const int wrote = dcaenc_convert_s32(context_, pcm.data(), out.data());
    if (wrote <= 0) {
      *error = "DCA encoder rejected a frame";
      return false;
    }
    if (packet_callback_) {
      packet_callback_(out.data(), static_cast<size_t>(wrote));
    }
    pending_.erase(pending_.begin(),
                   pending_.begin() + static_cast<long>(block));
  }
  return true;
}

bool DcaEncoder::Flush(std::string* error) {
  if (context_ == nullptr) {
    return true;
  }
  // Zero-pad the buffered tail into a full frame and encode it, mirroring
  // Ac3Encoder's internal padding. dcaenc_destroy() would encode an
  // all-zero extra frame, so we only use it to release the context.
  const size_t block = static_cast<size_t>(frame_size_) * channels_;
  if (!pending_.empty()) {
    pending_.resize(block, 0.0f);
    std::vector<int32_t> pcm(frame_size_ * channels_);
    const float* src = pending_.data();
    for (size_t i = 0; i < block; ++i) {
      float s = src[i];
      if (s > 1.0f) {
        s = 1.0f;
      } else if (s < -1.0f) {
        s = -1.0f;
      }
      double v = static_cast<double>(s) * 2147483647.0;
      v = v >= 0.0 ? std::floor(v + 0.5) : std::ceil(v - 0.5);
      pcm[i] = static_cast<int32_t>(v);
    }
    std::vector<uint8_t> out(16384);
    const int wrote = dcaenc_convert_s32(context_, pcm.data(), out.data());
    if (wrote > 0 && packet_callback_) {
      packet_callback_(out.data(), static_cast<size_t>(wrote));
    }
    pending_.clear();
  }
  dcaenc_destroy(context_, nullptr);
  context_ = nullptr;
  return true;
}

}  // namespace surround_encoder
