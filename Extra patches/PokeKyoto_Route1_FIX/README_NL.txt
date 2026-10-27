POKÉKYOTO ROUTE 1 – DOWNLOADFIX / HQ MUZIEK
=============================================

Dit pakket is bewust klein gehouden. Alleen de GBA-geschikte 8-bit WAV,
MIDI-controller en installer zitten erin. De installer genereert ook de
benodigde GBA .bin. De WAV blijft in het project voor een volgende `make clean`.

Wat het doet:
- maakt MUS_KYOTO_ROUTE1 als aparte muziek-ID (niet de oude Kanto-muziek);
- 26.758 Hz, mono, 8-bit; volledige originele orkestopname van 2:12,83;
- looping en de originele snelheid behouden;
- werkt ook als de oude Route 1-patch al gedeeltelijk aanwezig is;
- maakt back-ups van bestanden die worden aangepast;
- laat de koppeling aan een kaart standaard aan jou over (Porymap);
- past de mixer zo nodig aan naar 26.758 Hz, zoals bij Littleroot HQ;
- verandert de muziek van Littleroot Town en het titelscherm niet.

INSTALLEREN (Ubuntu / WSL)
1. Sla PokeKyoto_Route1_FIX.zip op in de Windows-map Desktop\2026.
2. In Ubuntu:
   cd ~/PokemonDev/pokekyoto
   unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Route1_FIX.zip" -d .
   python3 PokeKyoto_Route1_FIX/install_route1.py . --dry-run
   python3 PokeKyoto_Route1_FIX/install_route1.py .
   grep MUS_KYOTO_ROUTE1 include/constants/songs.h
   make -j4
3. Sluit Porymap volledig en open je project opnieuw.
   Ga naar jouw Route 1 -> Header -> Song -> MUS_KYOTO_ROUTE1.

WIL JE AUTOMATISCH TOEWIJZEN?
  .../install_route1.py . --assign route1   (Kanto: Route1_Frlg)
  .../install_route1.py . --assign route101 (Hoenn: Route101)
Standaard wordt NIETS op kaarten toegewezen, dus jij kiest zelf.

ALS DE ZIP-NIET WERKT, EN JE HEBT DE MP3 OP WINDOWS:
Gebruik de los verkrijgbare installer .py in de projectmap met:
  python3 install_route1.py . --mp3 '/mnt/c/Users/dave-/OneDrive/Desktop/2026/Route 1(1).mp3'
Deze route heeft ffmpeg nodig; installeren in Ubuntu indien niet aanwezig:
  sudo apt update && sudo apt install -y ffmpeg
De MP3 hoeft niet naar ChatGPT te worden gedownload; hij staat dan al op je PC.

CONTROLE
  grep -n 'MUS_KYOTO_ROUTE1' include/constants/songs.h
  ls -lh sound/direct_sound_samples/kyoto_route1.bin
  grep -n 'mus_kyoto_route1' sound/songs/midi/midi.cfg

Dit pakket is getest op de installerlogica en de GBA-audioheader. Een echte
ROM-build met jouw lokale project en een volledige mGBA-luistertest zijn
nog niet uitgevoerd. Let op de globale GBA-mixer en 32 MB ROM-limiet.
