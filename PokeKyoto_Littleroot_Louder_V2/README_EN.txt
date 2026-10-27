POKEKYOTO - LITTLEROOT TOWN - LOUDER V2 (27 OCTOBER 2026)
=========================================================

Raises the existing MUS_KYOTO_LITTLEROOT song by about 5 dB average PCM level,
plus a small MIDI volume adjustment (-V115 -> -V127).
No new song ID is created if it already exists. All other music stays as-is.
Original recording, instruments, tempo, duration and global mixer are retained.
Audio peaks are compressed and limited to prevent digital clipping.

SETUP (Ubuntu/WSL):
1. Close Opalmap. Put ZIP at C:\Users\dave-\OneDrive\Desktop\2026.
2. Run:

cd ~/PokemonDev/pokekyoto
unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Littleroot_Louder_V2.zip" -d .
python3 PokeKyoto_Littleroot_Louder_V2/install_littleroot.py . --dry-run
python3 PokeKyoto_Littleroot_Louder_V2/install_littleroot.py .
make -j4

3. Restart mGBA and listen in Littleroot Town.
4. In Opalmap > Littleroot Town > Header, choose MUS_KYOTO_LITTLEROOT.

Already installed the previous package? This installer upgrades the song in
place and keeps its music ID. Previous files are timestamp-backed up in
.pokekyoto_littleroot_music_backups/. The included restore tool can use
those backups. The standalone preview MP3 approximates PCM8 playback on PC;
it is NOT an emulator recording.

VALIDATION: PCM/WAV formats, peak levels, duration, manifest integrity and
updater behaviour on representative project files. Not compiled in your active
Pokekyoto checkout; actual in-game listening remains to be checked.
