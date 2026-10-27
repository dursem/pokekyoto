POKEKYOTO ROUTE 1 - PCM V5 / ROM SIZE FIX (ENGLISH)
===================================================

Resolves linker error: region `ROM' overflowed by 94400 bytes.
Install PCM V4 first; this package ONLY replaces an existing PCM V4 sample.

Preserves the full soundtrack, speed, loop and music volume.
Uncompressed, looped signed 8-bit PCM, no DPCM. No song ID, map assignment,
global mixer, sound effects, or MIDI controller changes.

Default: 19500 Hz; saves 202,428 bytes versus the previous 21024-Hz sample.
Based on the reported build, leaves about 108,028 bytes free.
Optional maximum: 20000 Hz; saves 136,016 bytes, leaves ~41,616 bytes free.

IN WSL / UBUNTU (from the PokéKyoto source repository):
  unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Route1_PCM_ROMFIX_V5.zip" -d .
  python3 PokeKyoto_Route1_PCM_ROMFIX_V5/install_route1_romfix.py . --dry-run
  python3 PokeKyoto_Route1_PCM_ROMFIX_V5/install_route1_romfix.py .
  make -j4

Options: --quality maximum | --check | --restore
Restoring the old sample may overflow the ROM again.

File/installer checks passed. Real mGBA / ROM build testing remains necessary.
