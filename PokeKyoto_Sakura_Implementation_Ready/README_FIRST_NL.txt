POKÉKYOTO — SAKURA WEATHER IMPLEMENTEREN
========================================

Dit pakket is afgestemd op de huidige GitHub master die tijdens deze chat is
gecontroleerd: 4dcb7048ecaa400f11927f07c5528b726ddb5b1e.

Wat wordt geïmplementeerd
-------------------------
- WEATHER_DOWNPOUR / ID 13 wordt Sakura Petals.
- Alias: WEATHER_SAKURA_PETALS.
- 24 kleine sakura-petals met de goedgekeurde 9 poses.
- De drie roze kleuren volgen de roze boomkleuren uit General palette 3,
  indices 9/10/11, dus ook de bestaande day/night-pipeline.
- Bij het verlaten: onmiddellijk stoppen met nieuwe/recycle particles.
- Geen ordered dithering en geen globale BLDALPHA fade.
- Bestaande petals bewegen normaal op de oude map.
- Zodra de nieuwe map werkelijk geladen is, gaan alleen de overgebleven
  particles 1,6x sneller uit beeld.
- Menu/Bag-resume bewaart de resterende outgoing population.
- Downpour telt niet meer als rain voor battle/weather mechanics.
- Kyogre/Groudon stormscripts gebruiken Rain Thunderstorm, zodat die storms
  geen Sakura worden.
- Autumn Wind ID 20 zit mee in dezelfde geteste weather-module.

Installeren in Ubuntu / WSL
---------------------------
1. Pak deze ZIP uit, bijvoorbeeld in ~/Downloads/PokeKyoto_Sakura_Implementation_Ready
2. Voer uit:

   cd ~/Downloads/PokeKyoto_Sakura_Implementation_Ready
   ./install_sakura_weather.sh ~/PokemonDev/pokekyoto

De installer:
- maakt lokaal branch feature/sakura-weather-20261024;
- doet eerst git apply --check;
- past daarna beide patches toe;
- doet git diff --check;
- draait make -j$(nproc).

Hij forceert geen patch als jouw relevante bestanden afwijken.
Onverwante lokale map/art-wijzigingen mogen blijven staan.

Testen
------
Open daarna je project in OpalMap/Porymap en zet een testmap op:
  WEATHER_DOWNPOUR
of kies Sakura Petals als de editor de alias toont.

Debug menu:
  houd B vast + druk START
  Utilities -> Set weather -> SAKURA PETALS / ID 13

Als je tevreden bent:
  cd ~/PokemonDev/pokekyoto
  git status
  git add -A
  git commit -m "Add Sakura petals weather"
  git push -u origin feature/sakura-weather-20261024

Belangrijk
----------
De ChatGPT GitHub-verbinding kon de repository en actuele commit lezen, maar
GitHub gaf 403 op branch/ref writes. Daarom is niets half naar remote master
gepusht; deze lokale installer is de veilige implementatieroute.
