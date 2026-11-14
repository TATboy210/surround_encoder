/// Real-time surround-sound encoder plugin.
///
/// Captures system playback audio and/or microphone input and encodes it to a
/// surround stream (AC-3 in this release; DCA planned) using FFmpeg's open
/// implementations. Output goes to a file, a UDP stream, or straight to Dart.
///
/// This package is an independent open implementation and is not affiliated
/// with, endorsed by, or certified by Dolby Laboratories, DTS Inc., or any
/// other vendor. Trademarks belong to their respective owners.
library;

import 'dart:async';
import 'dart:typed_data';

import 'src/messages.g.dart';
import 'src/surround_encoder_impl.dart';

export 'src/messages.g.dart'
    show CaptureSourceKind, ChannelLayout, CodecKind, EncoderState, SinkKind;

/// How encoded packets are delivered.
sealed class SinkConfig {
  const SinkConfig();
}

/// Write the elementary stream (wrapped in MPEG-TS) to a file.
class FileSink extends SinkConfig {
  const FileSink(this.path);

  /// Destination file path. The container is MPEG-TS; use a `.ts` extension
  /// for a directly playable file.
  final String path;
}

/// Stream the elementary stream (wrapped in MPEG-TS) over UDP.
class UdpSink extends SinkConfig {
  const UdpSink(this.host, this.port);

  /// Destination host, e.g. `239.255.0.1` for LAN multicast.
  final String host;

  /// Destination port, e.g. 1234 — playable with `ffplay udp://@:1234`.
  final int port;
}

/// Deliver raw encoded packets to Dart via [SurroundEncoder.packets].
class StreamSink extends SinkConfig {
  const StreamSink();
}

/// Configuration for an encoding session.
class SurroundEncoderConfig {
  const SurroundEncoderConfig({
    this.captureSystem = true,
    this.captureMicrophone = false,
    this.systemDeviceId,
    this.microphoneDeviceId,
    this.layout = ChannelLayout.surround51,
    this.codec = CodecKind.ac3,
    this.sampleRate = 48000,
    this.bitrate = 448000,
    this.sink = const StreamSink(),
  }) : assert(sampleRate == 48000,
            'AC-3/DCA encoding requires a 48 kHz sample rate');

  /// Capture the system's playback audio (loopback/monitor).
  final bool captureSystem;

  /// Capture a microphone.
  final bool captureMicrophone;

  /// Device id for system capture; null uses the platform default.
  final String? systemDeviceId;

  /// Device id for microphone capture; null uses the platform default.
  final String? microphoneDeviceId;

  /// Output channel layout. Stereo sources are upmixed to 5.1.
  final ChannelLayout layout;

  /// Codec selection; only [CodecKind.ac3] is available in this release.
  final CodecKind codec;

  /// Output sample rate; AC-3 requires 48 kHz.
  final int sampleRate;

  /// Output bitrate in bits per second (e.g. 448000 for 5.1 AC-3).
  final int bitrate;

  /// Where encoded packets go.
  final SinkConfig sink;
}

/// Feature availability of the current platform build.
class EncoderCapabilities {
  const EncoderCapabilities({
    required this.platformName,
    required this.supportsSystemCapture,
    required this.supportsMicrophone,
    this.notes,
  });

  final String platformName;
  final bool supportsSystemCapture;
  final bool supportsMicrophone;
  final String? notes;

  @override
  String toString() =>
      'EncoderCapabilities($platformName, system: $supportsSystemCapture, '
      'mic: $supportsMicrophone${notes == null ? '' : ', notes: $notes'})';
}

/// A capture device available for encoding.
class CaptureDevice {
  const CaptureDevice({
    required this.kind,
    required this.id,
    required this.name,
    required this.isDefault,
  });

  final CaptureSourceKind kind;
  final String id;
  final String name;
  final bool isDefault;

  @override
  String toString() =>
      'CaptureDevice($kind, "$name"${isDefault ? ', default' : ''})';
}

/// Lifecycle state and progress statistics.
class EncoderEvent {
  const EncoderEvent({
    required this.state,
    this.message,
    this.encodedSeconds,
    this.bitrateKbps,
  });

  final EncoderState state;
  final String? message;
  final double? encodedSeconds;
  final double? bitrateKbps;

  @override
  String toString() =>
      'EncoderEvent($state${message == null ? '' : ', $message'})';
}

/// An error reported by the native pipeline.
class EncoderException implements Exception {
  const EncoderException(this.message);

  final String message;

  @override
  String toString() => 'EncoderException: $message';
}

/// Facade for the surround-encoder plugin.
///
/// ```dart
/// await SurroundEncoder.start(const SurroundEncoderConfig(
///   captureSystem: true,
///   sink: FileSink('out.ts'),
/// ));
/// // ... later
/// await SurroundEncoder.stop();
/// ```
abstract final class SurroundEncoder {
  static final SurroundEncoderImpl _impl = SurroundEncoderImpl();

  /// Broadcast stream of lifecycle events and progress statistics.
  static Stream<EncoderEvent> get events => _impl.events;

  /// Broadcast stream of raw encoded packets. Only populated while a session
  /// configured with [StreamSink] is running.
  static Stream<Uint8List> get packets => _impl.packets;

  /// Reports which features this platform build supports.
  static Future<EncoderCapabilities> capabilities() => _impl.capabilities();

  /// Enumerates capture devices available on this platform.
  static Future<List<CaptureDevice>> listDevices() => _impl.listDevices();

  /// Starts capture and encoding. Throws [EncoderException] on failure.
  static Future<void> start(SurroundEncoderConfig config) =>
      _impl.start(config);

  /// Stops the current session and flushes pending output.
  static Future<void> stop() => _impl.stop();
}
