#include <flutter_linux/flutter_linux.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "include/surround_encoder/surround_encoder_plugin.h"
#include "surround_encoder_plugin_private.h"

// Unit tests for the Linux plugin shell. Once you have built the plugin's
// example app, you can run these tests from the command line. For instance,
// for an x64 debug build, run
// $ build/linux/x64/debug/plugins/surround_encoder/surround_encoder_test

TEST(SurroundEncoderPlugin, PluginTypeIsRegistered) {
  EXPECT_NE(surround_encoder_plugin_get_type(), 0);
}
