# PokéKyoto — ROM-ruimte & FRLG-NPC's (FIX V2)

## Correctie V2: `Ambiguous object dependency: sPicTable_Pidgey`

De eerste installer las ook de aparte Pokémon-followertabellen als NPC-data.
Daarin staan statische namen die ook in de oude FRLG-objecttabellen voorkomen.
V2 houdt de twee follower-headers buiten de NPC-afhankelijkheidscontrole. Deze
headers worden niet verwijderd of herschreven. Pidgey blijft ingeschakeld.
Echte dubbele definities binnen de NPC-bron blijven een reden om veilig te stoppen.

V1 stopte bij deze melding tijdens `source_plan`, vóór het schrijven van wijzigingen
en vóór zijn vergelijkingsbuilds. Voor precies deze fout is dus geen rollback nodig.

**Getest:** dezelfde fout met V1 gereproduceerd in een representatieve testbron;
V2 verwerkt die testbron wel. Alle 15 installertests slagen, inclusief controles
voor behoud van de follower-headers, terugrollen en conflicten in echte NPC-data.
**Niet getest:** volledige PokéKyoto-compilatie en mGBA-gameplay in deze omgeving.

Sla `PokeKyoto_ROM_Ruimte_FRLG_FIX_V2.zip` op in je Windows-map
`OneDrive/Desktop/2026`. Open Ubuntu en voer uit:

```bash
cd /home/dave-/PokemonDev/pokekyoto &&
python3 -m zipfile -e /mnt/c/Users/*/OneDrive*/Desktop/2026/PokeKyoto_ROM_Ruimte_FRLG_FIX_V2.zip . &&
python3 PokeKyoto_ROM_Ruimte_FRLG/kyoto_optimize.py . --build-compare --jobs 2
```

Controleer dat de uitvoer begint met `Installer: kyoto-space-frlg-20261028-v2`.
De ZIP vervangt uitsluitend de installerbestanden in `PokeKyoto_ROM_Ruimte_FRLG`;
de normale broncode wordt pas door het script aangepast, na een geslaagde baseline.

De meest recente gedeelde buildscreenshot meldt **33.448.260 bytes gebruikt**
van **33.554.432 bytes**. Dat betekent **106.172 bytes (103,68 KiB) vrij** vóór
optimalisatie. Dit is een andere, vollere build dan de eerder onderzochte ROM.
De nieuwe besparing wordt pas na twee geslaagde builds als `NET SAVING` afgedrukt.

De aparte `git push`-fout op de screenshot staat los van deze installer. Een push
is niet nodig om de lokale voor/na-build uit te voeren.

## Status op 28 oktober 2026

Dit is een **broncode-installatiepakket, geen geteste GBA-ROM**. De GitHub-verbinding
weigerde het aanmaken van een branch met HTTP 403. Daarom is er niets op GitHub
gewijzigd. De volledige PokéKyoto-build en mGBA-gameplaytest zijn niet uitgevoerd.
De meegeleverde tests gebruiken kleine synthetische bronbestanden; dat is geen
vervanging voor compileren of testen van de volledige game.

Referentiebron: `dursem/pokekyoto`, commit
`add41daf166fdd251b1bb14e7b1dc22c3ed3b61b` (Route 1-update).
Het script controleert de structuur van de **lokale** broncode voordat het iets
schrijft. Het vervangt geen bronboom door een oude versie.

## Veranderingen

- Mega-vormen, nieuwe Mega-vormen, Gigantamax-vormen, Tera-vormen en Ultra Burst
  uit via de bestaande soortinstellingen. Afzonderlijke Mega-cries ook uit.
- De 10 Ultra Beast-families uit, inclusief Poipole/Naganadel: 11 soorten.
- Activering van Mega Evolution, Ultra Burst, Z-Moves, Dynamax en Terastalization
  geblokkeerd in de gimmick-tabel én de bijbehorende Can*/Activate*-functies.
- De in deze broncode aanwezige menselijke FRLG-NPC's beschikbaar in de Emerald-
  build, met hun bestaande gereserveerde graphics-ID's, beeldtabellen en palettes.
  De precieze lijst staat na de controle in `FRLG_NPC_IDS.txt`.

Het script kopieert alleen de benodigde FRLG-NPC-data naar `#if !IS_FRLG`-blokken.
Het zet de game **niet** op FireRed/LeafGreen en schakelt geen FRLG-playerstaten in.
Red/Leaf als gewone NPC's waren al aanwezig; de custom player/Fly/Surf/Glide-
instellingen blijven staan. Er worden geen bestaande NPC's op maps vervangen of
verplaatst. Selecteer na een succesvolle build de extra ID's in Porymap.

## Wat blijft behouden?

Bestaande species/object-ID's, generaties 1–9, regionale vormen, gewone Pokémon,
Spritzee/Jestrick en Aromatisse/Jokequinn, Fairy, gewone moves/abilities, muziek,
mapbestanden, tilesets, NPC-posities en custom player-/glidecode.
Primal Reversions en de gewone/fusie-Necrozma-vormen zijn niet expliciet gevraagd
om te verwijderen en blijven daarom behouden. Alleen Ultra Necrozma wordt uitgezet.
Er worden geen complete generaties uitgezet en geen save-structuren heringedeeld.

Een apart geprogrammeerd Domain Expansion-mechaniek wordt niet gewist. Detecteert
het script Domain-verwijzingen in een te blokkeren functie/tabelrij, dan stopt het
vooraf voor handmatige controle in plaats van die blind te veranderen.

**Let op hergebruikte soortslots:** een custom Fakemon in een Ultra Beast-slot
valt ook onder de betreffende family-instelling. Controleer zulke slots vooraf.
De in deze chat bekende Jestrick/Jokequinn-slots vallen hier niet onder.

## Gebruik in Ubuntu/WSL

Pak de ZIP uit in `/home/dave-/PokemonDev/pokekyoto`. Daar staat dan de map
`PokeKyoto_ROM_Ruimte_FRLG` naast `src`, `include` en `Makefile`.

Alleen een broncontrole en een leesbare patch maken (nog niets wijzigen):

```bash
cd /home/dave-/PokemonDev/pokekyoto
python3 PokeKyoto_ROM_Ruimte_FRLG/kyoto_optimize.py .
```

De bestaande game bouwen, back-up maken, aanpassen, opnieuw bouwen en de
**werkelijke netto ROM-besparing** meten:

```bash
cd /home/dave-/PokemonDev/pokekyoto
python3 PokeKyoto_ROM_Ruimte_FRLG/kyoto_optimize.py . --build-compare
```

De gebruikelijke projectdependencies en een werkende `arm-none-eabi-gcc`/`nm`
moeten in je Ubuntu-PATH staan. Het script installeert niets en gebruikt geen
netwerk. Een bestaande fout in de ongewijzigde build wordt eerst gemeld; er wordt
dan nog niets aangepast. Volledige builds gebruiken dezelfde `make -B ... all`
instellingen voor en na. `--jobs 2` beperkt het geheugengebruik.

Optionele gelijkblijvende make-instellingen:

```bash
python3 PokeKyoto_ROM_Ruimte_FRLG/kyoto_optimize.py . --build-compare --jobs 2 --make-arg DEBUGGING=1
```

Gebruik dit alleen wanneer `DEBUGGING=1` ook in je bestaande project de juiste
instelling is. De standaardopdracht behoudt de instellingen van je `config.mk`.

## Uitvoer en herstel

Naast de projectmap ontstaat `kyoto-space-result-<tijdstip>` met:

- `before.gba`, `.elf`, `.map` en de oorspronkelijke bronbestanden.
- Na succes: `after.gba`, `.elf`, `.map` en de exacte voor/na-getallen in `report.json`.
- `changes.patch`, `FRLG_NPC_IDS.txt` en beide buildlogs.

Bij een fout in de aangepaste build herstelt het script automatisch de gewijzigde
bronbestanden en de opgeslagen baseline-ROM/ELF/map. Het verwijdert geen saves.
Na herstel is een nieuwe build nodig voordat later verder gewijzigde broncode
weer wordt getest; tijdelijke buildbestanden zijn geen bronwaarheid.

De opdracht voor handmatig terugrollen wordt afgedrukt. Voorbeeld:

```bash
python3 PokeKyoto_ROM_Ruimte_FRLG/kyoto_optimize.py . --rollback /home/dave-/PokemonDev/kyoto-space-result-<tijdstip>
```

Terugrollen weigert bronbestanden te overschrijven die na de installatie opnieuw
zijn bewerkt. Bewaar de back-upmap buiten GitHub.

## Hoe wordt ruimte gemeten?

Beide builds meten `__rom_end` in de gegenereerde ELF:

```
gebruikt = __rom_end - 0x08000000
vrij     = 33.554.432 - gebruikt
bespaard = gebruikt_voor - gebruikt_na
```

Daarmee telt de extra ruimte voor FRLG-NPC's automatisch mee. De grootte van een
opgevulde `.gba` van 32 MiB of de grootte van de GitHub-repository is geen goede
netto-ROM-meting. Er staat **geen verzonnen besparing** in dit pakket.

De eerder aangeleverde `pokeemerald(20261027-184644).gba` heeft 865.540 bytes
(0,825 MiB) aaneengesloten FF-opvulling aan het einde. Dat is een controle van
die specifieke oude build, niet de ruimte na deze optimalisatie en niet noodzakelijk
de build van de laatste GitHub-commit.

Let op: uitschakelen verwijdert niet automatisch ieder gedeeld stukje enginecode,
item, beschrijving of animatie. De patch verwijdert die niet agressief, omdat ze
ook door andere mechanics gebruikt kunnen worden. De winst uit daadwerkelijk
weggelaten speciesdata/graphics/cries wordt pas door de linker zichtbaar.

## Testen vóór normaal gebruik

Gebruik een **kopie** van je save. Geen save met een verwijderde Ultra Beast of
battle-only-vorm laden zonder die Pokémon eerst uit party/PC te halen. Nummering
behouden betekent niet dat uitgeschakelde soorten nog bruikbaar zijn.

Controleer in mGBA: gewone single/double battles, player én trainer zonder de
uitgeschakelde gimmicks, je custom Domain Expansion, party/PC/Pokédex en evoluties.
Controleer FRLG-NPC's naast Emerald-NPC's, looprichtingen, palettes/reflections,
dag/nacht en kaartovergangen. Controleer ook player/Fly/Surf/Glide, berry trees en
light sprites. Een geslaagde compile is geen bewijs dat al deze tests slagen.
