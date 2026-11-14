//
//  Generated file. Do not edit.
//

// clang-format off

#include "generated_plugin_registrant.h"

#include <surround_encoder/surround_encoder_plugin.h>

void fl_register_plugins(FlPluginRegistry* registry) {
  g_autoptr(FlPluginRegistrar) surround_encoder_registrar =
      fl_plugin_registry_get_registrar_for_plugin(registry, "SurroundEncoderPlugin");
  surround_encoder_plugin_register_with_registrar(surround_encoder_registrar);
}
