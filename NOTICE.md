# Notices

This file describes third-party components used by `surround_encoder` and
the compliance terms that apply when redistributing them.

## FFmpeg

This plugin links against [FFmpeg](https://ffmpeg.org) — specifically
`libavcodec`, `libavformat`, `libswresample` and `libavutil` — to provide the
AC-3 (ATSC A/52) audio encoding and the MPEG-TS muxing used for file and UDP
output.

- **License:** FFmpeg is licensed under the
  [GNU Lesser General Public License (LGPL) version 2.1 or later](https://www.ffmpeg.org/legal.html)
  when built without GPL components.
- **How this project stays LGPL-compliant:**
  1. FFmpeg is linked **dynamically**; the plugin never statically links
     FFmpeg.
  2. **No FFmpeg binaries are committed to this repository or published
     through pub.dev.** Every consumer builds their own FFmpeg from source
     using the provided scripts (`tool/setup_ffmpeg.ps1` /
     `tool/setup_ffmpeg.sh` — minimal audio-only LGPL configuration) or
     links a system FFmpeg.
  3. No GPL-only components (x264, x265, libdca, etc.) are enabled by the
     provided scripts.
- **Build trimming:** the provided scripts build FFmpeg with
  `--disable-everything`, re-enabling only the AC-3 encoder, MPEG-TS muxer
  and file/udp protocols, and drop avdevice/avfilter/swscale entirely. The
  bundled libraries therefore contain **no video codecs**. Trimming does not
  change the LGPL obligations above; it only reduces what ends up in your
  application bundle.
- If you distribute an application built with this plugin, you must comply
  with LGPL-2.1 for the FFmpeg portions: include the FFmpeg license text, and
  offer the corresponding FFmpeg source (or a written offer) to your
  recipients. See <https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>.

## Codec naming and trademarks

This plugin is an **independent open implementation**. It is **not**
affiliated with, endorsed by, sponsored by, or certified by Dolby
Laboratories, Inc., DTS, Inc., Xperi Inc., Creative Technology Ltd., or any
other vendor.

- "AC-3" and "DCA"/"Coherent Acoustics" are used purely to identify the
  *bitstream formats* this plugin produces, as documented in the public
  standards ATSC A/52 and ETSI TS 102 366.
- "Dolby", "Dolby Digital", "DTS", "DTS Connect", and related trademarks
  belong to their respective owners and are **not** used to brand this
  project. Do not use this project's name or output in any way that implies
  Dolby/DTS certification or licensing.
- The underlying codec patents for basic AC-3 and DTS encoding have expired;
  no patent license is required for the formats supported in this release.
  Newer variants (E-AC-3 / "Dolby Digital Plus", DTS:X, DTS-HD) are **not**
  implemented and remain subject to patents — do not add them without
  appropriate licensing.

## Platform capture notes

- **macOS:** system-audio capture uses ScreenCaptureKit (macOS 13+). The
  *host application* must hold the screen-recording permission granted by the
  user (System Settings → Privacy & Security → Screen Recording).
- **Linux:** capture uses libpulse, working against both PulseAudio and
  PipeWire (via `pipewire-pulse`).
- **Windows:** capture uses WASAPI loopback; no extra permissions are needed.
