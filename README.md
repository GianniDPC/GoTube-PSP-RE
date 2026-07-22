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
- JavaScript site-provider bridge and HTTP access
- Local and streamed MP4 playback with H.264 video and AAC audio
- Playback controls, comments, progress display, thumbnails and video output

## Modern YouTube branch

The modern provider deliberately does not use the historical provider
JavaScript. It uses native C and a PSP build of libcurl with mbedTLS to make
TLS 1.2 Innertube requests. Search results are parsed under a 512 KiB response
cap and thumbnails are downloaded over the same verified TLS transport.

Playback requests use YouTube's JS-less Android VR client and select only
progressive itag 18: one MP4 stream containing 360p H.264 Baseline video and
AAC-LC audio. The PSP does not attempt to merge DASH tracks or decode AV1,
VP9, Opus, high-profile H.264, or resolutions beyond its practical limits.
The MP4 is consumed sequentially with a 256 KiB ring buffer, so the full video
is neither stored in RAM nor downloaded before playback.

This compatibility route cannot make every YouTube item playable. Live, DRM,
age/account restricted, made-for-kids client-restricted, and videos without a
progressive itag 18 are reported as unsupported. YouTube can change its
private Innertube behavior; client constants are intentionally isolated in
[`src/media/modern.c`](src/media/modern.c).

Real PSP hardware is the reference test platform. PPSSPP is useful for many
code paths but does not reproduce every homebrew graphics/media behavior.

## Build requirements

- A current [PSPDEV](https://pspdev.github.io/) toolchain with PSPSDK
- `psp-config`, `psp-gcc`, `psp-prxgen`, `pack-pbp` and GNU Make in `PATH`

The repository includes the PSP-ready libraries and headers used by the
current build: SpiderMonkey, FFmpeg, FAAD2 and intraFont. No original EBOOT or
Ghidra project is required.

## Building

```sh
export PATH=/usr/local/pspdev/bin:$PATH
make -j4
make historical-package
```

The installable directory is generated at `release/GoTube/`. Copy that entire
folder to `ms0:/PSP/GAME/GoTube/` on the Memory Stick.

To force a clean application rebuild:

```sh
make clean
make -j4
make historical-package
```

The checked-in FFmpeg, FAAD2 and SpiderMonkey archives target PSP/MIPS and are
linked directly by the application build.

## Repository layout

- `src/` — reconstructed application implementation
- `include/` — application headers and generated CP932 table
- `runtime/` — JavaScript providers/configuration and required PSP modules
- `assets/` — PBP icon and startup audio
- `vendor/` — intraFont source plus PSP FAAD2 headers/library
- `third_party/ffmpeg/` — PSP FFmpeg source, headers and libraries
- `lib/` — PSP SpiderMonkey library and headers
- `docs/re/` — reverse-engineering notes and recovered behavior documentation

## Status

This remains a reverse-engineering project. Provider endpoints from 2010 may
no longer work even when their original client behavior is reproduced.
