POKEKYOTO ROUTE 1 - PCM V5 / ROM SIZE FIX (NEDERLANDS)
========================================================

Deze patch repareert de fout:
  arm-none-eabi-ld: region `ROM' overflowed by 94400 bytes

Deze patch is gemaakt VOOR een project waarin de eerdere
"PokeKyoto_Route1_PCM_V4"-installer al succesvol is gedraaid.

Het bestand data/kyoto_audio/route1_pcm.bin wordt vervangen door
iets compactere ONGEcomprimeerde signed 8-bit PCM-audio.
Het volledige nummer, de loop, het volume en het tempo blijven gelijk.
De geluidsengine, de MIDI-controller, muziek-ID's en de maptoewijzingen
worden NIET aangepast. Geen DPCM-compressie.

De versie `safe` heeft 19500 Hz en bespaart ongeveer 202428 bytes.
Op basis van de screenshot zou dit het tekort van 94400 bytes
oplossen en circa 108028 bytes vrij laten.

Er is ook een `maximum`-variant op 20000 Hz. Die bespaart slechts
136016 bytes en laat volgens de screenshot circa 41616 bytes over.
Kies standaard `safe` om ruimte voor andere game-data over te houden.

INSTALLATIE (Ubuntu / WSL):
-------------------------
1) Download PokeKyoto_Route1_PCM_ROMFIX_V5.zip in
   C:\Users\dave-\OneDrive\Desktop\2026

2) Open Ubuntu, voer uit:

   cd ~/PokemonDev/pokekyoto
   unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Route1_PCM_ROMFIX_V5.zip" -d .
   python3 PokeKyoto_Route1_PCM_ROMFIX_V5/install_route1_romfix.py . --dry-run
   python3 PokeKyoto_Route1_PCM_ROMFIX_V5/install_route1_romfix.py .
   make -j4

3) Controleer daarna in mGBA de nieuwe ROM.

CONTROLE:
   python3 PokeKyoto_Route1_PCM_ROMFIX_V5/install_route1_romfix.py . --check

20-KHZ-VERSIE (iets hogere hoge-tonenweergave, minder vrije ruimte):
   python3 PokeKyoto_Route1_PCM_ROMFIX_V5/install_route1_romfix.py . --quality maximum
   make -j4

TERUGZETTEN:
   python3 PokeKyoto_Route1_PCM_ROMFIX_V5/install_route1_romfix.py . --restore
   make -j4
   LET OP: terugzetten naar 21024 Hz kan de 32-MiB-limiet weer overschrijden.

AUDIO:
 - Gebaseerd op het originele Route 1(1).mp3-bestand.
 - Dezelfde mix, looptijd en instrumentatie als V4.
 - Signed 8-bit PCM, mono, GEEN DPCM.
 - Er is geen verandering in muziekvolume of globale mixer.
 - Ook bij 19500 Hz blijft de GBA-uitvoer lager van kwaliteit dan MP3.
 - Deze patch is getest als bestandsupdate, maar nog niet in een volledige
   actuele ROM / mGBA op de computer van de gebruiker.
