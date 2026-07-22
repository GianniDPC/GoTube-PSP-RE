# GoTube 1.2 RE

A source-level reverse engineering of GoTube 1.2 for the Sony PSP. The goal is
to reproduce the original application's interface and behavior while keeping
the reconstructed implementation readable and buildable.

This is an independent preservation project and is not affiliated with Sony or
the original GoTube authors.

The `main` branch is the preservation-focused GoTube 1.2 reconstruction. The
`modern-youtube` branch keeps that interface while adding a standalone modern
YouTube provider. It runs entirely on the PSP: no PC, phone, proxy, bridge, or
user authentication is required.

## Current functionality

- Original-style provider, results, menu, multiview and player interfaces
- Favorites and Playlist filesystem navigation
- Infrastructure-network configuration and connection status dialogs
- Native provider registry and TLS HTTP access on `modern-youtube`
- Local and streamed MP4 playback with H.264 video and AAC audio
- Playback controls, comments, progress display, thumbnails and video output

## Modern YouTube branch

The modern application does not create a JavaScript runtime or load historical
provider scripts. Favorites, Playlist, Onsen and YouTube are registered in
native C. A PSP build of libcurl with mbedTLS makes TLS 1.2 Innertube requests;
responses are parsed under a 512 KiB cap and thumbnails use the same verified
TLS transport. The reconstructed scripts remain in the repository only as
preservation evidence and are not included in the modern install package.

Playback requests use YouTube's JS-less Android VR client and select the
lowest current adaptive pair: itag 160 (256x144 H.264) and itag 139 (AAC).
The two fragmented MP4 inputs are consumed concurrently and queued into the
original-style video/audio workers; no temporary merged file is created.
Modern H.264 uses fast decoding and skips deblocking to stay within the PSP CPU
budget. The PSP does not attempt to decode AV1, VP9, Opus, or resolutions beyond
its practical limits. A bounded 512 KiB ring backs each network input, so the
full video is neither stored in RAM nor downloaded before playback. Save keeps
using progressive itag 18 because a directly playable single MP4 is required.

This compatibility route cannot make every YouTube item playable. Live, DRM,
age/account restricted, made-for-kids client-restricted, and videos without the
selected 144p H.264/AAC pair are reported as unsupported. YouTube can change its
private Innertube behavior; client constants are intentionally isolated in
[`src/media/modern.c`](src/media/modern.c).

Real PSP hardware is the reference test platform. PPSSPP is useful for many
code paths but does not reproduce every homebrew graphics/media behavior.

## Build requirements

- A current [PSPDEV](https://pspdev.github.io/) toolchain with PSPSDK
- `psp-config`, `psp-gcc`, `psp-prxgen`, `pack-pbp` and GNU Make in `PATH`

The repository includes the PSP-ready libraries and headers used by the
current build: FFmpeg, FAAD2, intraFont, libcurl and mbedTLS. No original EBOOT
or Ghidra project is required.

## Building

```sh
export PATH=/usr/local/pspdev/bin:$PATH
make -j4
make historical-package
```

The package also builds `cooleyesBridge.prx`, the GPL-2.0-or-later
firmware-NID bridge derived from cooleyes' PMPlayer Advance source. It enables
the optional PSP hardware AVC backend; unsupported firmware/emulators fall back
to FFmpeg automatically.

The installable directory is generated at `release/GoTube/`. Copy that entire
folder to `ms0:/PSP/GAME/GoTube/` on the Memory Stick.

To force a clean application rebuild:

```sh
make clean
make -j4
make historical-package
```

The checked-in FFmpeg archives target PSP/MIPS and are linked directly by the
application build. `third_party/ffmpeg-modern` is the playback build (FFmpeg
n0.8.1 with MOV fragments, H.264, AAC, FLV/H.263 and MP3 enabled); the older
tree and FAAD2 dependency remain as preservation material. The historical SpiderMonkey files remain
only as preservation material and are not compiled into `modern-youtube`.

## Repository layout

- `src/` — reconstructed application implementation
- `include/` — application headers and generated CP932 table
- `runtime/` — CA bundle, required PSP modules and preserved historical scripts
- `assets/` — PBP icon and startup audio
- `vendor/` — intraFont source plus PSP FAAD2 headers/library
- `third_party/ffmpeg-modern/` — active PSP FFmpeg n0.8.1 source and libraries
- `third_party/ffmpeg/` — preserved original-era FFmpeg source and libraries
- `lib/` — preserved PSP SpiderMonkey library and headers (not linked here)
- `docs/re/` — reverse-engineering notes and recovered behavior documentation

## Status

This remains a reverse-engineering project. Provider endpoints from 2010 may
no longer work even when their original client behavior is reproduced.
