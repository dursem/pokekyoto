#ifndef GUARD_KYOTO_PETALS_CONFIG_H
#define GUARD_KYOTO_PETALS_CONFIG_H

// The three pink canopy shades in the current General primary tileset.
// These are logical map-palette coordinates, not fixed physical BG banks.
#define KYOTO_SAKURA_TREE_PALETTE 3
#define KYOTO_SAKURA_TREE_LIGHT 9
#define KYOTO_SAKURA_TREE_MIDDLE 10
#define KYOTO_SAKURA_TREE_DARK 11

// Outgoing Sakura / Autumn Wind does not use a pixel mask or BLDALPHA fade.
// Existing particles stop recycling and are removed only after they cross their
// normal wrap boundary. They keep normal speed on the source map. Once a real
// connected-map change is detected, only the surviving outgoing particles get
// a mild 1.6x motion boost so they clear the destination sooner. This avoids
// changing the shared OBJ blend used by PokéKyoto's lamp/shadow effects.
#define KYOTO_PETALS_NEW_MAP_DRAIN_NUMERATOR 8
#define KYOTO_PETALS_NEW_MAP_DRAIN_DENOMINATOR 5

#endif
