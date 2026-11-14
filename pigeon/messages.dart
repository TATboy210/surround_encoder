import 'package:pigeon/pigeon.dart';

// This file is where you define the interface between Dart code and host
// platform code. After editing this file, run
//   dart run pigeon --input pigeon/messages.dart
// to re-generate all of the output files.
//
// To learn more about Pigeon, see https://pub.dev/packages/pigeon.

@ConfigurePigeon(
  PigeonOptions(
    dartPackageName: 'surround_encoder',
    dartOut: 'lib/src/messages.g.dart',
    cppHeaderOut: 'windows/messages.g.h',
    cppSourceOut: 'windows/messages.g.cpp',
    cppOptions: CppOptions(namespace: 'surround_encoder'),
    gobjectHeaderOut: 'linux/messages.g.h',
    gobjectSourceOut: 'linux/messages.g.cc',
    objcHeaderOut: 'macos/surround_encoder/Sources/surround_encoder/Messages.g.h',
    objcSourceOut: 'macos/surround_encoder/Sources/surround_encoder/Messages.g.m',
    objcOptions: ObjcOptions(prefix: 'SEN'),
  ),
)

/// Which audio source a device belongs to.
enum CaptureSourceKind { system, microphone }

/// Output channel layout of the encoded stream.
enum ChannelLayout { stereo, surround51 }

/// Where encoded packets are delivered.
enum SinkKind { file, udp, stream }

/// Lifecycle state of the encoder pipeline.
enum EncoderState { idle, starting, running, stopped, error }

/// Codec selector. Only `ac3` is implemented in this release; `dca` is
/// reserved for the planned DTS (DCA) backend.
enum CodecKind { ac3, dca }

/// A capture device visible to the host platform.
class AudioDeviceInfo {
  CaptureSourceKind? kind;
  String? id;
  String? name;
  bool? isDefault;
}

/// Feature availability on the host platform.
class PlatformCapabilities {
  String? platformName;
  bool? supportsSystemCapture;
  bool? supportsMicrophone;
  String? notes;
}

/// Full encoder configuration, validated on both sides. All fields nullable
/// because Pigeon data classes cannot use initializers; the Dart API applies
/// defaults before sending.
class EncoderConfig {
  /// Capture the system's playback audio (loopback/monitor).
  bool? captureSystem;

  /// Capture a microphone.
  bool? captureMicrophone;

  /// Device id for system capture; null = platform default.
  String? systemDeviceId;

  /// Device id for microphone capture; null = platform default.
  String? microphoneDeviceId;

  ChannelLayout? layout;
  CodecKind? codec;
  int? sampleRate;
  int? bitrate;
  SinkKind? sinkKind;

  /// Output file path for [SinkKind.file].
  String? filePath;

  /// Destination host for [SinkKind.udp].
  String? udpHost;

  /// Destination port for [SinkKind.udp].
  int? udpPort;
}

/// Streamed pipeline events and progress statistics.
class EncoderEventData {
  EncoderState? state;
  String? message;
  double? encodedSeconds;
  double? bitrateKbps;
}

@HostApi()
abstract class SurroundEncoderPluginApi {
  /// Enumerates capture devices available on this platform.
  @async
  List<AudioDeviceInfo> listCaptureDevices();

  /// Reports which features this platform build supports.
  @async
  PlatformCapabilities getCapabilities();

  /// Starts capture and encoding with the given configuration.
  @async
  void start(EncoderConfig config);

  /// Stops capture and encoding, flushing pending data.
  @async
  void stop();
}

@FlutterApi()
abstract class SurroundEncoderEventApi {
  /// State transitions and progress statistics.
  void onEncoderEvent(EncoderEventData event);

  /// One encoded elementary-stream packet (stream sink only).
  void onEncodedPacket(Uint8List packet);
}
