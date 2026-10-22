# Optional lamp profiling harness

Use a disposable checkout containing the lighting changes. The instrumentation patch uses timers 1/2 and adds a new-game/warp/weather fixture; it is not production code. `profile.py` loads the supplied save temporarily and does not modify it. Supply an existing 128 KiB save, or a disposable blank save. Requires Python with Pillow, a C compiler, the ARM toolchain, and mGBA 0.10.3 built as a shared library.

From the repository root:

```sh
git apply tools/kyoto_lighting_profile/instrument.patch
make -j
```

Compile the host runner, replacing the two mGBA paths with absolute paths to its source and build directories:

```sh
cc -shared -fPIC -O2 \
  -I/path/to/mgba/include -I/path/to/mgba-build/include \
  tools/kyoto_lighting_profile/runner.c \
  -L/path/to/mgba-build -lmgba -Wl,-rpath,/path/to/mgba-build \
  -o /tmp/kyoto-profile-runner.so
python3 tools/kyoto_lighting_profile/profile.py \
  pokeemerald.gba pokeemerald.elf /path/to/disposable.sav \
  /tmp/kyoto-profile-results /tmp/kyoto-profile-runner.so
```

The script saves screenshots and raw counter arrays. It exercises idle, movement, scrolling, synthetic overlapping lamps, clouds, firefly shade, Bag return, map transitions, and daytime cleanup. Both before/after builds must use the same instrumentation and input sequence. To instrument the baseline commit, apply just the main.c portion with `git apply --include=src/main.c`; total preparation and VBlank counters are available without the optional lighting subset counter.

For byte-for-byte comparison with the original mask algorithm, **after** applying instrumentation:

```sh
cp src/kyoto_lamp_mask.c /tmp/kyoto-lamp-mask.optimized.c
python3 tools/kyoto_lighting_profile/differential.py
make -j
# Run profile.py again; the final output reports comparisons and failures.
cp /tmp/kyoto-lamp-mask.optimized.c src/kyoto_lamp_mask.c
```

The oracle reads the original implementation from commit `6a9ac4a323f3009bbc8fec0f1f8e7318932f8c89`, which must exist locally. Diagnostic builds do extra work and must not be used for performance measurements.

Restore production sources and rebuild when finished:

```sh
git apply -R tools/kyoto_lighting_profile/instrument.patch
make -j
```

See [measurement notes](../../docs/performance/kyoto-night-lights.md) for counter meanings, recorded results, and limitations.
