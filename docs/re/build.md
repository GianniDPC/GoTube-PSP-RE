# Build instructions

Prerequisites are the pinned local PSPSDK at `/usr/local/pspdev` (GCC 15.2.0)
and the supplied `lib/libspidermonkey.a`. The build uses PSPSDK's installed
intraFont. From the workspace root:

```sh
export PSPDEV=/usr/local/pspdev
export PATH="$PSPDEV/bin:$PATH"
make clean
make historical-package
```

`historical-package` places the reconstructed EBOOT and recovered-verbatim
historical JS/PRXs in `release/GoTube/`. The resulting native executable is a
reconstruction, not the original source or a byte-identical binary.

