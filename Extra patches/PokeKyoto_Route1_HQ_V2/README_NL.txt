POKÉKYOTO – ROUTE 1 MUZIEK, HQ HERZIENING
=============================================

Wat is nieuw?
- Nieuwe GBA-conversie van jouw 'Route 1(1).mp3' (duur 2:12,83).
- De originele orkestratie blijft als opnamemix behouden: strijkers,
  fluiten, belletjes, koperblazers, slagwerk. Geen MIDI-remake.
- Hoge kwaliteit: 26.758 samples/seconde, 8-bit signed PCM, mono.
- Alternatief: 13.379 samples/seconde voor lagere CPU/ROM-belasting.
- Herhaling: de volledige opname loopt automatisch opnieuw.
- Hetzelfde muzieknummer als de eerdere patch: MUS_KYOTO_ROUTE1.
  De eerdere Route 1-versie wordt bijgewerkt, niet dubbel toegevoegd.
- Alleen Route1_Frlg krijgt de muziek. Route 2, Littleroot Town en
  titelschermmuziek blijven ongemoeid. De installer kan de algemene mixer
  wel van 13.379 Hz naar 26.758 Hz verhogen als dat nog niet gedaan is.

BELANGRIJKE BEPERKINGEN
De echte Game Boy Advance speelt deze opname NIET af als MP3: het is
mono, 8-bit PCM. Daardoor klinkt het niet identiek aan de MP3. De HQ-
versie gebruikt 3,39 MiB ROM-ruimte en een hogere globale mengfrequentie.
Dat kan invloed hebben op prestaties (FPS). Met eerdere eigen PCM-tracks
gebruiken we rate-aware voice_definities om verkeerde afspeelsnelheden
te voorkomen. Als andere custom no-resample instrumenten worden ontdekt,
weigert de installer een risicovolle mixerwijziging.

PREVIEW
assets/hq/kyoto_route1_preview.mp3: beluister de HQ GBA-sample.
assets/compatible/kyoto_route1_preview.mp3: de oude GBA-samplefrequentie.
Dit zijn previews van de werkelijke 8-bit audiogegevens, opnieuw als
MP3 opgeslagen om eenvoudig op Windows te beluisteren. Echte mGBA-weergave
kan afwijken door de GBA-mixer, emulatorinstellingen en DAC-filtering.

INSTALLATIE (Ubuntu WSL)
1. Zet PokeKyoto_Route1_HQ_V2.zip in:
   C:\Users\dave-\OneDrive\Desktop\2026

2. Open Ubuntu en voer uit:
   cd ~/PokemonDev/pokekyoto
   unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Route1_HQ_V2.zip" -d .

3. Doe altijd eerst de veiligstellende controle:
   python3 PokeKyoto_Route1_HQ_V2/install_route1_hq.py . --quality hq --dry-run

4. Installeer:
   python3 PokeKyoto_Route1_HQ_V2/install_route1_hq.py . --quality hq

5. Bouw:
   make -j4

6. Test de ROM in mGBA: muziek op Route 1, overstap Route 2,
   terug naar Littleroot, titelscherm, battles, weeranimaties en FPS.

KAARTKEUZE
De standaardkeuze is het FireRed/Kanto-routegebied in de Emerald-
broncode: data/maps/Route1_Frlg/map.json, ID MAP_ROUTE1.
Wil je juist de Emerald Route101 gebruiken, voeg dan aan beide
python3-commando's toe: --target route101

ALLEEN KLAARZETTEN ZONDER AUTOMATISCH TOEWIJZEN
Voeg toe: --no-assign
Selecteer MUS_KYOTO_ROUTE1 daarna handmatig in Porymap op je map.

COMPACTE VERSIE / FALLBACK
Installeer desgewenst met --quality compatible (13.379 Hz sample).
Die optie verlaagt een eerder geïnstalleerde globale HQ-mixer niet,
omdat dit de Littleroot-HQ-muziek zou beïnvloeden.

BACK-UP EN HERSTEL
De installer maakt een back-up van ieder gewijzigd bestaand bestand:
  .pokekyoto_route1_hq_backups/<tijdstip>/
Kopieer het volledige back-uppad van het scherm en herstel met:
  python3 PokeKyoto_Route1_HQ_V2/restore_route1_hq.py . .pokekyoto_route1_hq_backups/<tijdstip>
Daarna opnieuw 'make -j4'.

LET OP
De installer en audioformaten zijn offline gecontroleerd. Een volledige
PokéKyoto ROM-build en echte mGBA-test zijn NIET in dit pakket uitgevoerd.
Als je een buildfout krijgt, stuur de foutmelding; forceer niets.
