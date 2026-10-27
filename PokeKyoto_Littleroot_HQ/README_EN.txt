POKEKYOTO | LITTLEROOT TOWN | FULL-ORCHESTRA AUDIO PACK
======================================================
Source: user-supplied Town.mp3, duration approx. 157.21 seconds.
No change in tempo or pitch; no reconstructed MIDI instrument arrangement.

HQ (default): signed 8-bit mono PCM at 26,758 Hz. More orchestral detail,
about 4.01 MiB of ROM data, but changes the GLOBAL Emerald audio mixer
from 13,379 to 26,758 Hz, potentially reducing FPS on demanding maps.
Compatible: 13,379 Hz signed 8-bit mono PCM, about 2.01 MiB, unchanged
mixer speed but less high-frequency detail. Both include listening previews.
Standard Emerald/M4A audio cannot reproduce MP3 stereo or original fidelity.

Installation (from Windows ZIP downloaded to Desktop/2026, in Ubuntu WSL):

cd ~/PokemonDev/pokekyoto
unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Littleroot_HQ.zip" -d .
python3 PokeKyoto_Littleroot_HQ/install_littleroot.py . --quality hq --dry-run
python3 PokeKyoto_Littleroot_HQ/install_littleroot.py . --quality hq
make -j4

Run the newly-built GBA ROM in mGBA and test Littleroot Town, the song loop,
transitions, title music, battle sound, weather and frame rate.
If HQ adversely affects performance, revert to the original mixer frequency:
python3 PokeKyoto_Littleroot_HQ/install_littleroot.py . --quality compatible
make -j4

The installer appends a song called MUS_KYOTO_LITTLEROOT with a dynamic
song ID without changing existing IDs, and ONLY assigns it to:
data/maps/LittlerootTown/map.json (MAP_LITTLEROOT_TOWN).
On GitHub the existing music for this map was MUS_RG_CELADON. If your local
map music was changed to a different value, the installer stops safely;
use --force-map-music if you explicitly want to overwrite that choice.
To add the song but not assign it, use --no-assign.

If earlier Kyoto title / Route 1 full-length PCM mods are present, HQ
converts their rate-locked voices to rate-aware voices to prevent a speed
change after raising the global mixer rate. Unknown rate-locked voices
cause a safe stop in HQ mode rather than silent corruption. Compatible
mode does not need this change.

Song volume config: sound/songs/midi/midi.cfg : -V115 (max 127).
Wave-header loop covers the whole song, with TIE note + short controller
loop keeping the sample active. The small natural beginning/end fade avoids clicks.

Backup per installation:
.pokekyoto_littleroot_music_backups/<timestamp>/
To restore:
python3 PokeKyoto_Littleroot_HQ/restore_littleroot.py . \
 .pokekyoto_littleroot_music_backups/<timestamp>
make -j4

Audio PCM headers, SHA hashes, MIDI loops and installer behavior were
validated on simulated source checkouts, including earlier title/Route1
patches, reinstall and restore. Full ROM build and actual mGBA playback
were NOT performed here. Actual performance and fidelity require testing.
