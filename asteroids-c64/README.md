# asteroids-c64

A C64 port of [`michaelkemp/asteroids`](https://github.com/michaelkemp/asteroids)
(the JS/canvas prototype), built on everything measured in `vector-scene/`:
hand-written 6502 line drawing (trick #2, `drawline_asm.s`), double
buffering across two VIC banks (`doublebuf.c`), and the automated
VICE-remote-monitor benchmark harness (`tools/bench.sh`) so every claim
below about what runs at what speed is checked, not assumed.

## Is this actually possible?

Yes, in the sense that matters most: a correct, playable, tearing-free
game is well within reach. What needs honest expectation-setting is
**frame rate at higher object counts**, based on real numbers already
measured in `vector-scene/`:

- Hand-asm line drawing (trick #2) costs roughly **150-190
  cycles/pixel** for typical short-to-medium lines (`vector-scene`'s
  angle sweep and pair benchmarks), a `~4x` improvement over
  cc65-compiled C's measured `~730` cycles/pixel, but nowhere near
  free.
- A single PAL frame's budget is `19,656` cycles.
- The original JS prototype's `ship + 3 big asteroids` scene, back
  when `hires-bounce/tests/bench_scene.c` measured it with plain C,
  came out to `~1.28 million cycles` per redraw (`~65-75x` over a
  frame's budget) -- the finding that paused this whole line of work
  in the first place.
- Scaling that measurement by trick #2's `~4x` speedup (and, if it
  turns out the scene is mostly-moving content, trick #4's full-clear
  win on top) still lands the same ship+3-asteroids scene at roughly
  **10-15x over a single frame's budget** -- i.e. that scene would take
  somewhere around 10-15 real frames (~200-300ms at PAL) to redraw
  once it's fully drawn, not 1.

So: a ship alone, or a ship plus one or two asteroids, should run at or
close to real 50Hz smoothness. A full busy screen with many asteroids
at once will visibly slow down, the same way `hires-bounce/README.md`
originally found -- just from a much less severe starting point than
before this folder's tricks. That's exactly why the phases below add
asteroids **one at a time, measuring each step** rather than jumping
straight to a full scene: it turns "is this possible" from a guess into
a real, checkable frame-budget curve, and it's also the natural
follow-up to `vector-scene/README.md`'s still-open question about
where per-object erase stops beating a full clear (or vice versa).

If a later phase's numbers come out worse than hoped, the fallback
`hires-bounce/README.md` already named -- hardware sprites for some
objects (the ship, bullets, small asteroids) instead of CPU-drawn
bitmap lines for everything -- is still available and still the
C64-idiomatic answer for objects that don't need to be genuine rotated
vector polygons.

## What's being ported, and what's being adapted rather than copied literally

The JS prototype's canvas is 640x480; the C64 hi-res bitmap here is
320x200. Sizes, and a few mechanics that only make sense with a mouse/
keyboard-era browser event loop or with spare CPU nobody needs to
budget, are adapted rather than ported number-for-number:

| | JS prototype | This port |
|---|---|---|
| Screen | 640x480 | 320x200 |
| Ship scale | 12 px | 10 px (tune once it's on screen) |
| Rotation step | 3 degrees/frame | 3 degrees/frame (kept) |
| Asteroid sizes (BIG/MED/SML) | 40/20/10 px radius | 25/13/7 px radius (roughly screen-proportional) |
| Position/velocity | floating point | fixed-point (8.8: 8 integer + 8 fractional bits in a 16-bit int) -- the 6502 has no hardware float, and this is the standard trick for smooth sub-pixel motion with integer-only rendering |
| Friction/drag | `v *= 0.993` per frame | `v -= v >> 7` per frame (~0.992, a cheap shift instead of a multiply) |
| Bullets | small diamond polygon, drawn via lines | a single plotted pixel -- cheaper to draw *and* erase than replicating a 4-point polygon for something that's only ever a couple of pixels on our screen anyway |
| Collision detection | full SAT (separating axis theorem) on decomposed convex sub-polygons | not yet decided -- see phase 3; likely a cheap bounding-circle/distance check first, SAT only if that's not accurate enough |
| Rotation math | `Math.sin`/`Math.cos` per frame | precomputed per-degree tables (`tools/gen_*_table.py`-generated, same approach as `vector-scene/src/circle_table.c`) -- zero runtime trig, matching every other trick in this project so far |

Scoring, lives, difficulty progression, the saucer enemy, and
hyperspace are real mechanics from the prototype and are in scope
eventually (phase 5+), just not blocking the early phases.

## Phases

Each phase should end with something running in VICE and, where it
makes sense to measure, a real cycle-cost number via
`tools/bench.sh` -- same discipline as `vector-scene/`.

### Phase 0: scaffolding (done)

Copied the proven building blocks from `vector-scene/` rather than
sharing code across folders (matching this whole project's convention
of each folder being self-contained): `drawline_asm.s` (trick #2),
`doublebuf.c`/`.h`, `clearbitmap_asm.s` (trick #4, available if a later
phase's scene turns out mostly-moving), `cia_timer.h`, `bench_report.h`,
`tools/bench.sh`, `cfg/lowmem.cfg` (already has the zero-page and
`SCRATCH` fixes from `vector-scene`'s debugging).

### Phase 1: the ship alone

- Ship shape + precomputed per-degree rotation table.
- Static render: ship sitting still, rotated to a fixed heading --
  first correctness/visual check.
- Rotation: left/right input turns the ship 3 degrees/frame, double
  buffered, no tearing.
- Fixed-point position + thrust: up-input accelerates in the current
  heading, friction decays it, position wraps at screen edges.
- Firing: single-pixel bullets, up to 4 live at once, fixed lifetime
  in frames (not real time -- this game's frame time isn't constant
  once more objects are on screen, so counting elapsed *steps* is the
  correct unit, not counting elapsed *milliseconds*), spawned just
  ahead of the ship's nose.
- Measure: cycles for ship-only erase+draw (and bullets, at 0-4 live).

### Phase 2: one asteroid

- One rock shape + precomputed rotation table (or: confirm from the
  prototype whether asteroids actually rotate visually, since they
  have no rotational velocity in the physics -- if not, this may just
  need one fixed orientation per spawn, chosen at random, not a full
  360-entry table).
- Linear motion + wraparound, no collision yet.
- Measure: cycles for ship + 1 big asteroid, both erase+draw. This is
  the first real data point for the "how many objects before full
  clear or a template buffer starts winning" question left open in
  `vector-scene/README.md`.

### Phase 3: collision, splitting, more asteroids

- Ship-asteroid and bullet-asteroid collision (cheap bounding-circle
  check first; only reach for SAT if that's provably not good enough
  for this game's shapes).
- Splitting: BIG -> 2 MED, MED -> 2 SML, at +-30 degrees from the
  parent's trajectory, matching the prototype.
- Scale up to the prototype's easiest-level asteroid count and
  re-measure -- this is the real "ship + 3 asteroids" comparison point
  against `hires-bounce/tests/bench_scene.c`'s original number.

### Phase 4: a playable loop

- Scoring, lives, respawn-after-death, game over/restart.
- Whatever the phase-2/3 measurements say about erase vs. clear vs.
  template buffer, applied as the real per-frame strategy rather than
  a guess.

### Phase 5+: stretch goals

Saucer enemy, hyperspace, difficulty progression, sound (SID), an
attract/title screen. Not blocking a playable phase-4 game.

## Build / run

Same pipeline as `hello-world/`/`hires-bounce/`/`vector-scene/`: C
source -> `cl65` -> `.prg` -> `c1541` -> `.d64` -> VICE. See those
folders' READMEs for the walkthrough if the toolchain needs
re-verifying. Targets will be added here as each phase lands.
