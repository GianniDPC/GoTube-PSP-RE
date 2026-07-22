# Qualification status

## Reproducible build

A clean PSPSDK GCC 15.2.0 build succeeds and produces:

- root `EBOOT.PBP`: SHA-256
  `b057005a123fb1169f9cfa0d2f78db6a630273ceb3491fb9ca98d84e0f7ba442`
- `GoTube.elf`: SHA-256
  `a29ec71129f0d65b819ef328f2f4f350aff98324936390dce7b2fc34a2964ab7`
- `GoTube.prx`: SHA-256
  `720f8d5a0c0399d759e795c5fa0bcab346b9245342ef94cd7d07aa7894a40527`
- historical release `release/GoTube/EBOOT.PBP`: SHA-256
  `b057005a123fb1169f9cfa0d2f78db6a630273ceb3491fb9ca98d84e0f7ba442`

The historical package is under `release/GoTube/`; its JS and bundled PRXs
match the authoritative hashes. Its `PARAM.SFO` intentionally identifies this
reconstruction as `GoTube 1.2 RE`. The embedded `ICON0.PNG` and `SND0.AT3`
remain byte-identical to the originals (SHA-256 `cb9b0840...` and
`d07f52e3...` respectively), and its external file inventory matches the
original, including `update.js`. Two consecutive clean builds
produced identical files and byte-identical build logs. A verbose pre-fixup inspection confirms each
module's imports are contiguous, and the normal build emits no import warning.

The final layout pass re-derived the list framing primitive, header line order,
card typography/clipping, menu shading/alignment/animation, playing-item border,
player progress/status geometry, comment placement, panel composition and LCD/TV
safe areas from native VAs `0xa624`, `0xa79c`, `0xa9f4`, `0xacf8`, `0x1e0d8`,
`0x1e380`, `0x1f73c`, `0x1ffb8`, `0x2097c`, `0x23ef8`, `0x26be8`, `0x27b70`,
and `0x29760`. Two consecutive clean historical-package builds were byte-identical.

An initial hardware glyph-artifact pass replaced PSPSDK's universal 0.4 with
0.31, but physical hardware correctly disproved that result. The subsequent
binary audit identified the actual embedded generation as intraFont 0.22:
`0x2000` is Shift-JIS rather than scrolling, the font structure offsets match,
and the native renderer has the 0.22 baseline/multiline algorithm. Two clean
builds of this corrected renderer package were byte-identical. The static ABI
gate `re/scripts/verify_intrafont_abi.py` passes every invariant. See
`gui-equality-audit.md`.

## Deterministic media test

The test-only `GT_PLAYER_SELFTEST` build decodes fixture SHA-256
`73438a33af2da485a437bf0dade34ae43b1095ce902ce56bc6a15b0811fb12dd`:
320x240 FLV1 video at 15 fps with stereo 44.1 kHz MP3. PPSSPP records both
decoders opening, the first rendered frame, and clean end-of-stream. No
player-thread error is present in the final run. The release was rebuilt
without the self-test define afterward.

The independent thumbnail test decodes a 120x90 JPEG fixture (SHA-256
`4018bdb18a7c6ddab78647ef7b5882cf52c7537c58a412099c215dd3c0f0ecef`)
through the PSP IJG/texture conversion path. Its trace reaches
`thumbnail decode complete` without a thumbnail-thread emulator error. The
release was again restored without the test define.

The `GT_STREAM_SELFTEST` build reads the same FLV directly through a custom
FFmpeg ByteIO backed by `sceHttp`. The localhost server records a direct GET;
the PSP trace opens both decoders, renders the first frame, and reaches clean
end-of-stream without creating an intermediate media file. Test PRX SHA-256 is
`ad989255831d4ab8abd5cdecd9c07f4065a12ae3c9aeed6fa0bcb796613114fc`.

The Save test writes `保存:試験.flv` as raw CP932 base
`95db91b681468e8e8cb1`, including original colon substitution `81 46`.
The `.flv` and extension-replaced `.THM` sibling are byte-identical to the
fixtures. A separate Favorites test successfully scans, renames and deletes a
media file and both `.THM`/`.xml` siblings. See `GT12-SAVE-0001` and
`GT12-LOCAL-0001`.

The recovered player dispatcher controls Start pause, Cross stop, Square
background-play/list return, L/R playlist traversal, five Triangle overlay
states, and fifteen Select renderer positions. Deterministic state-cycle tests
wrap at the binary-derived bounds. Video-only streams are paced from demux PTS;
audio streams remain paced by blocking PSP audio output.

The final regression run again opened both FLV1 and MP3 decoders, rendered its
first frame, loaded three comments, and reached clean end-of-stream after the
latest overlay/render-mode changes.

The original binary also contains the MOV/MP4 demuxer, H.264 decoder and the
FAAD-backed MPEG-4 AAC decoder; these had mistakenly been removed from the
reconstruction's reduced FFmpeg configuration. They are restored. The exact
user-supplied 640x480 H.264 Main/AAC-LC MP4 fixture (SHA-256 `651d84ce...`)
opens both decoders, renders its first frame and reaches clean decode completion
in the isolated PSP test build.

Favorites playback also resolves an extension-replaced `.xml` comment
sidecar. A three-entry fixture exercises `vpos`, color, size, top/bottom and
scrolling tokens; PPSSPP reports `comment count 3` and clean media completion.
The original 400-centisecond visibility interval and Triangle comment bit are
implemented. Glyph-pixel comparison remains unavailable because the pinned
headless setup cannot load `jpn0.pgf`.

The Favorites special-extension dispatch ignores ASX/WVX/WAX exactly as their
original null callbacks do. Its ASHX callback parses RSS 2.0 channel/item
title, description and link fields. The two-item PPSSPP fixture produces the
expected channel and direct child URLs.

The `GT_TV_SELFTEST` path forces component, composite and LCD GU
reconfiguration without relying on cable emulation. PPSSPP accepts the exact
16-bit stride-768 TV double-buffer layout, records 720x480 for both TV modes,
the composite 40-pixel safe-area origin, and restoration to the 32-bit
stride-512 LCD layout without invalid VRAM or GU errors. The physical DVE signal
still requires real hardware qualification.

## PPSSPP smoke tests

Tests used PPSSPP v1.20.4, an isolated `XDG_CONFIG_HOME`, and a 12-second host
timeout. The reconstruction reached JS initialization, loaded the two verbatim
site scripts, evaluated `cfg.js` and `site.js`, completed the site-list callgate,
and entered GUI initialization without an emulated crash.

After restoration of the original weak `pspDveManager` imports, PPSSPP reports
two unknown syscalls when the splash probes the dynamically loaded homebrew
driver. Execution continues through GUI initialization. This is the same class
of unsupported homebrew-driver behavior that prevents using PPSSPP as the
video-out oracle.

The untouched original did not provide a complete oracle: PPSSPP reported
unknown syscalls and stopped on a bad memory access. The headless setup also
failed to expose `flash0:/font/jpn0.pgf` successfully to either build. This is
consistent with PPSSPP being incomplete for some homebrew behavior; it is a
test limitation, not evidence that the original binary is faulty.

The normal historical release was smoke-tested separately after all test-only
builds. It creates no `gt_trace.txt`; instrumentation is absent unless a
`GT_TEST_CFLAGS` build is explicitly requested.

## Claim boundary

All behavior that can be established statically or exercised in the available
PPSSPP envelope is implemented and qualified. The remaining checks are physical
PSP observations: original PGF glyph pixels, DVE cable output, system utility
interaction, controls on hardware, and long-run AV timing. PPSSPP cannot close
those items because it does not provide a complete oracle for this homebrew.
