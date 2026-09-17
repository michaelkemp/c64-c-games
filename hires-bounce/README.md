# hires-bounce

Second piece of the pipeline, after `hello-world/`: turn on the C64's
320x200 hi-res bitmap mode from C, draw into it with a hand-written
Bresenham line routine, and animate it — two points bounce around the
screen with a line drawn between them every frame. This is the graphics
groundwork `asteroids/` will build on (ships, asteroids and shots are all
just lines/polygons on this same bitmap).

The hi-res setup and `draw_line()` live in `src/hires.c`/`src/hires.h`,
shared between two separate programs (only one `main()` per `.prg`, so
they can't live in the same binary):

- `src/main.c` — the bouncing-line demo.
- `tests/bench_line.c` — a benchmark that times `draw_line()` itself; see
  "Benchmark" below.

## Build / run

Same pipeline as `hello-world/` — see that folder's README for the
compile → pack → boot walkthrough.

```sh
make            # compile + pack the demo (build/hires-bounce.d64)
make run        # also launch VICE and autostart it
make bench-d64  # compile + pack the benchmark (build/bench-line.d64)
make run-bench  # also launch VICE and autostart it
make clean      # remove build/
```

## How the hi-res bitmap works

The VIC-II has no dedicated bitmap-graphics chip mode API — "hi-res mode"
is just a different way of interpreting two chunks of ordinary RAM that
the program POKEs into directly:

- **Bitmap**, 8000 bytes: 320x200 monochrome pixels, one bit per pixel,
  but *not* laid out as 320-bytes-per-scanline. It's organized in the
  same 8x8 cells as text mode: for cell row `r` (0-24) and column `c`
  (0-39), that cell's 8 bytes (one per pixel-row) sit contiguously at
  `base + r*320 + c*8`. `draw_line()` in `src/hires.c` builds that address
  once per line, as `ROW_OFFSET[y>>3] + (x & ~7) + (y & 7)` (`ROW_OFFSET`
  is precomputed at compile time so there's no runtime multiply), then
  steps it incrementally per pixel — see "Making it fast" below.
- **Screen matrix**, 1000 bytes: one byte per 8x8 cell, *not* character
  codes in this mode — the high nibble is the color to show where the
  bitmap bit is 1, the low nibble where it's 0. This program fills it
  with `0x10` everywhere: white pixels on a black background.

Turning the mode on ([full bit layout in `c64-c-emu`'s
`docs/vic-ii.md`](../../c64-c-emu/docs/vic-ii.md), verified against real
VIC-II behavior while building that emulator):

| Register | What we set | Why |
|---|---|---|
| `CIA2.pra` ($DD00) bits 0-1 | `10` (binary) | Selects VIC bank 1, `$4000-$7FFF` (the encoding is inverted: `11`=bank 0, `00`=bank 3) |
| `VIC.addr` ($D018) | `$08` | Screen matrix at bank+`$0000` ($4000), bitmap at bank+`$2000` ($6000) |
| `VIC.ctrl2` ($D016) bit 4 | `0` | Multicolor mode off — plain 1-bit-per-pixel hi-res, not the 4-color variant |
| `VIC.ctrl1` ($D011) bit 5 | `1` | Bitmap mode on (this is the bit that actually switches the display out of text mode) |

Bank 1 was picked deliberately: it's plain RAM with no ROM overlapping it
at any CPU memory configuration, so both the VIC-II and the CPU always
see the same bytes there. (Compare a bitmap based at `$E000`, inside the
KERNAL ROM window — reads from the CPU would need HIRAM banked out first
to see real RAM instead of ROM. Bank 1 sidesteps that entirely.)

## Why a custom linker config

cc65's stock `c64.cfg` lets the linker place code/data/stack anywhere
from `$0801` up to `$D000`. This program POKEs the screen matrix and
bitmap into `$4000-$7FFF` by fixed address, outside cc65's own memory
management — so if the linker ever placed so much as one byte of our own
compiled program in that range, it would silently corrupt whatever the
VIC-II is currently displaying (or vice versa).

`cfg/lowmem.cfg` is the stock config with one change: `__HIMEM__` lowered
from `$D000` to `$4000`, capping every cc65-managed region (code, data,
the software stack) below the graphics window. The current build ends
around `$0FC4` — there's about 12KB of headroom left before this becomes
a real constraint.

## The animation: XOR draw/erase

There's no separate "clear the old line" step. `draw_line()` toggles
(`^=`, not `|=`) each pixel's bit. Calling it twice with the *exact same*
endpoints draws the line, then erases it — the second XOR flips every
bit the first one flipped, right back to its original value. So each
frame is:

1. Wait for the raster beam to reach line 250 (the crude vsync — see
   below).
2. Redraw the current line at its old endpoints → erases it.
3. Move both points, bouncing off the 0/319 and 0/199 edges.
4. Draw the line at the new endpoints.

This is the same trick `c64-c-emu/programs/asm/hires_line_xor.asm` uses
for its BASIC-callable rotating-square demo, just in C instead of 6502
assembly, and without a fixed point-count/table protocol — the
endpoints here are two plain `Point` structs.

## The line algorithm

`draw_line()` is the standard integer Bresenham (error-term formulation,
not the more common but slower "for x in range, compute y" school
version) — handles all eight octants, no floating point, no division.

## Making it fast

The first version of this program called a separate `plot(x, y)` function
once per pixel, which recomputed the full bitmap address (`ROW_OFFSET`
array lookup, several ANDs and shifts, a 16-bit add) from scratch every
single time, and compiled with no optimization flags at all. On a
~1MHz 6502 that was slow enough to *watch*: a ~300px diagonal line
visibly swept across the screen like a laser instead of appearing
instantly, because drawing one line took multiple video frames and the
VIC-II was displaying the bitmap while it was still being written.

Two changes fixed it:

1. **Compiler optimization.** `cl65 -Oirs` (optimize, inline more code,
   honour `register`, inline known standard-library calls) — free
   speedup, no code changes.
2. **Incremental addressing.** `draw_line()` now computes the bitmap
   address/bit mask once, for the line's first pixel, then *steps* them
   each iteration instead of recomputing from scratch:
   - Moving `x` by ±1 usually just shifts the bit mask one position;
     the byte address only changes (`±8`) when the mask shifts out,
     i.e. when `x` crosses into the next/previous 8-pixel column.
   - Moving `y` by ±1 usually just moves the pointer by ±1 (the 8 bytes
     of one pixel-column-within-a-cell are contiguous); it only jumps
     by `±(320-7)` when `y`'s low 3 bits wrap, i.e. crossing into the
     next/previous cell row.

   This is the same trick real C64 line-drawing code uses (and the
   reason `c64-c-emu`'s `hires_line.asm` reference is fast in hand-written
   6502): touch only what actually changed between one pixel and the
   next, rather than rebuilding the whole address every time.

Verified two ways, not just assumed: a burst of screenshots taken ~100ms
apart during a live run each showed a single, fully-formed line (no frame
ever caught a partial/growing line the way the first version did), and
`tests/bench_line.c` (below) puts an actual number on it — around 3,800
average cycles per random left-to-right line, a few milliseconds, not the
tens of milliseconds the unoptimized per-pixel-function-call version
would have needed to produce a visible sweep.

## Benchmark

`tests/bench_line.c` draws 100 lines, each from a random point on the
left half of the screen (`x` in 0-159) to a random point on the right
half (`x` in 160-319), times each one individually, and prints the
average. It shares `hires.c`'s exact `draw_line()` — the point is to
measure the real thing, not a copy that could quietly drift out of sync
with it.

**Timing mechanism:** CIA1 Timer A, loaded with `$FFFF` and set to count
down once (one-shot) at the system clock rate; elapsed cycles = `$FFFF`
minus whatever's left when it's stopped right after `draw_line()`
returns. This is the standard way to get cycle-accurate timing on a real
6502 — nothing else on the chip can measure time more precisely.
Interrupts are disabled (`SEI()`/`CLI()`) for the timed window, because
CIA1 Timer A is also what the KERNAL's default jiffy IRQ runs on;
leaving interrupts on here would both misfire that handler (its clock
source has just been repointed at our one-shot count) and add unrelated
cycles to the measurement.

**A real bug caught by checking the number, not just trusting it printed
something:** the first version computed `avg_cycles * 1000000UL /
985248UL` to convert cycles to microseconds. For an average around 5,000
cycles, `5000 * 1000000` is ~5 billion — past the ~4.29 billion ceiling
of a 32-bit `unsigned long` (cc65 has no 64-bit integer type to fall back
on), so it silently wrapped around and printed a plausible-looking but
wrong number (827us instead of the correct ~5187us). Fixed by scaling
with `1015/1000` instead (1015/1000 = 1.015 ≈ 1/0.985248, PAL's
cycles-per-microsecond, rounded to 3 places) — accurate to within about
0.003%, and safe up to an average of ~4.2 million cycles, far past
anything a single line draw can reach. Caught by hand-checking one
result against `cycles / 985248 Hz` rather than assuming the printed
number was correct just because the program ran without error.

## The "vsync"

```c
while (VIC.rasterline != 250) { }
```

This just busy-waits for the raster beam to reach a fixed line near the
bottom of the visible picture before drawing, so a frame's erase+move+draw
doesn't get half-displayed mid-refresh. It is **not** the more correct
approach (a raster interrupt), and it doesn't guarantee exactly one step
per real screen refresh — if a frame's drawing work ever takes longer than
one full refresh, the loop simply waits for line 250 the *next* time it
comes around, so motion would slow down rather than tear or race ahead.
With the optimization above, a single line draw is fast enough that this
mostly doesn't come up in practice; good enough for this demo either way,
real frame-budget pacing is a later problem.

## What's next

This bitmap/line groundwork moves into `asteroids/` next, where the ship,
asteroids and shots all get drawn as polygons on the same hi-res bitmap,
and the JS prototype's game loop/physics gets translated into C.
