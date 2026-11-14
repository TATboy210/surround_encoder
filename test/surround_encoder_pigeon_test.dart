import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:surround_encoder/src/messages.g.dart';
import 'package:surround_encoder/src/surround_encoder_impl.dart';
import 'package:surround_encoder/surround_encoder.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('start() maps a FileSink config onto the host config', () async {
    final api = FakeSurroundEncoderPluginApi();
    final impl = SurroundEncoderImpl(hostApi: api);

    await impl.start(const SurroundEncoderConfig(
      captureSystem: true,
      sink: FileSink('C:/temp/out.ts'),
    ));

    expect(api.lastStart, isNotNull);
    expect(api.lastStart!.sinkKind, SinkKind.file);
    expect(api.lastStart!.filePath, 'C:/temp/out.ts');
    expect(api.lastStart!.layout, ChannelLayout.surround51);
    expect(api.lastStart!.bitrate, 448000);
  });

  test('start() maps a UdpSink config onto the host config', () async {
    final api = FakeSurroundEncoderPluginApi();
    final impl = SurroundEncoderImpl(hostApi: api);

    await impl.start(const SurroundEncoderConfig(
      captureSystem: true,
      captureMicrophone: true,
      sink: UdpSink('239.255.0.1', 1234),
    ));

    expect(api.lastStart!.sinkKind, SinkKind.udp);
    expect(api.lastStart!.udpHost, '239.255.0.1');
    expect(api.lastStart!.udpPort, 1234);
  });

  test('start() validates per-codec bitrate ranges', () async {
    final api = FakeSurroundEncoderPluginApi();
    final impl = SurroundEncoderImpl(hostApi: api);

    await expectLater(
      impl.start(const SurroundEncoderConfig(
        codec: CodecKind.dca,
        bitrate: 5120000,
      )),
      throwsA(isA<EncoderException>()),
    );
    await expectLater(
      impl.start(const SurroundEncoderConfig(
        codec: CodecKind.ac3,
        bitrate: 2048000,
      )),
      throwsA(isA<EncoderException>()),
    );
  });

  test('start() rejects configs without any capture source', () async {
    final api = FakeSurroundEncoderPluginApi();
    final impl = SurroundEncoderImpl(hostApi: api);

    await expectLater(
      impl.start(const SurroundEncoderConfig(
        captureSystem: false,
        captureMicrophone: false,
      )),
      throwsA(isA<EncoderException>()),
    );
  });

  test('host errors surface as EncoderException', () async {
    final api = FakeSurroundEncoderPluginApi(startError: 'device not found');
    final impl = SurroundEncoderImpl(hostApi: api);

    await expectLater(
      impl.start(const SurroundEncoderConfig()),
      throwsA(isA<EncoderException>()
          .having((EncoderException e) => e.message, 'message',
              'device not found')),
    );
  });

  test('events receive mapped host events', () async {
    final impl = SurroundEncoderImpl(hostApi: FakeSurroundEncoderPluginApi());

    final Future<EncoderEvent> first = impl.events.first;
    impl.handleEventForTesting(
      EncoderEventData()
        ..state = EncoderState.running
        ..encodedSeconds = 1.5
        ..bitrateKbps = 448,
    );

    final EncoderEvent event = await first;
    expect(event.state, EncoderState.running);
    expect(event.encodedSeconds, 1.5);
    expect(event.bitrateKbps, 448);
  });

  test('packets receive host packet payloads', () async {
    final impl = SurroundEncoderImpl(hostApi: FakeSurroundEncoderPluginApi());

    final Future<Uint8List> first = impl.packets.first;
    impl.handlePacketForTesting(Uint8List.fromList(<int>[1, 2, 3]));

    expect(await first, Uint8List.fromList(<int>[1, 2, 3]));
  });
}

/// Fake implementation of the Pigeon host API that records calls.
class FakeSurroundEncoderPluginApi implements SurroundEncoderPluginApi {
  FakeSurroundEncoderPluginApi({this.startError});

  EncoderConfig? lastStart;
  final String? startError;

  @override
  Future<List<AudioDeviceInfo>> listCaptureDevices() async =>
      <AudioDeviceInfo>[];

  @override
  Future<PlatformCapabilities> getCapabilities() async =>
      PlatformCapabilities()
        ..platformName = 'test'
        ..supportsSystemCapture = true
        ..supportsMicrophone = true;

  @override
  Future<void> start(EncoderConfig config) async {
    if (startError != null) {
      throw PlatformException(code: 'start_failed', message: startError);
    }
    lastStart = config;
  }

  @override
  Future<void> stop() async {}

  @override
  // ignore: non_constant_identifier_names
  BinaryMessenger? get pigeonVar_binaryMessenger => null;

  @override
  // ignore: non_constant_identifier_names
  String get pigeonVar_messageChannelSuffix => '';
}
