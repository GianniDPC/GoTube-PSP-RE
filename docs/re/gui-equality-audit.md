# GoTube 1.2 GUI equality audit

Recorded 2026-07-20 after the real-PSP stale/date-glyph investigation.

## Root cause closed by this pass

Two successive library identifications were tested. PSPSDK's
`intraFont_universal 0.4` was clearly too new, but the first replacement with
public intraFont 0.31 was also wrong. The physical-PSP retest disproved that
intermediate conclusion.

The decisive discriminator is option bit `0x2000`: native `FUN_0001172c`
uses it for Shift-JIS input, while 0.25 and later use it for scrolling. With
0.31, every CRLF description was consequently flattened into a long scrolling
line. The native structure also ends with `size` at +0x34, colors at +0x38 and
+0x3c, and a 16-bit `options` at +0x40; 0.31 has a different structure.

The author's original `intraFont_0.22.zip`, archived from the 2007 release
site, was recovered and its exact sources are now in
`re/vendor-sources/intrafont-0.22/`. SHA-256 of the archive is
`6cb89636ab8710651968385d1f8bf77c5feb90c0a3b84df5fd3de8ac2e562b85`.
GoTube's two visible modifications to the renderer tail (no cache writeback and
no depth disable/enable pair) are applied and documented in the vendored file.

## Native-to-reconstruction map

| Native VA | Recovered behavior | Reconstruction status |
| --- | --- | --- |
| `0x0000dc04` | 0.22 font style fields (`size` +0x34, colors +0x38/+0x3c, `options` +0x40) | Exact layout enforced by compile-time assertions |
| `0x000104b8` | 0.22 UCS-2 renderer; native tail leaves depth unchanged | 0.22 source with the native tail modification |
| `0x0001172c` | `0x2000` selects Shift-JIS conversion, preserving CRLF | Exact 0.22 option and wrapper behavior |
| `0x00026be8` | GU init: depth off, scissor/texture on, clamp/linear, modulate/RGBA | Matched in `configure_gu` |
| `0x0001e380` | result preamble resets texfunc, scissor, color, wrap, color/alpha/blend/depth states | Matched at `render_results` entry |
| `0x0001e380` | header y=0..27; rows start y=28; 100 px row height | Matched |
| `0x0001e380` | title/length/description baselines +17/+29/+41 | Matched |
| `0x0001e380` | description clip y=+29..+53 and metadata at +65 | Matched |
| `0x004bad38` | scissor arguments are absolute left/top/right/bottom endpoints | Matched through an endpoint-to-width/height PSPSDK compatibility wrapper |
| `0x00012594` | local file dates use `\r\n` exactly | Matched; no speculative line splitting |
| `0x0000a9f4` | four ABGR4444 lines plus inset sprite, GU-list allocation | Matched |
| `0x0001e0d8` | menu vertical shade from `f666` toward `a666` | Matched |
| `0x0001e0d8` | menu label/separator vertical base is `0x28` (40) | Matched |
| `0x0001ffb8` | menu half-step animation to x=400 and back to canvas edge | Matched |
| `0x00027448` | alternate 0x60000-byte command lists through uncached aliases | Matched |
| `0x00026784` | finish, two callback vblanks, sync, swap | Matched |
| `0x00021958` | five 256-entry Y/V/U ABGR conversion lookup tables | Matched from recovered 1.164/1.596/0.813/0.391/2.018 coefficients |
| `0x00023ef8` | Y, V and U are GU_PSM_T8 plane textures; V/U use additive and reverse-subtractive passes | Matched; the non-native synthesized RGBA upload was removed |
| `0x00023ef8` | luma wider than 512 pixels is submitted as two texture strips | Matched for the 640x480 MP4 path |

The temporary layer-toggle instrumentation used to isolate the triggering
draw call is absent from the release build.

On 2026-07-22 the description clipping path was re-audited below the renderer.
The original application statically links an older GU wrapper whose third and
fourth arguments are absolute right/bottom endpoints. The installed modern
PSPSDK API uses width/height. Earlier reconstruction calls mixed those two
contracts whenever the clip origin was nonzero, expanding the description
scissor into the header or split-view area. All native-coordinate call sites
now pass through the compatibility wrapper documented by `GT12-GU-0001`.

## Verification

Two consecutive clean `historical-package` builds were byte-identical. The
final package hashes are recorded in `qualification.md`. PPSSPP boots the build
without a new crash, but the isolated emulator cannot load
`flash0:/font/jpn0.pgf`; therefore actual glyph pixels and this hardware-only
failure remain a physical-PSP qualification item.
