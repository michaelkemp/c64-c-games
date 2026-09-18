# vector-scene

`hires-bounce/README.md` paused with a conclusion: line-drawing on the
C64 is fundamentally too expensive for a CPU-drawn Asteroids-style
scene, and the real answer is hardware sprites, not more line-drawing
optimization. This folder revisits that conclusion and actually tries
the tricks real C64 games (Elite, in particular) use for fast vector
graphics, instead of stopping at "the CPU is slow, full stop":

- Hand-written 6502 instead of cc65-compiled C for the hot path.
- Zero-page-resident state for that hot path.
- A dirty-list erase instead of re-walking Bresenham to erase.
- Double buffering across two VIC banks, and a rotating-square demo
  built on top of all of the above.
- (Not yet built, see "What's next") a fast unrolled full-bitmap clear.

Every number below is machine-measured through the automated benchmark
harness described in "Automated benchmarking", not eyeballed off the
emulator screen -- and one of the most important findings this folder
produced was that an *existing, previously-published* benchmark number
in `hires-bounce/` was wrong for exactly the kind of reason that
discipline is meant to catch.

## Automated benchmarking

Every benchmark here reports its result twice: once as `cprintf()` text
a human can read on the emulator screen, and once as raw bytes POKEd to
a fixed memory address (`tests/bench_report.h`, `$2000`-`$2004`).
`tools/bench.sh` drives `x64sc` through VICE's remote text monitor
(`-remotemonitor`, TCP port 6510): launch, wait for the socket, resume
execution, then poll by reconnecting (which pauses the CPU) and reading
that fixed address until the ready-flag byte appears, then decode the
4 result bytes.

Two non-obvious things had to be verified before trusting any number
out of this harness:

1. **`-warp -sounddev dummy` are required, and don't affect the
   result.** Without them, `x64sc` paces itself to real 1x (or slower)
   wall-clock speed via its audio sync -- a benchmark that is genuinely
   ~0.5 real seconds of C64 time was taking 20+ real seconds to
   actually finish, easy to mistake for a hang if you only sample once
   (this cost a fair amount of this session's time before being
   caught: see the git history / session transcript if curious).
   Critically, warp mode only changes how fast the *host* lets the
   emulation run -- `x64sc` is cycle-exact regardless, and the
   benchmarks measure real 6502 *cycles* via the CIA hardware timer,
   never wall-clock time, so the reported numbers are exactly what real
   1x hardware would take either way.
2. **Connecting to the remote monitor pauses the CPU; `x` resumes it.**
   Verified directly against a known program before trusting the
   pause/resume timing of anything else.

## A real bug in the baseline number

`hires-bounce/tests/bench_line.c` times a single `draw_line()` call
with CIA1 Timer A alone (16-bit, max 65,535 cycles) and reports "around
3,800 average cycles" in that folder's README. While calibrating this
folder's benchmarks, that number turned out almost certainly wrong.

Measuring two fixed lines directly (a 2-pixel line: 2,634 cycles; a
51-pixel horizontal line: 38,359 cycles) and solving for a linear fit
gives roughly **730 cycles per pixel** for cc65-compiled `draw_line()`
in this build -- not the ~24 cycles/pixel a "3,800 average" implies.
At that real per-pixel cost, any line longer than about 90 pixels
overflows Timer A's 16-bit range. `hires-bounce/tests/bench_scene.c`'s
own comments already document the relevant 6526 CIA quirk: on
underflow, the counter reloads from its latch and, in one-shot mode,
then *stops* -- so a trial that actually took longer than 65,536 cycles
reads back as if it took exactly **0** cycles, not as an error and not
as an implausibly large number. A 100-trial average that's silently a
mix of real short-line readings and zeroed-out long-line ones will
still print a small, plausible-looking number. That's very likely
exactly what happened.

Every benchmark in this folder uses `tests/cia_timer.h` instead: CIA1
Timer A chained into Timer B for a wraparound-safe 32-bit counter, the
same technique `bench_scene.c` already uses for whole-scene timing, just
applied here to single lines too.

**A further refinement, found while debugging the hand-asm version's
correctness (see below):** the expensive part specifically is the
*Y-stepping* branch. A perfectly horizontal test line drew and erased
near-instantly; every other test line (any of them with vertical
movement) took multiple real seconds even under `-warp`. cc65's
generated code for the conditional 320-ish-byte pointer offset add
(`ptr += (y0&7==wrap) ? y_cross : y_step`) is apparently far more
expensive than the mask-shift/pointer+8 logic on the X side. This means
line *slope* matters a lot for cc65-compiled cost, not just length --
worth remembering if a future benchmark here only tests one kind of
line.

## Trick #1: dirty-list erase (regression, not a win)

`src/dirtylist.c`: `draw_line_dirty()` does what `draw_line()` does but
also records each touched byte's address and mask into a `DirtyList`.
`erase_dirty()` then replays that list -- a flat loop with no
error-term arithmetic -- instead of re-running the whole branchy
Bresenham walk a second time with identical endpoints (which is what
`hires-bounce`'s "draw the same line twice" erase trick actually does).

The theory: skip re-deriving the touched addresses, since draw already
worked them out. Measured result, same 100 random lines, same chained
timer:

| | cycles (draw+erase pair) |
|---|---|
| Baseline (`bench_pair_baseline.c`) | **216,867** |
| Dirty-list (`bench_pair_dirtylist.c`) | **282,360** (30% *worse*) |

The regression is real: cc65's generated code for indexing into a C
array with a 16-bit runtime index (computing `base + index*sizeof(T)`
for two parallel arrays, every pixel, on both the draw and erase side)
costs more than the branch/compare logic it was meant to replace. The
idea itself isn't wrong -- it's a real, well-known technique -- it's
just that cc65-compiled C is the wrong place to spend that cost. This
result is what motivated going to hand-written assembly for trick #2
rather than trying more C-level micro-optimizations.

## Trick #2: hand-written 6502 (the real win)

`src/drawline_asm.s`: the same Bresenham algorithm, restricted to
`x1 >= x0` (the caller swaps endpoints first if that doesn't already
hold -- a one-time setup cost, not a per-pixel one), with:

- All hot state (`ptr`, `mask`, `err`, `e2`, `dx`, `dy`, ...) in zero
  page, addressed directly.
- Parameters passed through plain global variables
  (`dla_x0`/`dla_y0`/`dla_x1`/`dla_y1`) instead of cc65's normal
  software-stack calling convention, which itself carries real
  per-call marshalling overhead.
- The pixel toggle uses a zero-page pointer with indirect-Y addressing
  (`LDA (ptr),Y` / `EOR mask` / `STA (ptr),Y`, `Y` always 0) rather than
  self-modified absolute addressing. One cycle slower per access (5 vs
  4 cycles) but much simpler to get right first -- see trick #2b below
  for upgrading this.

Measured result, same 100 random lines, same chained timer
(`bench_pair_asm.c`):

| | cycles (draw+erase pair) | vs. baseline |
|---|---|---|
| Baseline (cc65 C) | 216,867 | 1x |
| Hand-written 6502 | **56,214** | **~3.9x faster** |

This matches the hypothesis behind the whole exercise (and the
independent [ChatGPT conversation](https://chatgpt.com/share/6aad8e63-552c-83e8-a0eb-99a50b7d5884)
saved as a PDF while working on this, which converged on the same
"Elite didn't have a magic line algorithm, it just didn't waste cycles
re-deriving state cc65-equivalent C would" point): cc65's generic
16-bit-int-arithmetic-and-comparison overhead, not the Bresenham
algorithm itself, was the real bottleneck all along.

## Trick #2b: self-modified absolute addressing (the small, expected win)

`src/drawline_asm_smc.s`: identical to `drawline_asm.s` -- same
algorithm, same `x1 >= x0` restriction, same shared
`dla_x0`/`dla_y0`/`dla_x1`/`dla_y1`/`dla_bitmap_base` calling
convention (imported from `drawline_asm.s` rather than redefined, so
both can be linked into the same program without symbol clashes) --
except the pixel toggle patches the operand bytes of a fixed
`LDA a:$0000` / `STA a:$0000` pair (the `a:` prefix forces ca65 to
assemble a 3-byte absolute instruction even though `$0000` would
otherwise fit in a zero-page encoding) whenever the touched address
changes, instead of dereferencing a zero-page pointer. This is the
actual Elite technique the ChatGPT conversation described: `LDA abs` +
`STA abs` is 4+4=8 cycles per pixel instead of indirect-Y's 5+6=11,
paid for by a slightly bigger patch (4 operand bytes across both
instructions, instead of 2 pointer bytes) on the ~1-in-4-8 pixels where
the address actually changes.

Measured result, same 100 random lines, same chained timer, same
draw+erase pair (`bench_pair_smc.c`):

| | cycles (draw+erase pair) | vs. baseline | vs. trick #2 |
|---|---|---|---|
| Baseline (cc65 C) | 216,867 | 1x | -- |
| Trick #2 (zero-page indirect) | 56,214 | 3.9x | 1x |
| Trick #2b (self-modified) | **54,185** | **4.0x** | **~1.04x faster** |

A real but modest win, exactly as predicted -- most of the C-to-asm
gap was cc65's per-pixel overhead, not the specific addressing mode of
the pixel toggle itself. `tests/correctness_check.c` was extended to
check `draw_line_smc()` against the same 8 structural cases (all pass)
before trusting this number.

### Correctness, and a subtlety that looked like a bug but wasn't

`tests/correctness_check.c` draws 8 structural cases (horizontal, both
vertical directions, 45 degrees, shallow, steep) with both the proven C
`draw_line()` and `draw_line_asm()`, comparing a whole-bitmap checksum.
All 8 currently pass.

An earlier version of this test compared `draw_line_asm()` (always
left-to-right) against `draw_line()` called in whatever direction the
random test data happened to give it (sometimes right-to-left), and
found real checksum mismatches on 6 of 40 random lines. That looked
like an asm bug. It wasn't: this Bresenham formulation's tie-breaking
is direction-dependent for asymmetric slopes -- drawing A-to-B and
B-to-A can select a different but equally valid set of pixels. Directly
comparing `draw_line()` called in the *same* direction
`draw_line_asm()` uses (confirmed with a full-region byte-by-byte diff,
not just a checksum) showed zero difference. Since a real draw+erase
pair always uses one consistent direction, this doesn't matter in
practice -- but it's the kind of thing that's worth writing down so a
future benchmark doesn't get spooked by it again.

## Trick #3: double buffering, and the rotating square demo

`src/doublebuf.c`: two full hi-res buffers in separate VIC banks --
buffer 0 is bank 1 (screen `$4000`, bitmap `$6000`, the layout
`hires.c` has always used), buffer 1 is bank 2 (screen `$8000`, bitmap
`$A000`). The CPU draws into whichever buffer isn't currently on
screen, then `doublebuf_show()` flips which one the VIC displays by
changing `CIA2.pra`'s bank bits and `VIC.addr` -- a couple of register
writes, not a redraw.

One real gotcha, worth writing down because it's easy to get backwards:
`$A000`-`$BFFF` is BASIC ROM from the CPU's side by default. CPU
*writes* to that range always reach the real RAM underneath regardless
-- the C64's ROM overlay only affects what a *read* sees, RAM is always
physically there. But `draw_line_asm()`'s pixel toggle is a
read-modify-write (`LDA`/`EOR`/`STA`), so it needs reads to see real RAM
too, or every toggle XORs against whatever BASIC happens to contain
instead of the actual bitmap content. `doublebuf_init()` clears the CPU
port's LORAM bit once at startup to fix this, leaving KERNAL (HIRAM,
`$E000`-`$FFFF`) mapped in so `cprintf()`/`clrscr()` still work
afterward.

`src/main.c` is the actual demo requested: `src/circle_table.c` (360
points, radius 95, centered on screen, generated by
`tools/gen_circle_table.py` -- see its docstring) supplies four corners
90 degrees apart; each frame, one buffer is on screen while the other
gets the square it showed two frames ago erased (XOR-redraw, same trick
as `hires-bounce/src/main.c`, just now safely off-screen) and the
next-angle square drawn, then the buffers flip. Never touches the
buffer currently being displayed, so there's no tearing regardless of
how long the redraw takes.

**Measured** (`tests/bench_rotating_square.c`, same double buffer/asm
draw, 3 full rotations, chained wraparound-safe timer): erasing the old
square and drawing the new one (8 short line draws -- the square's
edges, at radius 95, are about 134 px each, shorter than the
full-width lines the trick #1/#2 benchmarks above use) costs
**~180,615 cycles per 1-degree step** -- about **9.2 PAL frame budgets**
(19,656 cycles/frame). A full 360-degree rotation is about 65 real
seconds at PAL speed.

That's not "instant" -- at one degree per redraw, this isn't yet
tearing-free *and* 50/60Hz-smooth simultaneously, it's tearing-free
*and* visibly stepping about every 9 frames. But it's a real, measured,
correctly-drawn rotating square (confirmed with a screenshot, not just
trusted), a large improvement over the "minutes to rotate a four-line
square" the plain-C, single-buffer version this session started from
would have produced, and every number above is machine-verified rather
than guessed.

### A surprise: trick #2b regresses on this specific demo

`src/main_smc.c` / `tests/bench_rotating_square_smc.c` are the same
demo/benchmark with `draw_line_smc()` (trick #2b) swapped in for
`draw_line_asm()` (trick #2) -- since trick #2b measured faster on the
isolated line benchmark, the natural assumption was it'd help here too.
Measured result: **190,699 cycles/step -- ~5.6% *slower*** than trick
#2's 180,615, not faster.

The mechanism, confirmed rather than just argued (`tests/bench_angle_sweep.c`
times both versions on the exact same line, center-to-circle-edge, at
18 angles from 0 to 170 degrees):

| angle | trick #2 | trick #2b | #2b/#2 |
|---|---|---|---|
| 0 (horizontal) | 13,283 | 13,359 | 1.006 |
| 30 | 14,920 | 15,791 | 1.058 |
| 60 | 16,004 | 17,378 | 1.086 |
| **90 (vertical)** | 17,961 | 19,809 | **1.103** |
| 120 | 15,958 | 17,330 | 1.086 |
| 150 | 14,938 | 15,800 | 1.058 |
| 170 | 14,290 | 14,618 | 1.023 |

(full 18-row table in git history / re-runnable via
`make run-bench-angle-sweep`) -- a smooth curve, worst at 90 degrees,
best at 0/180, trick #2 winning at *every single angle tested,
including the "best case."* That's a stronger result than "the square
picks bad angles": at this line length, trick #2b never wins at all.

Two things drive it. First, address-change frequency: trick #2b only
pays its patch cost (rewriting operand bytes across two separate
instructions, `toggle_lda` and `toggle_sta`) when the touched address
actually changes, and that happens on an x-mask wrap (1-in-8 pixels,
any slope) or *every single time* the y-step branch fires -- which,
unlike the x-side, has no "same address" sub-case. That fraction is
`~min(|dy|/dx, 1)`: 0 at perfectly horizontal, saturating at 1 for
anything 45 degrees or steeper. Second, and this is the part the
angle-sweep table above adds on top of the original reasoning: trick
#2b's *setup* is slightly more expensive too (the initial address gets
synced into two patched instructions instead of one pointer), a fixed
cost that needs enough pixels to amortize away -- and this sweep's
lines (radius 95, ~95-134 px) are shorter than the original 100-line
benchmark's (up to ~320 px). Short *and* steep both push toward trick
#2b losing; the square's edges are both (chords 90 degrees apart on a
circle, averaging close to 45 degrees across a full rotation, and
shorter than the benchmark's typical line) -- so its regression isn't
one cause, it's the two disadvantages stacking.

Not chased further this session (see "What's next") -- flagged here
because it's a real, useful lesson: a win measured on one synthetic
benchmark doesn't automatically transfer to a different real workload,
and both slope *and* length are hidden variables neither the original
line benchmark nor its README called out until this demo exposed them.
`main.c`/`bench_rotating_square.c` (trick #2) are the ones worth using
for now; `main_smc.c`/`bench_rotating_square_smc.c` (trick #2b) are
kept for the record and as a base for whatever fix "What's next" ends
up trying.

### A second real bug, found while chasing the first: scratch memory wasn't actually safe

The angle-sweep table above didn't come out clean on the first try --
two of the eighteen angles read back wildly wrong (one exactly 0, one
~9x its neighbors), in a way that moved to *different* angles between
runs. That pattern (same code, different-looking failure each build) is
a strong tell for memory corruption, not a real effect in the code
being measured, and an isolated single-angle re-test at the exact same
coordinates that had shown "0" came back completely normal --
confirming it.

The cause: every automated benchmark in this folder POKEs its result
to a fixed address (`tests/bench_report.h`) for `tools/bench.sh` to
read back, and that address was chosen once, early on, as "`$2000`,
below the VIC-II window and above where `hires-bounce` topped out
(~`$0FC4`)" -- true for small, simple programs, but never actually
*guaranteed*. `bench_angle_sweep.c` links two full asm modules plus a
720-byte table; a `.map` file (`cl65 ... -Wl -m,file.map`) showed its
own compiled code+rodata+data ran from `$080D` to `$2094`. The
"scratch" writes at `$2000`-`$208F` were landing *inside the running
program's own code and data*, not on empty RAM -- corrupting whatever
happened to be at that address at that point in a given build, which
is exactly the build-dependent, angle-shifting symptom observed.

Fixed at the linker-config level, not just in the one file that
exposed it: `cfg/lowmem.cfg` now lowers `__HIMEM__` from `$4000` to
`$3F00` and defines a `SCRATCH` memory area at `$3F00`-`$3FFF`.
Every `MAIN`/`BSS`/stack-managed byte is now capped below `$3F00`, so
`ld65` itself refuses to link (an overflow error, not silent
corruption) if a program ever grows large enough to threaten it.
`BENCH_RESULT_ADDR` and every other benchmark's raw scratch addresses
now point into `$3F00`+ instead of the old guess.

## What's next

Roughly in order of expected value:

1. **Trick #2b's honest scope is narrower than hoped, and now
   measured, not guessed**: the angle sweep above shows trick #2
   (zero-page indirect) winning at *every* tested angle at this line
   length, not just steep ones -- so trick #2b's isolated-benchmark win
   only shows up for lines both shallow *and* long enough to amortize
   its slightly pricier setup. Batching an x-side and y-side address
   change into one patch instead of two (the fix originally proposed
   here) would only address the slope half of that, not the length
   half, so it's unlikely to make trick #2b a strict improvement on its
   own -- worth remeasuring if pursued, but expectations should be
   modest. Trick #2 (indirect-Y) is the more broadly reliable default
   until/unless that changes.
2. **The fast unrolled full-bitmap clear** (the "list of `STA` calls"
   idea this whole folder started from): `STA $page,X` unrolled across
   all 32 pages of the bitmap, ~5 cycles/byte, ~40,000 cycles for the
   whole 8,000-byte bitmap. Worth benchmarking as an alternative to
   per-object dirty-list erase for scenes with many objects, where
   "clear everything, redraw everything" might beat "erase N objects,
   draw N objects" past some object count.
3. Re-run `hires-bounce/tests/bench_scene.c`'s ship+3-asteroids scene
   with all of the above, to get a real, un-silently-wrong answer to
   the question that paused `hires-bounce/`: does this close enough of
   the 65-75x-over-budget gap to make a CPU-drawn vector scene viable,
   or does it still point to hardware sprites? Given (1), use trick #2
   (indirect-Y) for this, not trick #2b, unless (1) resolves in #2b's
   favor first.
4. Push the rotating-square demo's per-step budget down toward 1-2
   frames (from ~9.2 today): shorter edges (smaller radius) and/or
   advancing more than 1 degree per redraw (fewer, bigger angular
   steps -- cheaper per second of rotation even though each individual
   redraw costs the same) are worth measuring regardless of how (1)
   resolves.

## Build / run

```sh
make                        # build every .d64 (build/)
make run-bench-pair-baseline
make run-bench-pair-dirtylist
make run-bench-pair-asm
make run-correctness-check
make run-rotating-square        # the demo: watch it live in VICE
make run-bench-rotating-square  # same demo, fixed frame count, reports cycles
tools/bench.sh build/bench-pair-asm.prg   # automated: prints avg cycles, no VICE-watching needed
make clean
```

`cfg/lowmem.cfg` is `hires-bounce/cfg/lowmem.cfg` plus one change: the
zero-page segment is grown from cc65's stock 26 bytes to 96
(`$0002`-`$0061`), to fit `drawline_asm.s`'s hand-placed zero-page state
alongside cc65's own runtime-reserved zp pointers.
