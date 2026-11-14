## 0.1.0

* Initial release.
* Real-time capture of system playback audio (WASAPI loopback /
  ScreenCaptureKit / PulseAudio sink monitor) and microphone input.
* AC-3 encoding at 48 kHz with stereo or 5.1 output via FFmpeg.
* MPEG-TS output to local files and UDP destinations; raw-packet streaming
  to Dart.
* Windows (x64/arm64), macOS 13+ (arm64/x64), Linux (x64/arm64).
* DTS (DCA) core encoding via the independent LGPL dcaenc implementation
  (`CodecKind.dca`, 64–1509 kbps, 512-sample frames).
* Minimal audio-only FFmpeg build scripts (`tool/setup_ffmpeg.*`) — no
  video codecs in the bundled libraries, a few MB instead of ~113 MB.
* dcaenc build scripts (`tool/setup_dcaenc.*`) for all supported platforms.
