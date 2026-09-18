# c64-c-games

Writing C64 games in C (via [cc65](https://cc65.github.io/)), building up
the toolchain pipeline in pieces rather than starting from a full game.
Sibling project to
[`c64-c-emu`](https://github.com/michaelkemp/c64-c-emu), a C64 emulator
written for the same reason: to actually understand the machine.

## What's here

- **`hello-world/`** — the pipeline itself, proven end to end: C source
  → `cl65` → `.prg` → `c1541` → `.d64` → boots in VICE. Start here if the
  toolchain needs re-verifying.
- **`hires-bounce/`** — turns on the VIC-II's 320x200 hi-res bitmap mode
  from C and draws into it with a hand-written Bresenham line routine
  (`src/hires.c`). Includes two benchmarks (`tests/`) that measure real
  cycle cost on real (emulated) hardware rather than guessing.
- **`vector-scene/`** — revisits the "paused" conclusion below by
  actually trying the tricks fast C64 vector graphics (Elite, etc.)
  use: hand-written 6502 for the hot path, zero-page state, a
  dirty-list erase. Found a real bug in `hires-bounce`'s own published
  benchmark number along the way, and measured hand-written 6502 at
  ~3.9x faster than cc65-compiled C for the same draw+erase pair.

## Status: revisiting the bitmap-line approach

See `vector-scene/README.md` for what's been tried since the pause
below and what it found. Short version: the original ~24 cycles/pixel
estimate this pause was based on was itself measured wrong (a silent
16-bit-timer wraparound bug); real cc65-compiled cost is closer to
~730 cycles/pixel, and hand-written 6502 gets a measured ~3.9x back.
Sprites may still end up being the right answer for the full scene, but
that hasn't been re-tested with a correct baseline yet.

### Original pause, for context

The original plan was to port the JS Asteroids prototype
(`../asteroids/index.html`) to C64 by drawing the ship/asteroids as
polygons on the hi-res bitmap, XOR-erasing and redrawing every frame —
the same technique `hires-bounce/` demonstrates for a single line.

`hires-bounce/tests/bench_scene.c` measured what that actually costs for
an Asteroids-scale scene (ship + 3 asteroids, shapes/scale taken directly
from the JS prototype): **~1.28 million CPU cycles per redraw — about
1.3 real seconds, roughly 65-75x over a single 50/60Hz frame's budget.**
That's not a "needs more optimization" gap, it's off by two orders of
magnitude.

The reason isn't a bug in the line-drawing code (verified against the
exact expected Bresenham pixel counts) — it's that the 1979 arcade
Asteroids ran on **vector (XY/stroke) CRT hardware**: a dedicated analog
vector generator steers the electron beam directly from point to point,
with no framebuffer and no per-pixel CPU cost at all. The C64 has no such
hardware; it's raster-only, so every line has to be rasterized by the
CPU into a bitmap one pixel at a time — fundamentally more expensive, no
matter how tight the code is.

The C64-appropriate answer is **hardware sprites** (free per-frame
redraw, unlike CPU-drawn bitmap polygons) rather than more line-drawing
optimization. Not pursued yet — parked here with the reasoning recorded
so it doesn't need re-deriving later. `hires-bounce/` itself (the
pipeline, the hi-res setup, the line algorithm, the benchmarking
approach) remains a solid, reusable foundation for whatever's built next.
