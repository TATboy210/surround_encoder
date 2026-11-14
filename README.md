# surround_encoder

Real-time surround-sound capture and encoding for Flutter desktop apps.
Capture **system playback audio** (what your speakers would play) and/or a
**microphone**, mix them, and encode them live into a **5.1 AC-3 or DTS
(DCA) stream** (wrapped in MPEG-TS for file/UDP output, or delivered as raw
packets to Dart).

Think of it as the open, self-hostable equivalent of the "what you hear"
encoders bundled with consumer sound-card software — built entirely on open
implementations: FFmpeg's AC-3 encoder and dcaenc's DTS Coherent Acoustics
encoder (both written from public standards). See [NOTICE.md](NOTICE.md) for
trademark and licensing notes: this project is **not** affiliated with or
certified by Dolby, DTS, Creative, or any other vendor, and contains no code
derived from any proprietary product.

## Platform support

| Platform | System audio capture | Microphone | Architectures |
|---|---|---|---|
| Windows | WASAPI loopback | WASAPI | x64, arm64 |
| macOS 13+ | ScreenCaptureKit | CoreAudio (AVAudioEngine) | arm64, x64 |
| Linux | PulseAudio/PipeWire sink monitor | PulseAudio | x64, arm64 |

Verified end-to-end on Windows x64 (`ffprobe`):
- `ac3 / 48000 Hz / 6ch / 5.1(side) / 448 kb/s`
- `dts / 48000 Hz / 6ch / 5.1(side) / 768 kb/s`

macOS and Linux build in CI; behavioral testing there is still pending —
reports welcome.

## Quick start

```dart
import 'package:surround_encoder/surround_encoder.dart';

// Encode system audio to a playable MPEG-TS file.
await SurroundEncoder.start(const SurroundEncoderConfig(
  captureSystem: true,
  layout: ChannelLayout.surround51,
  bitrate: 448000,
  sink: FileSink('out.ts'),
));

// ... or encode a DTS (DCA) core stream instead.
await SurroundEncoder.start(const SurroundEncoderConfig(
  captureSystem: true,
  codec: CodecKind.dca,
  bitrate: 768000,
  sink: FileSink('out-dts.ts'),
));

// ... or stream it over UDP — play with `ffplay udp://@:1234`.
await SurroundEncoder.start(const SurroundEncoderConfig(
  captureSystem: true,
  sink: UdpSink('239.255.0.1', 1234),
));

// Progress events.
SurroundEncoder.events.listen((event) {
  print('${event.state} ${event.encodedSeconds}s @ ${event.bitrateKbps}kbps');
});

await SurroundEncoder.stop();
```

Raw encoded packets are also available in Dart via
`SurroundEncoder.packets` when the session uses `const StreamSink()`.

Codec notes: AC-3 accepts 96–512 kbps; the DCA core bitstream accepts
64–1509 kbps (dcaenc snaps to the nearest valid rate). Both run at 48 kHz.

## Building: FFmpeg + dcaenc requirements

Neither FFmpeg nor dcaenc is shipped in this repository. The plugin needs
two minimal source builds (both produced by the setup scripts):

1. **Minimal audio-only FFmpeg 8.1** — built with `--disable-everything`,
   re-enabling only the AC-3 encoder, the MPEG-TS muxer and the file/udp
   protocols. A few MB of DLLs instead of the ~113 MB of a full build, with
   **no video codecs**. Layout: `native/ffmpeg/windows-x64` etc., auto
   discovered by CMake, or set `SURROUND_ENCODER_FFMPEG_ROOT`.
2. **dcaenc** — the independent LGPL DTS (DCA) encoder (~290 KB). Layout:
   `native/dcaenc/windows-x64` etc., or set `SURROUND_ENCODER_DCAENC_ROOT`.

```powershell
# Windows (x64): requires MSYS2 (https://msys2.org) + VS Build Tools
.\tool\setup_ffmpeg.ps1                        # -Msys2Root D:\tools\msys64
.\tool\setup_dcaenc.ps1                        # -Msys2Root D:\tools\msys64
```

```bash
# Linux / macOS (triplet auto-detected; or pass one)
./tool/setup_ffmpeg.sh                        # requires git, make, gcc/clang
./tool/setup_dcaenc.sh
```

Layout names used by the plugin's CMake/podspec: `windows-x64`,
`windows-arm64`, `arm64-osx`, `x64-darwin`, `linux-x64`, `linux-arm64`.

Notes:
- If your network needs a proxy for GitHub, export `http_proxy` /
  `https_proxy` before running the scripts.
- Windows arm64: build FFmpeg with `llvm-mingw`'s aarch64 toolchain or
  cross-compile in WSL (`gcc-aarch64-linux-gnu` + `--enable-cross-compile`);
  the CI arm64 leg still validates integration against a full vcpkg FFmpeg
  build, which is fine for correctness — only the *size* differs from the
  minimal build.

## Example app

`example/` demonstrates device enumeration, sink selection (file / UDP /
Dart stream), layout switching and start/stop. For an unattended smoke run:

```bash
cd example
flutter run --release --dart-define=SURROUND_AUTO_START=true
# encodes 8 s of system audio to AC-3, then 8 s to DTS
```

## Architecture

```
┌─ per-platform capture (ICaptureSource) ──────────────────────┐
│ Windows: WASAPI loopback / WASAPI capture                    │
│ macOS:   ScreenCaptureKit stream / AVAudioEngine input tap   │
│ Linux:   PulseAudio sink-monitor / PulseAudio source         │
└──────────────┬───────────────────────────────────────────────┘
               ▼  interleaved float32
   resample 48 kHz + upmix to 2.0/5.1 (libswresample)
               ▼
   mix sources + soft-clip
               ▼
   AC-3 encoder (libavcodec, 1536-sample frames)
               ▼
   MPEG-TS muxer (libavformat) ──► file / udp://
   or raw packets ─────────────► Dart EventChannel
```

The common pipeline lives in `src/common/`, platform backends in
`src/windows/`, `src/linux/` and the macOS pod target. The Dart/host
interface is generated with Pigeon from `pigeon/messages.dart`.

## Limitations & roadmap

- Only AC-3 encoding is implemented; DTS (DCA) via `dcaenc` is planned — the
  `CodecKind` enum already reserves it.
- No echo cancellation / noise suppression; sources are simply mixed.
- macOS system capture requires the host app to hold the screen-recording
  permission (macOS 13+).
- SPDIF/IEC 61937 passthrough playback and mobile platforms are not
  implemented.

## License & compliance

Plugin code: [MIT](LICENSE). FFmpeg: LGPL-2.1, dynamically linked, built by
you from source — see [NOTICE.md](NOTICE.md) for the full compliance and
trademark policy before redistributing.
