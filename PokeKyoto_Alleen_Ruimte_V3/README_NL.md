# PokéKyoto — alleen ruimtebesparing V3

GEEN FRLG-update. Geen MIDI-conversie of aanpassing van de muziek.

Dit vervangt het eerder aangeboden FRLG-installatiepakket voor deze opdracht.
Het staat in een aparte map en roept de oude installer niet aan. De oude
installer mag blijven staan; start die niet opnieuw.

## Doel

Uitschakelen van Mega Evolution (inclusief de extra Mega-cry-bestanden),
Gigantamax-vormen, Dynamax, Terastalization/Tera-vormen, Z-Moves, Ultra Burst
en de tien Ultra Beast-families. Poipole en Naganadel vallen onder één familie.

De bestaande configuratie sluit de uitgeschakelde vormen/families uit van de
build. Daarnaast worden de activeringsfuncties van de vijf ingebouwde gimmicks
uitgeschakeld. Dit is GEEN volledige handmatige verwijdering van alle gedeelde
battlecode, items, move-tabellen of animaties. De netto ROM-besparing wordt
pas bepaald met de builds; er is geen gegarandeerd aantal MB.

## Wat blijft ongemoeid?

- Alle overworld-/NPC-/playergraphics, object-ID's en hun registraties.
- Muziek, samples, MIDI-configuratie, maps, tilesets, Fly/Surf/Glide en verlichting.
- Generatie-instellingen 1–9, regionale vormen, gewone moves/abilities en cries.
- De soorten-ID's en de opbouw van de enum worden niet hernummerd.
- Primal Reversions en basis-Necrozma/fusies worden niet uitgezet in dit pakket.
- Een afzonderlijk geïmplementeerde Domain Expansion wordt niet aangepast.
  Als de installer zichtbare Domain-verwijzingen in een te wijzigen
  standaardgimmick aantreft, stopt hij in plaats van die blind te overschrijven.

De installer bewerkt geen sprites of species_info-headers. Een eigen Fakemon
op een uitgeschakeld Ultra Beast- of Mega-slot zou echter ook worden uitgezet.
Gebruik dit pakket niet zonder aanpassing als je die slots al voor Fakemon inzet.

## Installatie in Ubuntu/WSL

Sla PokeKyoto_Alleen_Ruimte_V3.zip op in OneDrive/Desktop/2026.

```bash
cd /home/dave-/PokemonDev/pokekyoto &&
python3 -m zipfile -e "/mnt/c/Users/dave-/OneDrive/Desktop/2026/PokeKyoto_Alleen_Ruimte_V3.zip" . &&
python3 PokeKyoto_Alleen_Ruimte_V3/kyoto_space_only.py . --build-compare --jobs 2
```

Verwachte eerste regel:

    Installer: kyoto-space-only-20261028-v3

De stappen zijn: broncontrole, back-up van huidige bouwbestanden, volledige
ongewijzigde build, back-up van de bronwijzigingen, alleen de besparingen toepassen,
volledige kandidaatbuild, vergelijken met __rom_end.

Beide builds gebruiken dezelfde compiler en instellingen. De -B-optie forceert
herbouw; dit voorkomt dat het eerdere afgebroken make-proces een nulmeting met
oude of onvolledige objectbestanden oplevert. De voortgang wordt getoond en het
complete buildlog wordt buiten je project bewaard. Er worden GEEN Git-commits,
resets, restores, pulls of pushes uitgevoerd.

Start geen tweede make en wijzig geen projectbestanden tijdens de vergelijking.

## Geen groei toegestaan

Alleen als de gemeten gebruikte ROM-ruimte gelijk of kleiner is dan de nulmeting,
blijft de aangepaste build actief. Bij grotere ROM, een kandidaat-bouwfout of
Ctrl+C na het toepassen worden de aangepaste bronbestanden en de nulmeting-ROM
teruggezet. Als je ondertussen zelf aan die bronbestanden hebt gewerkt, stopt
het herstel om die nieuwere handmatige wijzigingen niet te overschrijven.

Bij Ctrl+C tijdens de eerste build is nog geen gamebron aangepast; de eerder
bewaarde .gba/.elf/.map worden teruggezet. De installer beëindigt zijn eigen
compilerprocessen voordat hij herstelt. Een stroomuitval, SIGKILL of schijffout
kan automatische afhandeling verhinderen; bewaar daarom de backup-directory.

Niet alle tijdelijke/genererende buildbestanden worden teruggedraaid. Gebruik
na een afbreking deze V3 opnieuw met --build-compare voor een volledige herbouw.

Na slagen verschijnt:

    BEFORE: ... bytes used; ... free
    AFTER: ... bytes used; ... free
    NET SAVING (NO FRLG): ...
    FREE AFTER: ...
    Nieuwe ROM: .../optimized.gba

De normale pokeemerald.gba in de projectmap bevat dan dezelfde nieuwe build.
Het bestand kan door opvulling 32 MiB blijven; de gemeten gebruikte ROM-ruimte
is bepalend voor de winst, niet die vaste bestandsgrootte.

## Eerst controleren zonder wijzigingen

```bash
python3 PokeKyoto_Alleen_Ruimte_V3/kyoto_space_only.py .
```

Dit schrijft uitsluitend een changes.patch en rapport buiten de projectmap.
Er is bewust geen --apply-optie waarmee je ongemeten veranderingen accepteert.

## Terugzetten

Het script print het exacte commando met jouw back-upmap. Handmatig:

```bash
python3 PokeKyoto_Alleen_Ruimte_V3/kyoto_space_only.py . --rollback /volledig/pad/naar/kyoto-space-only-result-DATUM
make -B -j2
```

--rollback herstelt alleen de bijgehouden bronbestanden en weigert nieuwere
handmatige wijzigingen te overschrijven. Bouw daarna opnieuw; gebruik de dan
gegenereerde ROM. Een eerdere FRLG-V1/V2-back-up hoort bij die oudere installer.
V3 stopt als een werkelijk toegepaste oude FRLG-patch wordt aangetroffen.

## Teststatus en saves

32 geautomatiseerde installertests zijn geslaagd, inclusief fake-build-fouten,
Ctrl+C/rollback, weigeren van groei, bit-exact herstel, behoud van muziek/saves,
een host-C-proef voor de gimmick-entrypoints, en een echte subprocess-proef voor
het stoppen van compilerprocessen. Geen volledige PokéKyoto-ROM-compilatie of
mGBA-gameplaytest is hier uitgevoerd. TEST_RESULTS.txt geeft de precieze tests.

Test de succesvolle ROM met een KOPIE van je save. Een reeds opgeslagen Mega,
Ultra Beast of andere uitgeschakelde vorm moet je eerst uit die test-save halen.
De installer verandert je .sav niet en is geen save-migratietool.

Bronreferentie voor de pakketontwikkeling: dursem/pokekyoto,
add41daf166fdd251b1bb14e7b1dc22c3ed3b61b. Lokale bronbestanden worden gecontroleerd
in plaats van vervangen door complete bestanden uit die oude commit.
