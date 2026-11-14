#include <gtest/gtest.h>

#include "messages.g.h"

// Unit tests for the Windows plugin shell. The encoder pipeline behavior is
// exercised by the Dart-level tests; this file covers pieces of the
// generated Pigeon codec layer that are cheap to check natively.

namespace surround_encoder {
namespace test {

TEST(SurroundEncoderMessages, EnumValuesMatchDartContract) {
  // These values cross the wire as integers; changing them breaks the Dart
  // side. Pin them here.
  EXPECT_EQ(static_cast<int>(CaptureSourceKind::kSystem), 0);
  EXPECT_EQ(static_cast<int>(CaptureSourceKind::kMicrophone), 1);
  EXPECT_EQ(static_cast<int>(ChannelLayout::kStereo), 0);
  EXPECT_EQ(static_cast<int>(ChannelLayout::kSurround51), 1);
  EXPECT_EQ(static_cast<int>(SinkKind::kFile), 0);
  EXPECT_EQ(static_cast<int>(SinkKind::kUdp), 1);
  EXPECT_EQ(int(EncoderState::kIdle), 0);
  EXPECT_EQ(static_cast<int>(EncoderState::kError), 4);
  EXPECT_EQ(static_cast<int>(CodecKind::kAc3), 0);
  EXPECT_EQ(static_cast<int>(CodecKind::kDca), 1);
}

}  // namespace test
}  // namespace surround_encoder
