# PokéKyoto Sakura + Autumn Wind — No-Dither Fast Map Exit Update

Deze update gaat verder op `PokeKyoto_Sakura_Autumn_Update.zip` van 24-10-2026.

## Wat is veranderd

De 8×8 ordered-dither/pixel-mask fade is volledig verwijderd.

Bij het verlaten van Sakura Petals of Autumn Wind:

1. er worden direct geen nieuwe particles meer aangemaakt;
2. op de oude map behouden bestaande petals/leaves exact hun normale beweging, sway en animatie;
3. zodra `mapGroup/mapNum` echt naar de nieuwe map verandert, krijgen alleen de overgebleven outgoing particles een tijdelijke **1,6× bewegingssnelheid**;
4. wrapping/recycling blijft uitgeschakeld;
5. zodra een bestaande particle zijn normale schermrand/wrapgrens passeert, wordt alleen die particle verwijderd;
6. de sprite-art zelf blijft volledig ongewijzigd — geen verdwijnende pixels/filter/dither;
7. `BLDALPHA` wordt niet aangepast, zodat lampen en semi-transparante schaduwen niet mee veranderen.

Als je tijdens het uitlopen de Bag/Party opent, wordt alleen de nog bestaande population opgeslagen en teruggezet. Er worden tijdens de outgoing transition dus niet ineens nieuwe petals bijgemaakt.

Als je terugloopt naar de route met dezelfde weather voordat alles weg is, blijven de survivors bestaan. Pas nadat de outgoing transition is geannuleerd mag de ontbrekende density vanaf de normale offscreen instroomrand worden aangevuld.

## Verwachte uitlooptijd

De source-map blijft op **1,0×** snelheid. Pas nadat de game daadwerkelijk een andere `mapGroup/mapNum` heeft geladen wordt **1,6×** gebruikt voor de nog zichtbare outgoing particles. Daardoor is er geen versnelling vóór de mapgrens.

Bij een stilstaande camera, gemeten vanaf dezelfde volledige test-population:

- Sakura op normale source-map snelheid: laatste petal ~17,46 s.
- Sakura met de new-map boost: laatste petal ~11,12 s, gemiddeld ~6,03 s.
- Autumn Wind normaal: laatste leaf ~4,44 s.
- Autumn Wind met de new-map boost: laatste leaf ~3,01 s.

In echte mapovergangen is het meestal korter, omdat er vóór de map-ID-wissel al particles uit beeld kunnen zijn verdwenen.

## Installeren als de vorige no-dither drain al is toegepast

Als je het pakket uit mijn vorige bericht al hebt toegepast, hoef je alleen de kleine vervolgpatch te gebruiken:

```sh
git apply --check /pad/naar/pokekyoto-fast-map-exit-only.patch
git apply /pad/naar/pokekyoto-fast-map-exit-only.patch
make -j8
```

## Installeren als de smooth-fade build al in je PokéKyoto staat

Voer in je bestaande PokéKyoto-map uit:

```sh
git apply --check /pad/naar/pokekyoto-no-dither-drain.patch
git apply /pad/naar/pokekyoto-no-dither-drain.patch
make -j8
```

## Installeren vanaf de target source zonder eerdere Sakura/Autumn patch

Eerst:

```sh
git apply --check /pad/naar/pokekyoto-sakura-autumn-base.patch
git apply /pad/naar/pokekyoto-sakura-autumn-base.patch
```

Daarna:

```sh
git apply --check /pad/naar/pokekyoto-no-dither-drain.patch
git apply /pad/naar/pokekyoto-no-dither-drain.patch
make -j8
```

Gebruik de oude `pokekyoto-smooth-fades-only.patch` daarna **niet** meer; die zou de afgekeurde dither-fade terugzetten.

## Validatie

De update bevat:

- gewijzigde volledige sourcebestanden onder `changed_files/`;
- de cumulatieve `pokekyoto-no-dither-drain.patch` voor de smooth-fade build;
- `pokekyoto-fast-map-exit-only.patch` als de vorige no-dither versie al is toegepast;
- de eerdere basispatch voor een schone installatie;
- aangepaste focused tests;
- `validation/NO_DITHER_DRAIN_VALIDATION.md`;
- `validation/drain_motion_model.json` met zowel de normale source-map timing als de 1,6× destination-map timing.

De gewijzigde weather source en test source zijn door een host-side Clang syntaxcheck gekomen. De incremental patch is ook met `git apply --check` gevalideerd tegen de weather-build van vanochtend.

Een nieuwe GBA is in deze runtime niet meegebouwd, omdat hier geen `arm-none-eabi-gcc` / `arm-none-eabi-cpp` geïnstalleerd is. Daarom zit de oude dither-test-ROM bewust niet in dit pakket: die zou niet het nieuwe gedrag tonen.
