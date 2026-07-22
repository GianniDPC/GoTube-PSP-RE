# GoTube 1.2 RE

A source-level reverse engineering of GoTube 1.2 for the Sony PSP. The goal is
to reproduce the original application's interface and behavior while keeping
the reconstructed implementation readable and buildable.

This is an independent preservation project and is not affiliated with Sony or
the original GoTube authors.

## Current functionality

- Original-style provider, results, menu, multiview and player interfaces
- Favorites and Playlist filesystem navigation
- Infrastructure-network configuration and connection status dialogs
- JavaScript site-provider bridge and HTTP access
- Local and streamed MP4 playback with H.264 video and AAC audio
- Playback controls, comments, progress display, thumbnails and video output

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
