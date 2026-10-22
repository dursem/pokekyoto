# Night lamp profiling, 2026-10-22

The implementation substantially reduces lamp overlap cost. It does **not** eliminate all deadline misses, particularly with overlapping lamps and clouds together.

## Measurement

Baseline: `6a9ac4a323f3009bbc8fec0f1f8e7318932f8c89`. Both versions used the normal `make -j` configuration with identical temporary instrumentation. Measurements ran in headless mGBA 0.10.3, source `1c61b54208ca6266129d0f2394c04bd8c44f98c5`. This is emulated GBA cycle time, not host execution time.

The fixture initializes a new game in RAM, warps to Littleroot (33,23), and forces TIME_NIGHT before each refresh. It bypasses the opening story and does not supply a complete gameplay save. The first sequence waits 200 refreshes, then samples idle for 60 refreshes and Up/Down/Left/Right for 64/128/64/128 refreshes. These movement segments include lamp overlap, collision, and camera movement. They replay the same buttons for the same **hardware refresh counts**; faster game updates mean the versions need not finish at identical positions. Consequently the table describes reproducible scene sequences, not identical per-pose microbenchmarks.

The stress sequence warps back, moves two existing lamp sprites onto the player's position (eight pixels apart), and samples 120 refreshes after 240 warmup refreshes. It then repeats with sunny clouds and firefly shade. Existing lamp count, assets, and sprite callbacks are retained. These relocated lamps are a synthetic stress case.

| Sequence | Mean field preparation cycles, before | After | Blocked video transfers, before → after |
| --- | ---: | ---: | ---: |
| Standing near lamp, 60 refreshes | 196,544 | 104,285 | 12 → 0 |
| Up, 64 | 215,079 | 110,970 | 26 → 1 |
| Down, 128 | 238,143 | 112,851 | 62 → 0 |
| Left, 64 | 217,771 | 111,377 | 22 → 0 |
| Right, 128 | 75,562 | 42,290 | 11 → 2 |
| Two overlapping lamps, 120 | 343,842 | 141,404 | 59 → 0 |
| Overlapping lamps + clouds, 120 | 385,561 | 221,255 | 59 → 49 |
| Overlapping lamps + firefly shade, 120 | 344,243 | 149,311 | 60 → 5 |

A blocked transfer is `MapPalettesBlockTransfer()` rejecting that VBlank, not a measured display FPS value. Each hardware refresh samples the latest completed preparation counters; a missed refresh can repeat an earlier counter sample. Timings include interrupt time when an interrupt falls within a measured span. Avoid interpreting small differences as precise speedups.

In the final cloud stress sequence, mean lighting/cloud/firefly preparation was 141,233 / 79,797 / 87 cycles. In the firefly sequence it was 147,484 / 178 / 1,118 cycles. Clouds and shade were actually enabled, not just timed through their inactive entry points. Combined cloud load remains a performance limitation.

For unchanged private masks, the lamp VBlank stage fell from 1,719 cycles for one uploaded mask (2,741 for two) to 662 cycles and **zero mask bytes** transferred. Changed masks still upload the complete 512 bytes. The counters also cover map commit, field VBlank callback, buffered GPU/DMA requests, and cloud transfers. The measured video span ends before audio/link processing. Stage counters for a blocked transfer retain their previous admitted values and must not be treated as new transfers; refer to counter 14.

Raw refresh samples, means, and maxima: [samples.json](kyoto-night-lights/samples.json). Array positions: 0 lighting, 1 clouds, 2 firefly shade, 3 total preparation, 4 completed preparation sequence, 5 map commit, 6 VBlank callback, 7 buffered GPU/DMA, 8 cloud VBlank, 9 lamp VBlank, 10 video VBlank span, 11 lamp preparation subset, 12–13 unused, 14 blocked transfer, 15 fixture command.

## Changes and correctness boundaries

- Snapshot BG maps, priorities, character bases, and camera scroll once immediately before the frame's lamps. Cache pending map tile pointers for that frame only. BG tile copies, animation, scrolling, and resource reuse cannot retain old pointers into the next frame.
- Reject map layers once per overlapping OAM piece and skip transparent or already-cut glow pixels before sprite/map sampling. Hoist tile-row calculations and vertical flips out of the pixel loop.
- Keep only the measured pixel and map-sampling kernels in ARM code in IWRAM. Final IWRAM use is 28,932 bytes, leaving 3,836 bytes; the existing linker assertion requiring at least 3 KiB of stack headroom is unchanged. EWRAM use is 245,892 bytes. The mask scratch buffer is 512 bytes on the stack.
- Generate masks in scratch space and compare against persistent transfer buffers. Preserve a pending dirty flag across identical rebuilds and blocked VBlanks. Newly allocated/reused resources force dirty state. Transfer source storage remains persistent. A temporarily uncut lamp can leave an older upload pending; that harmless upload completes once, preserving the cached private tiles for later reuse.
- Reject actor OAM entries by palette/tile range before position arithmetic. Replace two software divisions per palette color with an exact 32-entry channel lift table. Continue computing colors every frame, retaining all palette, fade, strength-ramp, and source-animation behavior without new cross-frame palette caches.
- Leave `src/main.c`, transaction admission, commit order, cloud effects, and firefly effects unchanged in production. No light-count, quality, or update-frequency changes.

## Verification

`make -j` passed after removing all runtime profiling/oracle code; [build log](kyoto-night-lights/build.txt). The existing IWRAM stack assertion passes. All 32 red and green lift values match the original integer divisions exactly.

The diagnostic build calls the **original** mask algorithm on the same pending frame before the optimized algorithm. It compares every generated byte and the cut/no-cut result. The final run compared **4,396 masks with zero differences**, including NPCs, the truck's sprite pieces, walking animation/directions, scrolling, overlapping lights, clouds, firefly shade, Bag entry/return, and indoor/outdoor transitions. Daytime released all 16 private mask slots. See [diagnostic output](kyoto-night-lights/differential.txt).

Inspected emulator captures: [overlap](kyoto-night-lights/overlap.png), [clouds](kyoto-night-lights/clouds.png), [firefly shade](kyoto-night-lights/fireflies.png), [Bag](kyoto-night-lights/bag.png), [indoors](kyoto-night-lights/indoors.png), [daytime](kyoto-night-lights/daytime.png).

The oracle checks pending animation/map graphics through the existing pending-frame accessors. It is not exhaustive coverage of every tileset, split-sprite layout, flip combination, fade mode, or allocator failure. No real hardware, full-save Party/battle flows, or exhaustive map-transition testing was performed. Residual misses in combined-effects scenes mean the overall FPS problem is not fully resolved.

Reproduction tools and instructions: [profiling harness](../../tools/kyoto_lighting_profile/README.md).
