POKÉKYOTO – ROUTE 1 ORCHESTRAL HQ AUDIO REPLACEMENT
==================================================

This is a brand-new conversion of the original Route 1(1).mp3 recording,
not a reconstructed MIDI composition. All instruments in the original
mixed recording are retained as faithfully as 8-bit GBA PCM allows.

Included versions:
- HQ: 26758 Hz, 8-bit mono PCM; 132.83 seconds; ~3.39 MiB in ROM.
- Compatible: 13379 Hz, 8-bit mono PCM; ~1.69 MiB in ROM.
- MP3 previews are encoded from the ACTUAL quantized game PCM samples.

The existing custom MUS_KYOTO_ROUTE1 track is reused if installed.
The installer assigns it only to data/maps/Route1_Frlg/map.json (MAP_ROUTE1).
Route 2, Littleroot Town, and title-screen music are kept as they are.
It uses a full-length looping wave and a held-note controller MIDI.

To install from Ubuntu WSL, with the ZIP in
C:\Users\dave-\OneDrive\Desktop\2026:

  cd ~/PokemonDev/pokekyoto
  unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Route1_HQ_V2.zip" -d .
  python3 PokeKyoto_Route1_HQ_V2/install_route1_hq.py . --quality hq --dry-run
  python3 PokeKyoto_Route1_HQ_V2/install_route1_hq.py . --quality hq
  make -j4

Other options:
  --target route101      target MAP_ROUTE101 instead of MAP_ROUTE1
  --no-assign            install the track, assign music yourself in Porymap
  --quality compatible   use lower-burden 13379 Hz wave, no global downgrade
  --force-map-music      explicitly replace an unexpected map music setting

HQ raises the game's GLOBAL mix frequency to 26758 Hz if necessary.
This may cost FPS/CPU and could affect earlier custom PCM songs; known
older title sample definitions are converted from no-resample to
rate-aware voices when enabling HQ. Unknown no-resample voices cause a
safety refusal. HQ PCM itself is always rate-aware.

Backups: .pokekyoto_route1_hq_backups/<timestamp>/
Restore using:
  python3 PokeKyoto_Route1_HQ_V2/restore_route1_hq.py . .pokekyoto_route1_hq_backups/<timestamp>
Then rebuild with make -j4.

Limitations: GBA cannot directly play MP3. The result is NOT identical
stereo studio quality: it is signed 8-bit mono PCM. HQ does preserve more
high-frequency information than the previous 13 kHz version, but costs
more ROM and CPU. Building and listening in an actual mGBA ROM has NOT
yet been validated; test the game, sound effects, loop and framerate.
