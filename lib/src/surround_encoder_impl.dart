import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import '../surround_encoder.dart';
import 'messages.g.dart';

/// Default implementation of [SurroundEncoder] backed by Pigeon channels.
class SurroundEncoderImpl {
  SurroundEncoderImpl({SurroundEncoderPluginApi? hostApi})
      : _hostApi = hostApi ?? SurroundEncoderPluginApi();

  final SurroundEncoderPluginApi _hostApi;

  final StreamController<EncoderEvent> _events =
      StreamController<EncoderEvent>.broadcast();
  final StreamController<Uint8List> _packets =
      StreamController<Uint8List>.broadcast();

  bool _eventApiSetUp = false;

  /// Broadcast stream of lifecycle events and progress statistics.
  Stream<EncoderEvent> get events => _events.stream;

  /// Broadcast stream of raw encoded packets (stream sink sessions only).
  Stream<Uint8List> get packets => _packets.stream;

  void _ensureEventApi() {
    if (_eventApiSetUp) {
      return;
    }
    _eventApiSetUp = true;
    SurroundEncoderEventApi.setUp(_SurroundEncoderEventSink(this));
  }

  Future<EncoderCapabilities> capabilities() async {
    final PlatformCapabilities caps = await _hostApi.getCapabilities();
    return EncoderCapabilities(
      platformName: caps.platformName ?? 'unknown',
      supportsSystemCapture: caps.supportsSystemCapture ?? false,
      supportsMicrophone: caps.supportsMicrophone ?? false,
      notes: caps.notes,
    );
  }

  Future<List<CaptureDevice>> listDevices() async {
    final List<AudioDeviceInfo> devices = await _hostApi.listCaptureDevices();
    return devices
        .where((AudioDeviceInfo d) =>
            d.kind != null && d.id != null && d.name != null)
        .map((AudioDeviceInfo d) => CaptureDevice(
              kind: d.kind!,
              id: d.id!,
              name: d.name!,
              isDefault: d.isDefault ?? false,
            ))
        .toList(growable: false);
  }

  Future<void> start(SurroundEncoderConfig config) async {
    _ensureEventApi();
    if (!config.captureSystem && !config.captureMicrophone) {
      throw const EncoderException(
          'At least one capture source must be enabled');
    }
    final SinkConfig sink = config.sink;
    final EncoderConfig host = EncoderConfig()
      ..captureSystem = config.captureSystem
      ..captureMicrophone = config.captureMicrophone
      ..systemDeviceId = config.systemDeviceId
      ..microphoneDeviceId = config.microphoneDeviceId
      ..layout = config.layout
      ..codec = config.codec
      ..sampleRate = config.sampleRate
      ..bitrate = config.bitrate;
    switch (sink) {
      case FileSink(:final String path):
        host
          ..sinkKind = SinkKind.file
          ..filePath = path;
      case UdpSink(host: final String udpHost, port: final int udpPort):
        host
          ..sinkKind = SinkKind.udp
          ..udpHost = udpHost
          ..udpPort = udpPort;
      case StreamSink():
        host.sinkKind = SinkKind.stream;
    }
    try {
      await _hostApi.start(host);
    } on PlatformException catch (e) {
      throw EncoderException(e.message ?? e.code);
    }
  }

  Future<void> stop() async {
    try {
      await _hostApi.stop();
    } on PlatformException catch (e) {
      throw EncoderException(e.message ?? e.code);
    }
  }

  void _handleEvent(EncoderEventData event) {
    _events.add(EncoderEvent(
      state: event.state ?? EncoderState.idle,
      message: event.message,
      encodedSeconds: event.encodedSeconds,
      bitrateKbps: event.bitrateKbps,
    ));
  }

  void _handlePacket(Uint8List packet) {
    _packets.add(packet);
  }

  /// Delivers a synthetic host event; visible for tests.
  @visibleForTesting
  void handleEventForTesting(EncoderEventData event) => _handleEvent(event);

  /// Delivers a synthetic encoded packet; visible for tests.
  @visibleForTesting
  void handlePacketForTesting(Uint8List packet) => _handlePacket(packet);
}

/// Bridges Pigeon callbacks into [SurroundEncoderImpl] streams.
class _SurroundEncoderEventSink extends SurroundEncoderEventApi {
  _SurroundEncoderEventSink(this._impl);

  final SurroundEncoderImpl _impl;

  @override
  void onEncoderEvent(EncoderEventData event) => _impl._handleEvent(event);

  @override
  void onEncodedPacket(Uint8List packet) => _impl._handlePacket(packet);
}
