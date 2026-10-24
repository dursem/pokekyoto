# PokéKyoto Sakura + Autumn Wind — No-Dither Fast Map Exit Update

This update continues from `PokeKyoto_Sakura_Autumn_Update.zip` dated 24 October 2026.

## Changed behavior

The 8×8 ordered-dither / pixel-mask fade is removed completely.

When leaving Sakura Petals or Autumn Wind:

1. spawning/recycling of new particles stops immediately;
2. existing petals/leaves keep their exact normal movement, sway and pose animation;
3. edge wrapping is disabled;
4. each existing particle is removed only when it crosses the same boundary that would normally wrap it;
5. the sprite sheet remains byte-identical — no disappearing pixels, filter, or dither;
6. the transition does not modify global `BLDALPHA`, so PokéKyoto lamps and semi-transparent shadows keep their existing blend settings.

A Bag/Party interruption stores and restores only the surviving outgoing population. It does not refill the field while the transition is still draining.

If the transition is cancelled by walking back into the same weather, survivors remain. Only after cancellation may missing density re-enter from the weather's normal offscreen entry edge.

## Natural drain duration

With a stationary camera and the current exact movement formulas:

- Sakura: first petal retires at frame 54 (~0.90 s), last at frame 1,043 (~17.46 s), average ~9.50 s.
- Autumn Wind: last leaf retires at frame 266 (~4.45 s).

No artificial exit acceleration is used. Camera movement can change these timings.

## Install if the previous no-dither drain is already applied

If you already applied the package from my previous message, only use the small follow-up patch:

```sh
git apply --check /path/to/pokekyoto-fast-map-exit-only.patch
git apply /path/to/pokekyoto-fast-map-exit-only.patch
make -j8
```

## Install on top of the previous smooth-fade weather build

```sh
git apply --check /path/to/pokekyoto-no-dither-drain.patch
git apply /path/to/pokekyoto-no-dither-drain.patch
make -j8
```

## Install from the target source without the earlier Sakura/Autumn update

Apply the included base patch first:

```sh
git apply --check /path/to/pokekyoto-sakura-autumn-base.patch
git apply /path/to/pokekyoto-sakura-autumn-base.patch
```

Then apply the no-dither patch:

```sh
git apply --check /path/to/pokekyoto-no-dither-drain.patch
git apply /path/to/pokekyoto-no-dither-drain.patch
make -j8
```

Do **not** apply the old `pokekyoto-smooth-fades-only.patch` afterward; it would restore the rejected ordered-dither renderer.

## Validation included

The package contains the complete updated source overlay, the incremental patch, the previous base patch, updated focused tests, a validation report and the deterministic movement timing JSON.

The changed weather source and test source passed host-side Clang syntax checks. The incremental patch also passed `git apply --check` against the exact weather package from earlier today.

A new GBA is intentionally not included because this runtime does not have `arm-none-eabi-gcc` / `arm-none-eabi-cpp`. The old smooth-fade test ROM is therefore omitted rather than being mislabeled as the new behavior.
