POKÉKYOTO ROUTE 1 — REPAIR V3 (27 OKTOBER 2026)
================================================

WAAROM DE VORIGE INSTALLATIE MISLUKTE
De vorige installer stopte bij standaard Pokémon-drum-samples met
'voice_directsound_no_resample'. Hij maakte geen wijzigingen; een
succesvolle 'make' betekende dus NIET dat Route 1 was geïnstalleerd.

DEZE VERSIE
* Registreert een NIEUW nummer: MUS_KYOTO_ROUTE1, zichtbaar in Porymap na herstart.
* Bewaart de oorspronkelijke opname als een door de GBA ondersteund
  4-bit DPCM-sample, ca. 1,83 MB in ROM (26.758 Hz, volledige ca. 2:13).
* Houdt je Route 2, Littleroot Town en titelscherm ongewijzigd.
* Behoudt de bestaande song-ID's, ook als eerdere muziekpatches zijn geïnstalleerd.
* Maakt automatisch een back-up van elke gewijzigde broncodefile.
* Door gebruik van een .dpcm-bestand blijft het audiobestand aanwezig na 'make clean'.
* Standaard geen wijziging van de algemene geluidsmixer. Dit is veiliger en laat
  de route direct als selecteerbaar nummer verschijnen.

STAP 1 — INSTALLEREN (Ubuntu / WSL)
Download PokeKyoto_Route1_REPAIR_V3.zip naar Windows:
  C:\Users\dave-\OneDrive\Desktop\2026
Open Ubuntu:
  cd ~/PokemonDev/pokekyoto
  unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Route1_REPAIR_V3.zip" -d .
  python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py . --dry-run
  python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py .
  python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py . --check
  make -j4

STAP 2 — MUZIEK ZOEKEN IN PORYMAP
Sluit Porymap en heropen het project. Open de gewenste Route 1-map.
Kies het nummer MUS_KYOTO_ROUTE1 bij de Song-/Music-instellingen.
De installer koppelt het niet automatisch aan een route; dat doe je zelf.

Wil je automatisch koppelen? Alleen indien zeker van de map:
  python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py . --assign route1
     -> Kanto FRLG-route 'Route1_Frlg'
  python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py . --assign route101
     -> Hoenn-route 'Route101' boven Littleroot Town

STAP 3 — OPTIONELE 26-KHZ MIXER VOOR MAXIMALE KWALITEIT
Pokémon Emerald mixt geluid normaal op 13.379 Hz. De originele opname is
hier in 26.758 Hz opgeslagen, maar de volledige hoge frequenties worden pas
hoorbaar bij een 26.758-Hz-mixer. Voor de veilige eerste installatie wordt
de algemene mixer NIET aangepast. Wil je de globale HQ-mixer inzetten:

  python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py . --enable-hq --dry-run
  python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py . --enable-hq
  make -j4

DE OPTIONELE HQ-MIXER KAN ANDERE MUZIEK, PERCUSSIE EN FPS BEÏNVLOEDEN!
De installer converteert 'no-resample' voices naar pitch-afhankelijke voices
om verdubbelde afspeelsnelheid van drums te voorkomen. Hierdoor kan hun
klank wel veranderen. Controleer dit zelf in mGBA. Maak een back-up of
gebruik het meegeleverde restore_route1.py-script indien nodig.

OPMERKINGEN
* Door het compacte DPCM-audioformaat kunnen lichte compressieartefacten
  hoorbaar zijn. Er is geen directe MP3-decodering op de GBA.
* De vorige ongecomprimeerde HQ-sample was ca. 3,55 MB. Dat zou de 32-MB
  GBA-ROM-limiet kunnen overschrijden. Deze versie gebruikt ca. 1,83 MB.
* De installer en bestandsindeling zijn gecontroleerd. Een echte ROM-build
  van jouw volledige lokale project en een mGBA-audiotest zijn NIET gedaan.
* Bewaar de ZIP en maak zo nodig een aparte back-up van je project.

VOOR EEN BACK-UP HERSTELLEN
Na elke installatie toont het script een exact pad in:
  .pokekyoto_route1_backups/...
Herstel de gewenste momentopname met:
  python3 PokeKyoto_Route1_REPAIR_V3/restore_route1.py . <backupmap>
Daarna 'make -j4'.

ALS JE ALLEEN DE LOSSE INSTALLER KUNT DOWNLOADEN
Plaats de oorspronkelijke MP3 op je Windows-bureaublad/2026 en gebruik:
  python3 install_route1.py . --mp3 "/mnt/c/Users/dave-/OneDrive/Desktop/2026/Route 1(1).mp3"
Deze optie gebruikt de lokale ffmpeg en de bestaande PokéKyoto wav2agb-tool om
zelf het compacte GBA-audiobestand te maken; geen ZIP of gebundelde DPCM nodig.
Wanneer ffmpeg ontbreekt, kan Ubuntu dit installeren met:
  sudo apt install ffmpeg
