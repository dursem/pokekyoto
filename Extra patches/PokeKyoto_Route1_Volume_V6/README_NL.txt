POKÉKYOTO – ROUTE 1 VOLUME V6
=================================

Doel: Route 1 duidelijk harder maken zonder het PCM-formaat,
het tempo, de loop, de samplefrequentie of ROM-grootte te veranderen.
Alleen de bestaande Route 1 PCM-sample en de regel voor de Route 1
muziek in midi.cfg worden aangepast.

WERKING
- PCM8 sample: +3,0 dB met een zachte piekbegrenzing
- Route 1 MIDI-mastervolume: naar maximaal -V127
- Geen verandering aan de andere liedjes of algemene geluidsmixer
- Automatische back-up en bescherming tegen tweemaal verhogen
- Verandert geen kaarttoewijzingen of muziek-ID's

INSTALLATIE (Ubuntu / WSL)
1. Download PokeKyoto_Route1_Volume_V6.zip naar Windows:
   C:\Users\dave-\OneDrive\Desktop\2026

2. Voer in Ubuntu uit:
   cd ~/PokemonDev/pokekyoto
   unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Route1_Volume_V6.zip" -d .
   python3 PokeKyoto_Route1_Volume_V6/boost_route1_volume.py . --dry-run
   python3 PokeKyoto_Route1_Volume_V6/boost_route1_volume.py .
   make -j4

3. Test volume en vervorming in mGBA; beluister zacht en hard gedeelte.

TERUGZETTEN
   python3 PokeKyoto_Route1_Volume_V6/boost_route1_volume.py . --restore
   make -j4

LET OP
- Vereist al geïnstalleerde Route 1 PCM V4/V5 muziek.
- Dit is NIET de eerder sterk vervormende DPCM-versie.
- De balans met andere game-muziek moet nog in mGBA worden vergeleken.
- De standaard samplefrequentie van de GBA-game verandert niet.
