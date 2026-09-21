# sid-tune-player-c64

One compiled C64 player that loads a song's data from a separate file
on disk at runtime, instead of every song being its own compiled
`pattern.c`/`.prg` the way `o-come-all-ye-faithful-c64` and its
siblings are. This is the "each song lives as its own file on a disk,
loaded by one player" piece `sid-player-c64/README.md` originally
listed as future work.

## Why not just load the JSON directly?

A C64 is a ~1MHz 6502 with about 38K of free RAM. Parsing JSON (or
YAML) text at runtime -- tokenizing strings, floating-point beat
durations, nested objects -- is a lot of work for hardware that
limited, for no real benefit versus doing that parsing once on a
modern machine ahead of time. So the split is:

- **`tools/gen_tune.py`** (runs on a PC) reads a hand-written
  `song.json` (the same format `staff-editor/play_song.py` plays --
  see its README for the syntax) and compiles it into a compact
  binary `.tune` file.
- **`src/main.c`** (runs on the C64) just loads that `.tune` file's
  raw bytes into an array and plays them -- no parsing beyond reading
  a 5-byte header.

## The `.tune` format

```
offset  size  meaning
0       1     0x54 magic byte ('T', but see the "0x54 not 'T'" note below)
1       1     format version (2)
2       1     ROW_FRAMES: PAL video frames per sixteenth-note row (from tempo)
3       2     ROW_COUNT, little-endian
5       15    3 voices' instruments (soprano, alto, bass), 5 bytes each:
              waveform select bits, pulse width (little-endian, 0-4095),
              AD register, SR register -- one instrument per voice for
              the whole song (see "Instruments" below)
20      ROW_COUNT*3   one byte per voice per row (soprano, alto, bass):
              a NOTE_* index into the player's compiled-in note_table.h,
              or HOLD (0xFE), or NOTE_REST (0xFF)
```

This is exactly the row/voice model every other song directory's
`pattern.c` uses (one row = one sixteenth note, HOLD holds, REST
rests) -- just loaded from bytes at runtime instead of compiled in as
a C array.

## Instruments

A song's `instruments` block and each voice's `I<name>` token (see
`staff-editor/README.md` for the JSON syntax) are read by
`tools/gen_tune.py` and compiled into real SID register values --
waveform select bits, the 12-bit pulse-width register, and the AD/SR
registers straight from the `adsr` hex string's two byte-pairs -- so
`songs/oh-come-all-ye-faithful.json`'s brass arrangement (soprano and
alto/bass on sawtooth "Trumpet"/"Tuba" instruments, matching
`staff-editor/songs/sampler.json`'s patches 25 and 27) plays on this
same waveform+envelope combination on *actual SID hardware* (or
reSID's cycle-accurate emulation in VICE), not just `play_song.py`'s
Python DSP approximation. That's the real test of how close the
Python preview's guess actually was.

Two things this compiled player does **not** do, unlike
`play_song.py`'s preview:

- **No mid-song instrument switching.** Each voice loads one
  instrument at startup and never rewrites its AD/SR or waveform again
  -- `tools/gen_tune.py` errors out if a voice's `I<name>` tokens ever
  name more than one distinct instrument. (This isn't a hard limit of
  the chip, just of this player's current code -- see "Not here yet.")
- **No filter.** The SID has exactly one filter circuit shared across
  all 3 voices -- cutoff, resonance, and mode are chip-wide settings,
  not per-voice, so "every voice gets its own `filter_cutoff`" (which
  the JSON format allows, and which `play_song.py`'s preview fakes by
  rendering each voice's audio independently and mixing afterward)
  can't be reproduced on real hardware without deciding which single
  voice actually owns the filter at any moment. `filter_cutoff`/
  `filter_resonance`/`filter_mode` are simply ignored by
  `tools/gen_tune.py`; every voice plays its raw, unfiltered waveform.

**On the SID's envelope-generator "ADSR delay bug":** the earlier
version of this player deliberately used only rate-0 (fastest)
attack/decay/release on every voice specifically to dodge this
well-known chip quirk (a stall of up to ~34ms if an A/D/R *rate*
register is rewritten while a smaller rate is still pending -- see
`o-come-all-ye-faithful-c64/src/main.c`'s comment, and `sid.c`'s note
about voice 3 once swapping between a bass and a drum envelope). This
version writes each voice's AD/SR **once** at startup and never
rewrites it during playback -- only GATE toggles per note -- which is
not the rewrite-while-pending scenario the bug describes. If a
particular instrument's attack or decay ever sounds audibly delayed
compared to `play_song.py`'s preview, that bug is the first thing to
suspect and measure for.

**Debugging note, left in a comment in `src/main.c`:** the magic byte
is checked against the literal `0x54`, not the character `'T'`. cc65
compiles char literals through a default PETSCII charset mapping that
isn't guaranteed to land on the same byte value Python's `ord('T')`
writes into the file. This bit exactly that way once -- a debug dump
showed the file's actual bytes were completely correct and the C
comparison was what was wrong. Worth remembering for any format this
player's C code and a Python tool both need to agree on byte-for-byte.

## Build & run

```
make run                    # plays songs/oh-come-all-ye-faithful.json (the default)
make run SONG=chord_test    # plays a different songs/*.json instead
```

`make run` builds `build/player.prg`, compiles `songs/$(SONG).json` to
`build/$(SONG).tune` via `tools/gen_tune.py` (needs no extra Python
deps beyond the standard library), packs both onto
`build/tune-player.d64` as `player` and `TUNE`, and autostarts it in
VICE. Swapping to a different song currently means rebuilding the
disk with a different `TUNE` file -- there's no on-C64 menu to pick
between multiple songs on one disk yet (each song still needs its own
`.tune` file re-packed as `TUNE`; the *player* itself never changes).

## What's carried over from the compiled-pattern.c song directories

`src/sid.c` and the note-table generation approach
(`tools/gen_note_table.py`, here C2-C6 same as
`o-come-all-ye-faithful-c64`) are unchanged from that project -- see
its README for why. `main.c`'s envelope settings used to be a fixed
ADSR-bug-avoidance default (attack=decay=release=0 on every voice,
hardcoded, ignoring the song entirely); they're now loaded per voice
from the song's `instruments` -- see "Instruments" above for why that's
safe against the same bug this project used to sidestep by brute
force.

## Not here yet

- Mid-song instrument switching (one instrument per voice for the
  whole song only -- see "Instruments" above)
- The SID filter (shared across all 3 voices on real hardware; a
  song's `filter_cutoff`/`filter_resonance`/`filter_mode` are read by
  `staff-editor/play_song.py`'s preview but ignored by
  `tools/gen_tune.py`)
- Choosing between multiple songs on one disk without rebuilding it
  (needs a directory-read + menu on the C64 side)
- PETSCII art / a "now playing" screen while it plays
- A `.tune` format version for songs needing a wider note range than
  C2-C6 (see `we-three-kings-c64` for why that sometimes matters) --
  the player would need to either read the range from the file too, or
  ship a second player build against a wider `note_table.c`
