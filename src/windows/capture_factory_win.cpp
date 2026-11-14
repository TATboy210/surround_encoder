// Copyright 2026 The surround_encoder authors. MIT license.
//
// Windows implementation of the capture-device factory declared in
// common/capture_source.h.

#include <initguid.h>  // instantiates the PROPERTYKEY GUIDs below
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <audioclient.h>
#include <combaseapi.h>
#include <propidl.h>

#include <memory>
#include <string>
#include <vector>

#include "../common/capture_source.h"
#include "wasapi_capture.h"

namespace surround_encoder {

namespace {

// COM bootstrap for device enumeration on this thread.
struct ComGuard {
  ComGuard() : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~ComGuard() {
    if (SUCCEEDED(hr_)) {
      CoUninitialize();
    }
  }
  HRESULT hr_;
};

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

// Enumerates render (system) or capture (mic) endpoints on the default
// console role. Device ids are WASAPI endpoint id strings.
void EnumerateEndpoints(EDataFlow flow, SourceKind kind,
                        std::vector<DeviceInfo>* out) {
  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator),
                              reinterpret_cast<void**>(&enumerator)))) {
    return;
  }
  IMMDeviceCollection* collection = nullptr;
  if (SUCCEEDED(enumerator->EnumAudioEndpoints(
          flow, DEVICE_STATE_ACTIVE, &collection)) && collection != nullptr) {
    UINT count = 0;
    collection->GetCount(&count);
    IMMDevice* default_device = nullptr;
    enumerator->GetDefaultAudioEndpoint(flow, eConsole, &default_device);
    LPWSTR default_id = nullptr;
    if (default_device != nullptr) {
      default_device->GetId(&default_id);
    }
    for (UINT i = 0; i < count; ++i) {
      IMMDevice* device = nullptr;
      if (FAILED(collection->Item(i, &device)) || device == nullptr) {
        continue;
      }
      LPWSTR id = nullptr;
      IPropertyStore* store = nullptr;
      if (SUCCEEDED(device->GetId(&id)) &&
          SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store))) {
        PROPVARIANT name;
        PropVariantInit(&name);
        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &name))) {
          DeviceInfo info;
          info.kind = kind;
          info.id = WideToUtf8(id);
          info.name = WideToUtf8(name.pwszVal);
          info.is_default =
              default_id != nullptr && wcscmp(id, default_id) == 0;
          out->push_back(std::move(info));
          PropVariantClear(&name);
        }
        store->Release();
      }
      if (id != nullptr) {
        CoTaskMemFree(id);
      }
      device->Release();
    }
    if (default_id != nullptr) {
      CoTaskMemFree(default_id);
    }
    if (default_device != nullptr) {
      default_device->Release();
    }
    collection->Release();
  }
  enumerator->Release();
}

}  // namespace

std::vector<DeviceInfo> ListCaptureDevices() {
  std::vector<DeviceInfo> devices;
  ComGuard com;  // best-effort: plugin calls arrive on COM-initialized threads
  if (SUCCEEDED(com.hr_)) {
    EnumerateEndpoints(eRender, SourceKind::kSystem, &devices);
    EnumerateEndpoints(eCapture, SourceKind::kMicrophone, &devices);
  }
  return devices;
}

std::unique_ptr<ICaptureSource> CreateCaptureSource(const SourceSpec& spec,
                                                    std::string* error) {
  return std::make_unique<WasapiCapture>(spec);
}

const char* PlatformName() { return "Windows"; }

}  // namespace surround_encoder
