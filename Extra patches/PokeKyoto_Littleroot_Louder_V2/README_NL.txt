POKEKYOTO - LITTLEROOT TOWN - LOUDER V2 (27-10-2026)
=====================================================

This replaces ONLY the sample used by MUS_KYOTO_LITTLEROOT. The existing
Littleroot track was too quiet; this version is roughly 5 dB louder on average
(plus a small boost in MIDI song volume from -V115 to -V127).

- Same original Town.mp3 recording: instruments, notes, tempo and 157-second
  duration are preserved. No reconstructed MIDI instruments.
- Dynamics are mildly compressed to raise the body of the sound without
  clipping. The GBA sample remains signed PCM8 mono at 13379 Hz.
- Does not change other songs, IDs or the global GBA mixer rate.
- Safe to run AFTER the previously distributed Littleroot installation patch.
  It updates the existing sample and volume IN PLACE, with a backup.

INSTALLATION (UBUNTU / WSL):
1. Close Opalmap to avoid saving an outdated map assignment.
2. Place PokeKyoto_Littleroot_Louder_V2.zip in
   C:\Users\dave-\OneDrive\Desktop\2026.
3. Execute:

cd ~/PokemonDev/pokekyoto
unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Littleroot_Louder_V2.zip" -d .
python3 PokeKyoto_Littleroot_Louder_V2/install_littleroot.py . --dry-run
python3 PokeKyoto_Littleroot_Louder_V2/install_littleroot.py .
make -j4

4. Restart the game in mGBA, go to Littleroot and listen.
   The chosen music ID should be MUS_KYOTO_LITTLEROOT.
   Check that the volume is comfortable and doesn't distort.

CHECK THE SONG REGISTRATION:
grep -n MUS_KYOTO_LITTLEROOT include/constants/songs.h data/maps/LittlerootTown/map.json
grep -n 'mus_kyoto_littleroot.mid' sound/songs/midi/midi.cfg

RESTORE:
The installer writes previous files to
.pokekyoto_littleroot_music_backups/<timestamp>/.
Original restore tool is included. The audio preview MP3 is created from
exactly the PCM8 used by the game, but played on a PC, not the mGBA mixer.

VALIDATION:
Audio data / ROM PCM format / installer on representative sources checked.
Actual compilation or in-game audio loudness in YOUR current checkout has not
been verified. If custom files have changed, the installer can refuse safely.
