# Prebuilt opera-boot ROM

`opera-boot.bin` is the image a3DO compiles in as its built-in system ROM
(see `cmake/embed_binary.cmake` and `src/app/builtin_bios.h`).

It is built from the source in this directory's parent: upstream opera-boot
commit `5ba4e67` (release `2026-10-12`) with a3DO's modification, which
replaces the upstream third-party 3DO logo on the startup banner with original
artwork (`../src/banner.c`). Built 2026-11-19 with the 3DO DevKit via
`tools/build-opera-boot.sh`.

    sha256  38c4a7422467ea5bacd1551835e69feec613b220e5e13b995e4f9cef3447e0f3
    size    131072 bytes

Compared with the upstream release, only the startup-banner code in the launch
image differs (offsets 0x40F-0x758); the DIPIR loader and ROM tags are
byte-identical.

To change the ROM, edit the source in `../src` or `../launch` and run
`tools/build-opera-boot.sh /path/to/3do-devkit` from the repository root, then
commit the source change and the new image together and update the hash above.

## Licence

opera-boot is GPL-2.0-only (`../LICENSE`). The a3DO banner artwork is original
and covered by the same licence.
