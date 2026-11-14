#include "include/surround_encoder/surround_encoder_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "surround_encoder_plugin.h"

void SurroundEncoderPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  surround_encoder::SurroundEncoderPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
