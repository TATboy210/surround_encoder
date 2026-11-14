// Copyright 2026 The surround_encoder authors. MIT license.

#include "wasapi_capture.h"

#include <initguid.h>  // instantiates KSDATAFORMAT_SUBTYPE_IEEE_FLOAT below
#include <audioclient.h>
#include <combaseapi.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propidl.h>

#include <string>
#include <vector>

namespace surround_encoder {

namespace {

std::string WideToUtf8(const std::wstring& wide) {
  if (wide.empty()) {
    return std::string();
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                       static_cast<int>(wide.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string utf8(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                      utf8.data(), size, nullptr, nullptr);
  return utf8;
}

std::string HrToString(HRESULT hr) {
  char buf[64];
  snprintf(buf, sizeof(buf), "0x%08lx", static_cast<unsigned long>(hr));
  return buf;
}

// COM bootstrap for the capture thread lifetime.
struct ComGuard {
  ComGuard() : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~ComGuard() {
    if (SUCCEEDED(hr_)) {
      CoUninitialize();
    }
  }
  HRESULT hr_;
};

// Selects a device: by id when set, otherwise the platform default.
IMMDevice* ResolveDevice(SourceKind kind, const std::string& device_id,
                         std::string* error) {
  IMMDeviceEnumerator* enumerator = nullptr;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator));
  if (FAILED(hr)) {
    *error = "CoCreateInstance(MMDeviceEnumerator) failed: " + HrToString(hr);
    return nullptr;
  }
  IMMDevice* device = nullptr;
  if (!device_id.empty()) {
    const std::wstring wide_id(device_id.begin(), device_id.end());
    hr = enumerator->GetDevice(wide_id.c_str(), &device);
  } else {
    hr = enumerator->GetDefaultAudioEndpoint(
        kind == SourceKind::kSystem ? eRender : eCapture, eConsole, &device);
  }
  enumerator->Release();
  if (FAILED(hr)) {
    *error = "audio device not found (error " + HrToString(hr) + ")";
    return nullptr;
  }
  return device;
}

}  // namespace

WasapiCapture::~WasapiCapture() { Stop(); }

bool WasapiCapture::Start(AudioChunkCallback callback, std::string* error) {
  if (running_.load()) {
    *error = "capture already running";
    return false;
  }
  callback_ = std::move(callback);

  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (stop_event_ == nullptr) {
    *error = "failed to create stop event";
    return false;
  }

  running_.store(true);
  thread_ = std::thread(&WasapiCapture::ThreadMainStatic, this);
  return true;
}

void WasapiCapture::Stop() {
  if (!running_.exchange(false)) {
    return;
  }
  if (stop_event_ != nullptr) {
    SetEvent(stop_event_);
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  if (stop_event_ != nullptr) {
    CloseHandle(stop_event_);
    stop_event_ = nullptr;
  }
}

DWORD WINAPI WasapiCapture::ThreadMainStatic(LPVOID param) {
  static_cast<WasapiCapture*>(param)->ThreadMain();
  return 0;
}

void WasapiCapture::ThreadMain() {
  ComGuard com;
  if (FAILED(com.hr_)) {
    return;  // COM unavailable on this thread.
  }

  IMMDevice* device = ResolveDevice(spec_.kind, spec_.device_id,
                                    &thread_error_);
  if (device == nullptr) {
    return;
  }

  IAudioClient* audio_client = nullptr;
  HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(&audio_client));
  if (FAILED(hr)) {
    thread_error_ = "IAudioClient activation failed: " + HrToString(hr);
    device->Release();
    return;
  }

  WAVEFORMATEX* mix_format = nullptr;
  hr = audio_client->GetMixFormat(&mix_format);
  if (FAILED(hr) || mix_format == nullptr) {
    thread_error_ = "GetMixFormat failed: " + HrToString(hr);
    audio_client->Release();
    device->Release();
    return;
  }

  // Shared-mode mix format is float32; accept only that (32-bit PCM is
  // never produced in shared mode).
  const bool is_float = mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                        (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                         reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix_format)
                                 ->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
  if (!is_float || mix_format->wBitsPerSample != 32) {
    thread_error_ = "unsupported capture mix format (expected float32)";
    CoTaskMemFree(mix_format);
    audio_client->Release();
    device->Release();
    return;
  }

  const REFERENCE_TIME buffer_duration =
      static_cast<REFERENCE_TIME>(10) * 1000 * 1000;  // 100 ms
  DWORD flags = AUDCLNT_STREAMFLAGS_NOPERSIST;
  if (spec_.kind == SourceKind::kSystem) {
    flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
  }
  hr = audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags,
                                buffer_duration, 0, mix_format, nullptr);
  if (FAILED(hr)) {
    thread_error_ = "audio client init failed: " + HrToString(hr);
    CoTaskMemFree(mix_format);
    audio_client->Release();
    device->Release();
    return;
  }

  IAudioCaptureClient* capture_client = nullptr;
  hr = audio_client->GetService(__uuidof(IAudioCaptureClient),
                                reinterpret_cast<void**>(&capture_client));
  if (FAILED(hr) || capture_client == nullptr) {
    thread_error_ = "IAudioCaptureClient unavailable: " + HrToString(hr);
    CoTaskMemFree(mix_format);
    audio_client->Release();
    device->Release();
    return;
  }

  hr = audio_client->Start();
  if (FAILED(hr)) {
    thread_error_ = "audio client start failed: " + HrToString(hr);
    capture_client->Release();
    CoTaskMemFree(mix_format);
    audio_client->Release();
    device->Release();
    return;
  }

  CaptureFormat format;
  format.sample_rate = mix_format->nSamplesPerSec;
  format.channels = mix_format->nChannels;

  // Polling loop: 10 ms quantum keeps latency low without the loopback
  // event-notification quirk.
  while (running_.load()) {
    const DWORD wait = WaitForSingleObject(stop_event_, 10);
    if (wait == WAIT_OBJECT_0) {
      break;
    }
    UINT32 packet_frames = 0;
    while (SUCCEEDED(capture_client->GetNextPacketSize(&packet_frames)) &&
           packet_frames > 0) {
      BYTE* data = nullptr;
      UINT32 frames = 0;
      DWORD flags_out = 0;
      if (FAILED(capture_client->GetBuffer(&data, &frames, &flags_out,
                                           nullptr, nullptr))) {
        break;
      }
      if (frames > 0 && (flags_out & AUDCLNT_BUFFERFLAGS_SILENT) == 0) {
        callback_(reinterpret_cast<const float*>(data), frames, format);
      }
      capture_client->ReleaseBuffer(frames);
    }
  }

  audio_client->Stop();
  capture_client->Release();
  CoTaskMemFree(mix_format);
  audio_client->Release();
  device->Release();
}

}  // namespace surround_encoder
