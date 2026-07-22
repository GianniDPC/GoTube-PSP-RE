# PSP Media Engine acceleration research

Status: research complete; no playback code changed by this investigation.

## Result

There is a credible route to substantially faster YouTube playback on a real
PSP, but merely loading GoTube's bundled `mediaengine.prx` will not accelerate
FFmpeg. The best next experiment is a hardware AVC video backend using the PSP
firmware MPEG/VME decoder, while retaining the current FFmpeg MOV demuxer and
AAC decoder.

The current 144p YouTube stream (itag 160, H.264 Main at 256x144) is a good
candidate for that experiment. Its resolution and profile are comfortably
inside the range handled by historical PSP AVC players. Audio can remain on the
main CPU initially.

## What the bundled PRX actually is

The exact file shipped with GoTube 1.2 and preserved at
`runtime/mediaengine.prx` has SHA-256:

```
a65481e5baa70b02b8ce6edaa3ecace8282c9424968ad422600d2c5d4e778556
```

Static inspection of this 3.3 KiB MIPS PRX shows a small Media Engine bootstrap
and arbitrary callback executor. It initializes the second core, performs cache
maintenance, invokes a supplied callback with an argument, and reports job
state. It contains no H.264, AAC, IDCT, motion-compensation, or container code.

The original GoTube 1.2 EBOOT has no string or call-site that loads this PRX;
its only splash-time homebrew module load names `dvemgr.prx`. Thus the original
application did not obtain video acceleration from the bundled file. This agrees
with reverse-engineering evidence `GT12-ONSEN-0001`.

## Options assessed

| Route | Expected benefit | Cost/risk | Verdict |
| --- | --- | --- | --- |
| Load bundled `mediaengine.prx` only | None | Low | Not useful by itself |
| Move all FFmpeg H.264 decoding to the second MIPS core | Potentially large | Very high: separate execution environment, memory placement, cache coherency, runtime dependencies, and no Allegrex VFPU | Poor first route |
| Offload YUV-to-RGB conversion to the second core | Useful in players that convert on CPU | Moderate | Not useful here: GoTube already renders Y, U, and V planes directly with GU textures |
| Offload AAC decode | Modest | Moderate/high | Possible later, but video decoding is the dominant load |
| PSP firmware AVC decoder through `sceMpeg`/`sceVideocodec` | Large; uses the PSP video-decoding hardware/Media Engine path | Moderate/high integration effort and requires real-hardware qualification | Recommended prototype |

The modern custom Media Engine project demonstrates that arbitrary work can be
run on the second core, but it does not provide an H.264 codec. Moonlight PSP's
current design similarly uses its Media Engine worker for color conversion, not
H.264 decoding. That optimization does not transfer to GoTube's planar YUV GU
renderer.

## Why the firmware AVC route fits GoTube better than Moonlight

An earlier Moonlight PSP hardware-decoder attempt was abandoned because
`sceMpeg` expects a continuous, ordered, correctly framed stream while live RTP
can arrive late, missing, or out of order. GoTube does not have that transport
problem: FFmpeg's MOV demuxer already receives an ordered fragmented-MP4 byte
stream and emits complete H.264 access-unit packets.

The remaining adaptation is deterministic:

1. Read SPS/PPS and NAL-length size from the MP4 `avcC` extradata.
2. Convert each MP4 length-prefixed sample to the framing required by the PSP
   decoder (normally Annex B start codes).
3. Prepend SPS/PPS at initialization and after decoder reset; add access-unit
   delimiters only if physical-device testing shows they are required.
4. Submit access units to the firmware decoder and present decoded frames to a
   dedicated renderer path.
5. Keep the existing FFmpeg software decoder as a runtime/build fallback.

## Recommended staged prototype

### Stage 1: decoder probe, not a player rewrite

- Add an optional `GT_PSP_AVC` backend behind the existing player interface.
- Use the existing network and MOV demux code unchanged.
- Capture the first several itag-160 video packets plus `avcC` metadata to a
  deterministic local fixture.
- Initialize the firmware MPEG modules and feed only that fixture.
- Log every module-load, decoder-init, access-unit, and decode return code.
- Verify decoded dimensions and several frame checksums on a physical PSP.

This isolates the undocumented decoder contract from network, audio, timing,
and UI behavior. It also prevents another cycle where an experimental backend
breaks the known-working software player.

### Stage 2: frame presentation

- Prefer the YCbCr decode/output path if its physical-PSP buffer contract can be
  established, because it can feed the existing planar renderer with the least
  conversion work.
- Otherwise use the supported RGB output mode and add a small direct texture
  presentation path.
- Confirm cache writeback/invalidation and 16-byte/64-byte alignment rules on
  hardware.

### Stage 3: live integration

- Place the hardware backend behind automatic capability detection.
- Preserve software H.264 as fallback for unsupported profiles, initialization
  failure, and PPSSPP.
- Reconnect video PTS to the current AAC audio clock and frame-drop policy.
- Benchmark decode time, queue depth, dropped frames, and audio underruns at
  222 MHz and 333 MHz before considering a higher YouTube representation.

## Emulator limitation

PPSSPP is useful for application flow but is not a complete oracle for this
backend. Its `sceMpeg` emulation implements common PSMF paths using the host's
decoder, while some lower-level AVC functions remain explicitly unimplemented.
A probe can be made emulator-safe, but success and performance must ultimately
be qualified on a real PSP.

## Decision

Do not wire the bundled `mediaengine.prx` into the current FFmpeg decoder. Keep
it as preserved original media. Build the isolated firmware-AVC fixture probe
next. If that succeeds on hardware, it is the most credible path to smooth
playback without sacrificing GoTube's on-device-only requirement.

## Primary references

- PSPSDK MPEG interface: <https://pspdev.github.io/pspsdk/pspmpeg_8h.html>
- PSPSDK video-codec interface: <https://pspdev.github.io/pspsdk/pspvideocodec_8h.html>
- PPSSPP Media Engine overview: <https://www.ppsspp.org/docs/development/ppsspp-internals/media-engine/>
- PPSSPP `sceMpeg` implementation: <https://github.com/hrydgard/ppsspp/blob/master/Core/HLE/sceMpeg.cpp>
- PSP Media Engine/VME notes: <https://www.psdevwiki.com/psp/Media_Engine>
- Custom Media Engine core: <https://github.com/mcidclan/psp-media-engine-custom-core>
- Moonlight PSP implementation/history: <https://github.com/k4idyn/Moonlight-PSP>
- Historical PSP AVC API constraints discussed by PMP Mod's author: <https://ffmpeg.org/pipermail/ffmpeg-devel/2006-June/013026.html>
