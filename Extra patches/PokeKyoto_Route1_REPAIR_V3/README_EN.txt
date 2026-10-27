POKÉKYOTO ROUTE 1 — REPAIR V3 (27 OCT 2026)
=============================================

WHY THE PREVIOUS INSTALLER FAILED
It aborted when encountering built-in Pokemon drum voices marked
voice_directsound_no_resample, leaving the new song completely unregistered.
Running make afterward only rebuilt the previous game.

INSTALL SAFELY (DEFAULT)
Copy PokeKyoto_Route1_REPAIR_V3.zip to C:\Users\dave-\OneDrive\Desktop\2026
Then in Ubuntu/WSL:
  cd ~/PokemonDev/pokekyoto
  unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Route1_REPAIR_V3.zip" -d .
  python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py . --dry-run
  python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py .
  python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py . --check
  make -j4

The song is MUS_KYOTO_ROUTE1. Close and reopen Porymap to refresh the Song
list, then assign it to the desired map yourself. Maps are not changed by
default. It is not the built-in Route 1 track MUS_RG_ROUTE1.

The full ~2:13 orchestra recording has been packed into GBA DPCM audio
(26,758 Hz source; 1.83 MiB in the ROM). It loops and does not shift song IDs.
The DPCM file has a .dpcm extension so that make clean does not delete it.
Existing Route 2, Littleroot and title music are unchanged.

OPTIONAL GLOBAL 26-KHZ MIXER:
  python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py . --enable-hq --dry-run
  python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py . --enable-hq
  make -j4

WARNING: A higher global GBA mixer frequency increases CPU use and may
change percussion / other sound effects. The HQ option converts all
fixed-rate voices (type 8) to pitch-aware type 0 to prevent obvious 2x speed,
but their sound might still change. Test in mGBA; make an independent project
backup first. Do not claim audio quality or FPS is verified until tested.

Optional direct map assignment:
  --assign route1      selects Route1_Frlg (Kanto Route 1)
  --assign route101    selects Route101 (north of Littleroot in Hoenn)

Use --check to verify registration and the printed backup folder to restore
with restore_route1.py.

This package has been checked for file integrity and installer logic. It has
NOT been built and played on the user's actual ROM in mGBA.

STANDALONE .PY + ORIGINAL MP3 FALLBACK
Put the original MP3 on your Windows Desktop/2026 and run:
  python3 install_route1.py . --mp3 "/mnt/c/Users/dave-/OneDrive/Desktop/2026/Route 1(1).mp3"
This uses local ffmpeg and the project's wav2agb to produce DPCM audio,
without needing the prepacked ZIP assets.
