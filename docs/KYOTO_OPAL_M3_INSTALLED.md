# Kyoto Opal M3 installed

This branch contains the Kyoto Emerald adaptation of Opal's logical map-palette remapper.

- Logical map palettes: 0..17
- Physical map palette banks: 0..12 (13 total, unchanged)
- UI BG palette banks 13..15 remain outside M3
- Logical palette 13 uses the secondary tileset's palettes/13.pal
- Logical palettes 14..17 use palette_refs.json + palettes/extra_0.pal .. extra_3.pal
- Kyoto's 8-subtile metatile renderer is mirrored exactly
- M2 tile-cache release waits for the matching M3 tilemap publication ticket

Generated runtime profiles live in src/data/tilesets/opal_palettes.generated.h.
