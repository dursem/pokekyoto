POKEKYOTO | LITTLEROOT TOWN | ORIGINELE ORKESTMUZIEK
==================================================
Bron: Town.mp3 (jouw eigen upload)
Duur: circa 2:37,21. Zelfde tempo en melodie, geen MIDI-remake.

KIES OP BASIS VAN GELUIDSKWALITEIT
--------------------------------
HQ (aanbevolen om eerst te proberen):
- 26.758 Hz, 8-bit, mono (GBA PCM DirectSound), ca. 4,01 MiB ROM-ruimte.
- Bewaart meer detail in de fluiten, strijkers, belletjes en percussie.
- Verandert de ALGEMENE GBA-geluidsmixfrequentie van 13.379 naar 26.758 Hz.
  Daardoor kan de CPU-belasting toenemen, ook tijdens gevechten of met
  uitgebreide tilesets/weer-effecten. Controleer FPS en audio in mGBA.
- Kent de eerdere Kyoto-title en Kyoto-Route1 full-sample audio upgrades:
  als deze in je broncode staan, worden hun 'no_resample' stemmen omgezet
  naar normale sample-rate-aware stemmen om hun snelheid te behouden.
- Andere onbekende vaste-frequentiestemmen? HQ wordt dan VOOR wijziging
  geweigerd, om problemen te voorkomen.

COMPATIBLE (veilige uitwijkoptie):
- 13.379 Hz, 8-bit, mono; circa 2,01 MiB ROM-ruimte.
- Houdt de oorspronkelijke mixerfrequentie aan.
- Klinkt doffer / minder gedetailleerd dan HQ op de originele GBA.

DE MUZIEK ZIT ALS COMPLETE ORKESTOPNAME IN DE AUDIO-SAMPLE.
Alle instrumenten en partijen blijven dus hoorbaar. GBA-audio is wel mono,
8-bit PCM en minder helder dan de MP3. De originele stereo-ruimtelijkheid
kan niet behouden blijven met de standaard Pokémon Emerald-muziekengine.

HOE INSTALLEER JE DE MUZIEK IN JOUW GITHUB-GEKOPPELDE POKEKYOTO?
----------------------------------------------------------------
1. Download 'PokeKyoto_Littleroot_HQ.zip' naar:
   C:\Users\dave-\OneDrive\Desktop\2026
2. Open Ubuntu (WSL) en plak onderstaande regels:

   cd ~/PokemonDev/pokekyoto
   unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Littleroot_HQ.zip" -d .
   python3 PokeKyoto_Littleroot_HQ/install_littleroot.py . --quality hq --dry-run
   python3 PokeKyoto_Littleroot_HQ/install_littleroot.py . --quality hq
   make -j4

3. Open de nieuw gebouwde ROM (gewoonlijk 'pokeemerald.gba') in mGBA.
4. Ga naar Littleroot Town en test het afspelen, de song-loop, overgang
   naar Route 101, menu-, battle- en weather-effecten. Controleer FPS.

HQ GEEFT HAPERINGEN OF FPS-PROBLEMEN?
------------------------------------
Schakel eenvoudig over naar de conservatieve 13-kHz-versie:

   cd ~/PokemonDev/pokekyoto
   python3 PokeKyoto_Littleroot_HQ/install_littleroot.py . --quality compatible
   make -j4

ZELF MUZIEK KIEZEN IN PORYMAP?
------------------------------
Nieuwe eigen muziekconstant: MUS_KYOTO_LITTLEROOT
Nieuwe muzieknaam: mus_kyoto_littleroot
Het ID wordt veilig na de bestaande muziek toegevoegd: normaal 610, of
611 als je eerdere Route 1-pakket al geïnstalleerd is. ID is dynamisch.
Als je liever de map zelf instelt: geef bij het installeren --no-assign mee.

WELKE MAP WORDT AANGEPAST?
--------------------------
- data/maps/LittlerootTown/map.json (MAP_LITTLEROOT_TOWN), muziek ->
  MUS_KYOTO_LITTLEROOT.
- Volgens je huidige GitHub-project had de map eerst MUS_RG_CELADON.
- De gedeelde Celadon-muziek zelf blijft bestaan, net als die van alle
  andere routes, steden, het Pokémon Center en het titelscherm.
- Als de map bij jou intussen een ander muzieknummer heeft, stopt de
  installer bewust zonder wijzigingen. Gebruik dan na beoordeling:
  --force-map-music

VOLUME EN HERHALING
-------------------
- Muziekconfiguratie in sound/songs/midi/midi.cfg: -V115 (max 127).
- Je kunt de waarde later bijstellen, gevolgd door make -j4.
- Herhaling: GBA-waveheader looppoint 0 -> einde; de MIDI bevat een
  TIE-notitie en een korte GOTO-loop zodat de volledige sample doorloopt.
- Natuurlijk rustig einde en begin uit de opname, minieme fade om clicks
  te voorkomen. Tempo en toonhoogte niet veranderd.
- De opname is ca. 2:37. Als je de map verlaat, stopt de muziek zoals
  normaal via de Emerald-audiocode (te controleren in mGBA).

BACK-UP EN TERUGZETTEN
----------------------
De installatie maakt voor elke wijziging eerst een back-up in:
.pokekyoto_littleroot_music_backups/<tijdstempel>/

Om de meest recente installatie terug te zetten, zoek eerst de laatste map:
  ls -td .pokekyoto_littleroot_music_backups/* | head -1

Voer daarna uit (vervang <tijdstempel> door de getoonde mapnaam):
  python3 PokeKyoto_Littleroot_HQ/restore_littleroot.py . \
    .pokekyoto_littleroot_music_backups/<tijdstempel>
  make -j4

BESTANDEN IN DE ZIP
------------------
- install_littleroot.py / restore_littleroot.py
- assets/hq/kyoto_littleroot.wav en .bin
- assets/compatible/kyoto_littleroot.wav en .bin
- assets/hq/kyoto_littleroot_preview.mp3 (preview van werkelijk 8-bit PCM)
- assets/compatible/kyoto_littleroot_preview.mp3
- assets/mus_kyoto_littleroot.mid (alleen besturing van afspelen)
- manifest.json (checksums), README_NL, README_EN

TESTSTATUS
----------
De audio- en GBA-sampleheaders, hashes, MIDI-controller en installatie-
scenarios (dry-run, HQ, compatible, reinstall, restore, oude titel/Route1,
onbekende no_resample-voice, route-veiligheid) zijn getest.
De werkelijke volledige PokéKyoto-ROM kon hier niet worden gebouwd of
in mGBA beluisterd: hiervoor is jouw checkout/build-toolchain nodig.
Daarom geen garantie voor 26k-HQ prestaties op echte hardware.
