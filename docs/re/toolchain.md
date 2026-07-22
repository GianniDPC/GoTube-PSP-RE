# Tool and host inventory

Recorded 2026-07-19.

- Host: Ubuntu kernel `7.0.0-14-generic`, x86-64.
- Ghidra bundle: `12.1.2_PUBLIC` (local bundle; Java runtime `21.0.9+10`).
  Fresh headless import selected `PSP Executable (ELF)` and
  `Allegrex:LE:32:default:default`. Analysis completed in 112 seconds, with a
  loader warning for unsupported PSP type-A relocation 7 that must be accounted
  for during review.
- Hash utility: GNU coreutils `sha256sum`.
- Extraction: Python standard library only.
- PPSSPP: AppImage `v1.20.4` at
  `/home/gianni/Desktop/PSP/toolchain/ppsspp/PPSSPP.AppImage`. Isolated test
  configuration uses `XDG_CONFIG_HOME` beneath ignored `re/fixtures/`.
- PSPSDK: `/usr/local/pspdev`; `psp-gcc` 15.2.0. Invoke builds with
  `PSPDEV=/usr/local/pspdev PATH=/usr/local/pspdev/bin:$PATH make`.
- Font renderer: vendored intraFont 0.22 from BenHur's original archived 2007
  source release (archive SHA-256 `6cb89636...e562b85`). Native structure
  offsets and the `0x2000` Shift-JIS option prove this generation. Neither
  PSPSDK's universal 0.4 nor intraFont 0.25+ is ABI-compatible because those
  releases repurpose `0x2000` for scrolling. The separate 0.31 `libccc.c` is
  retained only for reconstruction-owned UTF-8/CP932 utility conversions; it
  is not used by the font renderer.
- PSP Ghidra/Allegrex extension: installed at
  `Ghidra/Extensions/ghidra-allegrex`; loader/language selection verified by the
  headless import log.

PPSSPP is a partial oracle only. In the isolated boot test, the untouched
original reached its main thread but hit unknown syscalls and stopped after a
bad memory access. This prevents treating PPSSPP as an oracle for all homebrew
PRX/ME/video-out paths.
