POKEKYOTO - LITTLEROOT TOWN - MUSIC INSTALL FIX
===============================================
Adds the COMPLETE original Town.mp3 recording to PokeKyoto as a new song,
MUS_KYOTO_LITTLEROOT. Does not overwrite any existing song IDs.
The old MUS_LITTLEROOT_TEST is a built-in test track, not this import.

The original HQ installer can stop on stock no-resample drumsets in the game
source. This FIX uses the verified 13,379 Hz compatible PCM version so the
GLOBAL audio mixer and existing sound effects remain unchanged. Original
instrument performances are preserved in the audio sample, but GBA mono/8-bit
13-kHz playback cannot retain MP3 fidelity.

To install:
1. Close Opalmap before changing source files (do not save the incorrect song).
2. Place PokeKyoto_Littleroot_Install_Fix.zip in Windows Desktop\2026.
3. Run in Ubuntu WSL:

cd ~/PokemonDev/pokekyoto
unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Littleroot_Install_Fix.zip" -d .
python3 PokeKyoto_Littleroot_Install_Fix/install_littleroot.py . --dry-run
python3 PokeKyoto_Littleroot_Install_Fix/install_littleroot.py .
grep -n 'MUS_KYOTO_LITTLEROOT' include/constants/songs.h data/maps/LittlerootTown/map.json
make -j4

4. Restart Opalmap; verify the Song field is MUS_KYOTO_LITTLEROOT.
5. Test the rebuilt game in mGBA, including title, Route 1, maps and battles.

The installer recognizes existing MUS_LITTLEROOT_TEST and MUS_LITTLEROOT map
assignments, preserves other music, and creates timestamped backups. It
refuses unknown map settings unless --force-map-music is explicitly specified.

Restore: PokeKyoto_Littleroot_Install_Fix/restore_littleroot.py
(see the original backup folder for its timestamp).

Validated: dry run and source modifications on a representative PokeKyoto
source snapshot. Actual current user checkout and GBA/mGBA build NOT tested.
