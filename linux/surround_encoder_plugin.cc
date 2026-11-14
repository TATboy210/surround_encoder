#include "include/surround_encoder/surround_encoder_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>

#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "../src/common/capture_source.h"
#include "../src/common/encoder_pipeline.h"
#include "messages.g.h"

#define SURROUND_ENCODER_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), surround_encoder_plugin_get_type(), \
                              SurroundEncoderPlugin))

struct _SurroundEncoderPlugin {
  GObject parent_instance;
  SurroundEncoderSurroundEncoderEventApi* event_api;
};

G_DEFINE_TYPE(SurroundEncoderPlugin, surround_encoder_plugin,
              g_object_get_type())

namespace {

const gboolean kTrue = TRUE;

surround_encoder::EncoderPipeline* PluginPipeline() {
  static surround_encoder::EncoderPipeline pipeline;
  return &pipeline;
}

std::mutex* PluginMutex() {
  static std::mutex mutex;
  return &mutex;
}

// Marshals pipeline events onto the platform thread, where the
// FlBinaryMessenger must be used.
gboolean IdleSendEvent(gpointer data) {
  auto* payload = static_cast<
      std::pair<SurroundEncoderEncoderEventData*, SurroundEncoderPlugin*>*>(
      data);
  SurroundEncoderEncoderEventData* event = payload->first;
  SurroundEncoderEncoderEventData* event_ref = event;
  surround_encoder_surround_encoder_event_api_on_encoder_event(
      payload->second->event_api, event_ref, nullptr, nullptr, nullptr);
  g_object_unref(event);
  delete payload;
  return G_SOURCE_REMOVE;
}

void DispatchEvent(SurroundEncoderPlugin* self,
                   SurroundEncoderEncoderEventData* event_data) {
  auto* payload = new std::pair<SurroundEncoderEncoderEventData*,
                                SurroundEncoderPlugin*>(event_data, self);
  g_idle_add_once(IdleSendEvent, payload);
}

gboolean IdleSendPacket(gpointer data) {
  auto* payload = static_cast<
      std::pair<std::vector<uint8_t>*, SurroundEncoderPlugin*>*>(data);
  surround_encoder_surround_encoder_event_api_on_encoded_packet(
      payload->second->event_api, payload->first->data(),
      payload->first->size(), nullptr, nullptr, nullptr);
  delete payload->first;
  delete payload;
  return G_SOURCE_REMOVE;
}

void DispatchPacket(SurroundEncoderPlugin* self, const uint8_t* data,
                    size_t size) {
  auto* copy = new std::vector<uint8_t>(data, data + size);
  auto* payload =
      new std::pair<std::vector<uint8_t>*, SurroundEncoderPlugin*>(copy, self);
  g_idle_add_once(IdleSendPacket, payload);
}

void OnPipelineEvent(SurroundEncoderPlugin* self,
                     const surround_encoder::PipelineEvent& event) {
  SurroundEncoderEncoderState state;
  switch (event.state) {
    case surround_encoder::PipelineState::kIdle:
      state = SURROUND_ENCODER_ENCODER_STATE_IDLE;
      break;
    case surround_encoder::PipelineState::kStarting:
      state = SURROUND_ENCODER_ENCODER_STATE_STARTING;
      break;
    case surround_encoder::PipelineState::kRunning:
      state = SURROUND_ENCODER_ENCODER_STATE_RUNNING;
      break;
    case surround_encoder::PipelineState::kStopped:
      state = SURROUND_ENCODER_ENCODER_STATE_STOPPED;
      break;
    case surround_encoder::PipelineState::kError:
    default:
      state = SURROUND_ENCODER_ENCODER_STATE_ERROR;
      break;
  }
  const double encoded_seconds = event.encoded_seconds;
  const double bitrate_kbps = event.bitrate_kbps;
  SurroundEncoderEncoderEventData* data =
      surround_encoder_encoder_event_data_new(
          &state,
          event.message.empty() ? nullptr : event.message.c_str(),
          &encoded_seconds, &bitrate_kbps);
  DispatchEvent(self, data);
}

void handle_list_capture_devices(
    SurroundEncoderSurroundEncoderPluginApiResponseHandle* response_handle,
    gpointer user_data) {
  (void)user_data;
  g_autoptr(FlValue) list = fl_value_new_list();
  const std::vector<surround_encoder::DeviceInfo> devices =
      surround_encoder::ListCaptureDevices();
  for (const surround_encoder::DeviceInfo& info : devices) {
    g_autoptr(FlValue) device = fl_value_new_list();
    fl_value_append_take(
        device,
        fl_value_new_int(
            info.kind == surround_encoder::SourceKind::kSystem
                ? SURROUND_ENCODER_CAPTURE_SOURCE_KIND_SYSTEM
                : SURROUND_ENCODER_CAPTURE_SOURCE_KIND_MICROPHONE));
    fl_value_append_take(device, fl_value_new_string(info.id.c_str()));
    fl_value_append_take(device, fl_value_new_string(info.name.c_str()));
    fl_value_append_take(device,
                         fl_value_new_bool(info.is_default ? TRUE : FALSE));
    fl_value_append_take(list, fl_value_ref(device));
  }
  surround_encoder_surround_encoder_plugin_api_respond_list_capture_devices(
      response_handle, list);
}

void handle_get_capabilities(
    SurroundEncoderSurroundEncoderPluginApiResponseHandle* response_handle,
    gpointer user_data) {
  (void)user_data;
  const gchar* notes =
      "System audio uses the default sink monitor source; microphone uses "
      "the default PulseAudio/PipeWire source.";
  SurroundEncoderPlatformCapabilities* caps =
      surround_encoder_platform_capabilities_new(
          const_cast<gchar*>("Linux"), const_cast<gboolean*>(&kTrue),
          const_cast<gboolean*>(&kTrue), const_cast<gchar*>(notes));
  surround_encoder_surround_encoder_plugin_api_respond_get_capabilities(
      response_handle, caps);
  g_object_unref(caps);
}

void handle_start(SurroundEncoderEncoderConfig* config,
                  SurroundEncoderSurroundEncoderPluginApiResponseHandle*
                      response_handle,
                  gpointer user_data) {
  auto* self = SURROUND_ENCODER_PLUGIN(user_data);
  std::lock_guard<std::mutex> lock(*PluginMutex());

  if (PluginPipeline()->is_running()) {
    surround_encoder_surround_encoder_plugin_api_respond_error_start(
        response_handle, "start_failed", "a session is already running",
        nullptr);
    return;
  }

  surround_encoder::PipelineConfig pipeline;
  const gboolean* capture_system =
      surround_encoder_encoder_config_get_capture_system(config);
  if (capture_system != nullptr) {
    pipeline.capture_system = *capture_system == TRUE;
  }
  const gboolean* capture_mic =
      surround_encoder_encoder_config_get_capture_microphone(config);
  if (capture_mic != nullptr) {
    pipeline.capture_mic = *capture_mic == TRUE;
  }
  const gchar* system_device =
      surround_encoder_encoder_config_get_system_device_id(config);
  if (system_device != nullptr) {
    pipeline.system_device_id = system_device;
  }
  const gchar* mic_device =
      surround_encoder_encoder_config_get_microphone_device_id(config);
  if (mic_device != nullptr) {
    pipeline.mic_device_id = mic_device;
  }
  const SurroundEncoderChannelLayout* layout =
      surround_encoder_encoder_config_get_layout(config);
  pipeline.out_channels =
      layout != nullptr && *layout == SURROUND_ENCODER_CHANNEL_LAYOUT_SURROUND51
          ? 6
          : 2;
  const int64_t* sample_rate =
      surround_encoder_encoder_config_get_sample_rate(config);
  pipeline.sample_rate =
      sample_rate != nullptr ? static_cast<int>(*sample_rate) : 48000;
  const int64_t* bitrate = surround_encoder_encoder_config_get_bitrate(config);
  pipeline.bitrate = bitrate != nullptr ? *bitrate : 448000;
  const SurroundEncoderCodecKind* codec_kind =
      surround_encoder_encoder_config_get_codec(config);
  pipeline.codec =
      codec_kind != nullptr &&
              *codec_kind == SURROUND_ENCODER_CODEC_KIND_DCA
          ? surround_encoder::EncoderKind::kDca
          : surround_encoder::EncoderKind::kAc3;

  surround_encoder::PipelineSink sink =
      surround_encoder::PipelineSink::kStream;
  const SurroundEncoderSinkKind* sink_kind =
      surround_encoder_encoder_config_get_sink_kind(config);
  if (sink_kind != nullptr) {
    if (*sink_kind == SURROUND_ENCODER_SINK_KIND_FILE) {
      sink = surround_encoder::PipelineSink::kFile;
      const gchar* path =
          surround_encoder_encoder_config_get_file_path(config);
      if (path == nullptr) {
        surround_encoder_surround_encoder_plugin_api_respond_error_start(
            response_handle, "start_failed", "file sink requires a path",
            nullptr);
        return;
      }
      pipeline.file_path = path;
    } else if (*sink_kind == SURROUND_ENCODER_SINK_KIND_UDP) {
      sink = surround_encoder::PipelineSink::kUdp;
      const gchar* host = surround_encoder_encoder_config_get_udp_host(config);
      const int64_t* port =
          surround_encoder_encoder_config_get_udp_port(config);
      if (host == nullptr || port == nullptr) {
        surround_encoder_surround_encoder_plugin_api_respond_error_start(
            response_handle, "start_failed",
            "udp sink requires host and port", nullptr);
        return;
      }
      pipeline.udp_host = host;
      pipeline.udp_port = static_cast<int>(*port);
    }
  }
  pipeline.sink = sink;

  if (!pipeline.capture_system && !pipeline.capture_mic) {
    surround_encoder_surround_encoder_plugin_api_respond_error_start(
        response_handle, "start_failed",
        "at least one capture source must be enabled", nullptr);
    return;
  }

  PluginPipeline()->set_event_callback(
      [self](const surround_encoder::PipelineEvent& event) {
        OnPipelineEvent(self, event);
      });
  PluginPipeline()->set_packet_callback(
      [self](const uint8_t* data, size_t size) {
        DispatchPacket(self, data, size);
      });

  std::string error;
  if (!PluginPipeline()->Start(pipeline, &error)) {
    surround_encoder_surround_encoder_plugin_api_respond_error_start(
        response_handle, "start_failed", error.c_str(), nullptr);
    return;
  }
  surround_encoder_surround_encoder_plugin_api_respond_start(response_handle);
}

void handle_stop(
    SurroundEncoderSurroundEncoderPluginApiResponseHandle* response_handle,
    gpointer user_data) {
  auto* self = SURROUND_ENCODER_PLUGIN(user_data);
  std::lock_guard<std::mutex> lock(*PluginMutex());
  std::string error;
  if (!PluginPipeline()->Stop(&error) && !error.empty()) {
    surround_encoder_surround_encoder_plugin_api_respond_error_stop(
        response_handle, "stop_failed", error.c_str(), nullptr);
    return;
  }
  surround_encoder_surround_encoder_plugin_api_respond_stop(response_handle);
}

}  // namespace

static void surround_encoder_plugin_dispose(GObject* object) {
  auto* self = SURROUND_ENCODER_PLUGIN(object);
  {
    std::lock_guard<std::mutex> lock(*PluginMutex());
    std::string ignored;
    PluginPipeline()->Stop(&ignored);
  }
  g_clear_object(&self->event_api);
  G_OBJECT_CLASS(surround_encoder_plugin_parent_class)->dispose(object);
}

static void surround_encoder_plugin_class_init(SurroundEncoderPluginClass*
                                                   klass) {
  G_OBJECT_CLASS(klass)->dispose = surround_encoder_plugin_dispose;
}

static void surround_encoder_plugin_init(SurroundEncoderPlugin* self) {}

void surround_encoder_plugin_register_with_registrar(
    FlPluginRegistrar* registrar) {
  SurroundEncoderPlugin* plugin = SURROUND_ENCODER_PLUGIN(
      g_object_new(surround_encoder_plugin_get_type(), nullptr));

  plugin->event_api = surround_encoder_surround_encoder_event_api_new(
      fl_plugin_registrar_get_messenger(registrar), nullptr);

  static SurroundEncoderSurroundEncoderPluginApiVTable api_vtable = {
      .list_capture_devices = handle_list_capture_devices,
      .get_capabilities = handle_get_capabilities,
      .start = handle_start,
      .stop = handle_stop,
  };
  surround_encoder_surround_encoder_plugin_api_set_method_handlers(
      fl_plugin_registrar_get_messenger(registrar), nullptr, &api_vtable,
      g_object_ref(plugin), g_object_unref);

  g_object_unref(plugin);
}
