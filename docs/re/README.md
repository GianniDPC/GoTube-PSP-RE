# GoTube 1.2 reconstruction evidence

This directory contains reviewable metadata and claims for a behavioral/source
reconstruction. It does not contain original binaries. Generated extraction and
analysis data belongs under the ignored `re/` directory.

Reproduce the initial extraction from the workspace root with:

```sh
python3 re/scripts/extract_pbp.py \
  Original_GoTube1_2/PSP/GAME/GoTube/EBOOT.PBP re/extracted
sha256sum -c docs/re/original-sha256.txt
```

The executable original remains the authority. Native C in `src/` is a
reconstruction and must not be described as original source.
