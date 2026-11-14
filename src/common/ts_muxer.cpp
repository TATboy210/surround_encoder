// Copyright 2026 The surround_encoder authors. MIT license.

#include "ts_muxer.h"

namespace surround_encoder {

namespace {
const char* AvErrorString(int err) {
  static thread_local char buf[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(err, buf, sizeof(buf));
  return buf;
}

struct NetworkInit {
  NetworkInit() { avformat_network_init(); }
  ~NetworkInit() { avformat_network_deinit(); }
};

NetworkInit& GlobalNetworkInit() {
  static NetworkInit init;
  return init;
}
}  // namespace

TsMuxer::~TsMuxer() {
  std::string ignored;
  Close(&ignored);
}

bool TsMuxer::Open(const std::string& url, AVCodecID codec_id, int sample_rate,
                   int channels, int64_t bit_rate, std::string* error) {
  GlobalNetworkInit();

  int ret = avformat_alloc_output_context2(&format_ctx_, nullptr, "mpegts",
                                           url.c_str());
  if (ret < 0 || format_ctx_ == nullptr) {
    *error = std::string("failed to allocate MPEG-TS context: ") +
             AvErrorString(ret);
    return false;
  }

  stream_ = avformat_new_stream(format_ctx_, nullptr);
  if (stream_ == nullptr) {
    *error = "failed to allocate output stream";
    std::string cleanup_error;
    Close(&cleanup_error);
    return false;
  }

  AVCodecParameters* par = stream_->codecpar;
  par->codec_type = AVMEDIA_TYPE_AUDIO;
  par->codec_id = codec_id;
  par->sample_rate = sample_rate;
  par->bit_rate = bit_rate;
  av_channel_layout_default(&par->ch_layout, channels);
  stream_->time_base = AVRational{1, sample_rate};

  if (!(format_ctx_->oformat->flags & AVFMT_NOFILE)) {
    AVDictionary* opts = nullptr;
    if (url.rfind("udp://", 0) == 0) {
      // Low-latency live streaming: send each packet promptly.
      av_dict_set(&opts, "flush_packets", "1", 0);
      av_dict_set(&opts, "pkt_size", "1316", 0);
    }
    ret = avio_open2(&format_ctx_->pb, url.c_str(), AVIO_FLAG_WRITE, nullptr,
                     &opts);
    av_dict_free(&opts);
    if (ret < 0) {
      *error = std::string("failed to open output '") + url +
               "': " + AvErrorString(ret);
      avformat_free_context(format_ctx_);
      format_ctx_ = nullptr;
      stream_ = nullptr;
      return false;
    }
  }

  ret = avformat_write_header(format_ctx_, nullptr);
  if (ret < 0) {
    *error =
        std::string("failed to write stream header: ") + AvErrorString(ret);
    std::string cleanup_error;
    Close(&cleanup_error);
    return false;
  }

  sample_rate_ = sample_rate;
  packet_index_ = 0;
  return true;
}

bool TsMuxer::WritePacket(const uint8_t* data, size_t size, int64_t pts,
                          std::string* error) {
  if (format_ctx_ == nullptr) {
    *error = "muxer is not open";
    return false;
  }
  AVPacket pkt{};
  av_packet_unref(&pkt);  // not strictly needed for stack packet, but safe
  pkt.data = const_cast<uint8_t*>(data);
  pkt.size = static_cast<int>(size);
  pkt.stream_index = stream_->index;
  pkt.pts = pts;
  pkt.dts = pts;
  pkt.duration = 0;
  pkt.flags |= AV_PKT_FLAG_KEY;
  pkt.pos = -1;

  int ret = av_write_frame(format_ctx_, &pkt);
  if (ret < 0) {
    *error = std::string("failed to write packet: ") + AvErrorString(ret);
    return false;
  }
  ++packet_index_;
  return true;
}

bool TsMuxer::Close(std::string* error) {
  bool ok = true;
  if (format_ctx_ != nullptr) {
    if (av_write_trailer(format_ctx_) < 0) {
      ok = false;
      *error = "failed to write stream trailer";
    }
    if (!(format_ctx_->oformat->flags & AVFMT_NOFILE) &&
        format_ctx_->pb != nullptr) {
      avio_closep(&format_ctx_->pb);
    }
    avformat_free_context(format_ctx_);
    format_ctx_ = nullptr;
    stream_ = nullptr;
  }
  return ok;
}

}  // namespace surround_encoder
