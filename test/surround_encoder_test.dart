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

  test('config rejects out-of-range bitrates', () {
    expect(
      () => SurroundEncoderConfig(bitrate: 640000),
      throwsA(isA<AssertionError>()),
    );
  });

  test('config rejects unimplemented codecs', () {
    expect(
      () => SurroundEncoderConfig(codec: CodecKind.dca),
      throwsA(isA<AssertionError>()),
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
