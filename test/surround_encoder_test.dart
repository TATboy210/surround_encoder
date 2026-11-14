import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:surround_encoder/surround_encoder.dart';

void main() {
  test('config rejects non-48kHz sample rates', () {
    expect(
      () => SurroundEncoderConfig(sampleRate: 44100),
      throwsA(isA<AssertionError>()),
    );
  });

  test('config accepts dca codec with high bitrate (validated at start())',
      () {
    // Construction no longer hard-codes the AC-3 range; start() validates
    // per codec (covered by surround_encoder_pigeon_test.dart).
    const SurroundEncoderConfig(
      codec: CodecKind.dca,
      bitrate: 1509000,
    );
  });

  test('events is broadcast-capable', () {
    final Stream<EncoderEvent> events = SurroundEncoder.events;
    expect(events.isBroadcast, isTrue);
  });

  test('packets is broadcast-capable', () {
    final Stream<Uint8List> packets = SurroundEncoder.packets;
    expect(packets.isBroadcast, isTrue);
  });
}
