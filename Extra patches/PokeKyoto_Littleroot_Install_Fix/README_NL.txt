POKEKYOTO - LITTLEROOT TOWN - MUSIC INSTALL FIX
================================================
Dit pakket voegt de originele orkestopname uit Town.mp3 toe als nieuw lied:
MUS_KYOTO_LITTLEROOT. Het vervangt geen andere audio. Het wijst alleen
LittlerootTown toe aan deze song en maakt vooraf een backup.

WAAROM DE EERDERE VERSIE NIET LUKTE
----------------------------------
MUS_LITTLEROOT_TEST is een bestaande oude testmuziek-ID en NIET jouw
nieuwe geïmporteerde nummer. De HQ-installer kan op het originele Kyoto-project
stoppen bij bestaande no-resample drumstemmen, voordat muziek is toegevoegd.
Deze FIX-installer gebruikt de beproefde compatible-optie met de originele
13.379 Hz mixer, zodat title, Route 1 en andere geluiden niet versnellen.
De volledige melodie en instrumenten zitten in de originele audio-opname
(8-bit mono voor GBA); hogere frequenties/stereo zijn beperkt.

INSTALLEREN - IN UBUNTU/WSL
---------------------------
1. SLUIT Opalmap volledig af (anders kan een oude open kaart je instellingen
   opnieuw overschrijven). Je hoeft de foutieve MUS_LITTLEROOT_TEST-selectie
   niet op te slaan.
2. Download dit ZIP-bestand naar C:\Users\dave-\OneDrive\Desktop\2026.
3. Plak deze opdrachten in Ubuntu, een voor een:

cd ~/PokemonDev/pokekyoto
unzip -o "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Littleroot_Install_Fix.zip" -d .
python3 PokeKyoto_Littleroot_Install_Fix/install_littleroot.py . --dry-run
python3 PokeKyoto_Littleroot_Install_Fix/install_littleroot.py .
grep -n 'MUS_KYOTO_LITTLEROOT' include/constants/songs.h data/maps/LittlerootTown/map.json
make -j4

4. Open Opalmap opnieuw en laad je project; kijk onder LittlerootTown > Header > Song.
   Het resultaat moet MUS_KYOTO_LITTLEROOT zijn, niet MUS_LITTLEROOT_TEST.
5. Test de net gebouwde pokeemerald.gba in mGBA in Littleroot, Route 1,
   titel, battles en met weather/muziekovergangen.

Als de dry-run een andere bestaande mapmuziek meldt en uit veiligheid stopt,
controleer eerst welke muziek de kaart nu gebruikt. Alleen als je zeker weet
 dat je de bestaande mapmuziek wilt vervangen, gebruik --force-map-music.

BEVESTIGING
-----------
Je ziet twee regels bij de grep-controle: een regel in songs.h en
 een in map.json. De dynamische song-ID is doorgaans 610 (of 611 als een
 eerdere Route1-update geïnstalleerd is). Verander nooit bestaande nummers.

BACKUPS
-------
.pokekyoto_littleroot_music_backups/<timestamp>/ bevat vorige bestanden.
De hersteltool restore_littleroot.py zit in dit ZIP. Audio, installatie en
registratie zijn op een broncodekopie getest; volledige ROM/mGBA-weergave
in je ACTUELE lokale project moet na het bouwen nog worden bevestigd.
