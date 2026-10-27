PokéKyoto Route 1 – PCM V4 audio quality repair
==============================================

V3 used lossy 4-bit DPCM on the full orchestra recording; the user's mGBA
video contains significant unwanted high-frequency noise. This release
replaces that compression with UNCOMPRESSED signed 8-bit mono PCM audio.
Original orchestral arrangement, tempo, duration and instruments preserved.
No MIDI instrument reconstruction.

Recommended: 21,024 Hz PCM (2.79 MB). Alternative: 18,157 Hz (2.41 MB).
Previews are encoded from the ACTUAL exported PCM samples; they are not
claimed to be recordings of the ROM running in mGBA.

INSTALL (Ubuntu/WSL, project at ~/PokemonDev/pokekyoto)
  cd ~/PokemonDev/pokekyoto
  unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Route1_PCM_V4.zip" -d .
  python3 PokeKyoto_Route1_PCM_V4/install_route1_pcm.py . --dry-run
  python3 PokeKyoto_Route1_PCM_V4/install_route1_pcm.py .
  python3 PokeKyoto_Route1_PCM_V4/install_route1_pcm.py . --check
  make -j4

To install smaller 18,157 Hz variant if the GBA ROM exceeds 32 MiB:
  python3 PokeKyoto_Route1_PCM_V4/install_route1_pcm.py . --quality compact
  make -j4

This patch preserves existing MUSIC VOLUME (MIDI configuration), global
mixer frequency (src/m4a.c) and map music assignments. The existing song
name MUS_KYOTO_ROUTE1 is reused without creating new duplicate music IDs.
It works both when V3 is installed and when the song is not registered yet.
Backups are created automatically. Restore latest:
  python3 PokeKyoto_Route1_PCM_V4/restore_route1_pcm.py . --latest

The old DPCM sample may remain on disk, but is no longer referenced by
sound/direct_sound_data.inc and therefore won't take up ROM space.

A standard GBA sound engine is limited to 8-bit mono and its mixing rate.
In particular, a global 13,379 Hz mixer still limits playback bandwidth.
Do not change global mixer sound settings without validating game sound
and frame rate. ROM playback must still be tested in mGBA.

Audio MP3 previews are distributed as separate links to reduce ZIP size.

If ZIP download fails, use PokeKyoto_Route1_PCM_V4_losse_installer.py
with your original Route 1(1).mp3 and ffmpeg in Ubuntu:
  python3 /mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Route1_PCM_V4_losse_installer.py . --mp3 "/mnt/c/Users/dave-/OneDrive/Desktop/2026/Route 1(1).mp3"
This standalone script contains the MIDI controller and generates new PCM.
