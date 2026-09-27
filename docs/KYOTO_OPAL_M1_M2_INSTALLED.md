# Kyoto Opal M1 + M2 framework installed

This branch now contains:

- M1: 11-bit map-grid IDs, 672 primary metatiles, 1375 usable secondary metatiles (0x7FF reserved).
- Migrated Emerald map/border data and metatile labels, with legacy/wide semantic digest comparison.
- M2 runtime framework for Opal extended tile references on Kyoto's **8-subtile** metatiles.
- Logical tile capacity: 1024 primary + 2048 secondary; physical BG tile window remains 1024.
- `metatile_tiles_ext.bin` sidecar support and build-time capacity generation/validation.
- Animation slot pinning and deferred cache-slot reuse after DMA completion.
- Shop map-background resolution for streamed tile IDs.

No tileset is streamed until it has a `metatile_tiles_ext.bin` sidecar. This intentionally makes the first qualification build visually equivalent to the clean baseline.

M3 logical palettes 14..17 are **not installed yet**.
