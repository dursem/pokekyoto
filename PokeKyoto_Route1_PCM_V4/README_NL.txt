PokéKyoto Route 1 – PCM V4 (kwaliteitsreparatie)
==================================================

Waarom anders?
De vorige V3 gebruikte een DPCM-compressie van 4 bit voor een hele
orkestopname. In de mGBA-opname zijn stevige ruis en vervorming hoorbaar.
Deze versie gebruikt GEEN DPCM, maar een originele, ongecomprimeerde
8-bit mono-opname. De instrumenten worden NIET naar MIDI omgezet.

Inbegrepen:
- route1_pcm21024.bin: 21.024 Hz, 2,79 MB, aanbevolen
- route1_pcm18157.bin: 18.157 Hz, 2,41 MB, bij beperkte ROM-ruimte
- Previews zijn LOS te beluisteren via de links in ChatGPT.
  Ze zijn daadwerkelijk uit de PCM-data gemaakt, niet uit de MP3.
- installer, herstelprogramma en MIDI-controller

De standaardinstallatie wijzigt NIET:
- het volume van Route 1 of andere nummers
- je globale mixerfrequentie en GBA-drum/geluidseffecten
- jouw huidige mapmuziek/toewijzingen
- Littleroot Town of het titelscherm

INSTALLATIE (Ubuntu/WSL)
-------------------------
1. Sla PokeKyoto_Route1_PCM_V4.zip op in:
   C:\Users\dave-\OneDrive\Desktop\2026
2. Voer uit:

   cd ~/PokemonDev/pokekyoto
   unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Route1_PCM_V4.zip" -d .
   python3 PokeKyoto_Route1_PCM_V4/install_route1_pcm.py . --dry-run
   python3 PokeKyoto_Route1_PCM_V4/install_route1_pcm.py .
   python3 PokeKyoto_Route1_PCM_V4/install_route1_pcm.py . --check
   make -j4

3. Open mGBA en test muziek op je map. De registratie heet
   MUS_KYOTO_ROUTE1. Als je het zelf aan een map wilt toewijzen, herstart
   Porymap en kies deze song. Bestaande toewijzingen blijven ongewijzigd.

ROM TE GROOT?
--------------
Deze versie vraagt meer ROM-ruimte dan de oude DPCM. GBA-limiet is
32 MiB. Als make/linken meldt dat het niet past, kies de compacte
PCM-versie (nog steeds zonder DPCM):

   python3 PokeKyoto_Route1_PCM_V4/install_route1_pcm.py . --quality compact
   make -j4

De oudere DPCM wordt na het overschakelen NIET meer in de ROM ingebouwd
omdat de .incbin-verwijzing nu naar route1_pcm.bin gaat. Het oude bestand
kan op schijf blijven staan zonder extra ROM-kosten.

GELUIDSKWALITEIT
----------------
- Een echte GBA kan GEEN CD-stereo/MP3 direct afspelen met de standaard
  M4A-engine; deze sample is 8-bit mono en dus beperkt.
- Bij een mixer van slechts 13.379 Hz kan het spel minder hoge tonen
  weergeven, zelfs wanneer het bronbestand 21.024 Hz is.
- De installer verandert de mixinstelling bewust niet. Als je voor
  Littleroot al een hogere mixer hebt, wordt die behouden.
- Volume-afstemming doen we pas NADAT je de nieuwe klank hebt beoordeeld.
- De MP3-previews laten de PCM-bron horen, niet bewezen mGBA-uitvoer.

Herstellen:
Het installatieprogramma bewaart alleen gewijzigde bestanden in
.pokekyoto_route1_pcm_v4_backups/ met een manifest. Herstel met:

   python3 PokeKyoto_Route1_PCM_V4/restore_route1_pcm.py . --latest

Test in mGBA moet op je eigen actuele project nog plaatsvinden.

GEEN ZIP DOWNLOAD MOGELIJK?
---------------------------
Download alleen PokeKyoto_Route1_PCM_V4_losse_installer.py. Zet ook
je eigen originele Route 1(1).mp3 in C:\Users\dave-\OneDrive\Desktop\2026.
In Ubuntu in je PokéKyoto-project:

   python3 "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Route1_PCM_V4_losse_installer.py" . --mp3 "/mnt/c/Users/dave-/OneDrive/Desktop/2026/Route 1(1).mp3"
   make -j4

De losse installer bevat al zijn eigen MIDI-controller en maakt uit
jouw originele MP3 rechtstreeks een ongecomprimeerde GBA-sample.
Installatie en de aanpassing aan het volume blijven verder hetzelfde.
