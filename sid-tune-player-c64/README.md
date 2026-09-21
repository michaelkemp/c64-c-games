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
1       1     format version (1)
2       1     ROW_FRAMES: PAL video frames per sixteenth-note row (from tempo)
3       2     ROW_COUNT, little-endian
5       ROW_COUNT*3   one byte per voice per row (soprano, alto, bass):
              a NOTE_* index into the player's compiled-in note_table.h,
              or HOLD (0xFE), or NOTE_REST (0xFF)
```

This is exactly the row/voice model every other song directory's
`pattern.c` uses (one row = one sixteenth note, HOLD holds, REST
rests) -- just loaded from bytes at runtime instead of compiled in as
a C array.

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

`src/sid.c`, the note-table generation approach (`tools/gen_note_table.py`,
here C2-C6 same as `o-come-all-ye-faithful-c64`), and the ADSR-bug-
avoidance envelope settings in `main.c` (attack=decay=release=0 on
every voice, to sidestep SID's documented envelope rate-decrease
stall) are all unchanged from that project -- see its README for why.

## Not here yet

- Choosing between multiple songs on one disk without rebuilding it
  (needs a directory-read + menu on the C64 side)
- PETSCII art / a "now playing" screen while it plays
- A `.tune` format version for songs needing a wider note range than
  C2-C6 (see `we-three-kings-c64` for why that sometimes matters) --
  the player would need to either read the range from the file too, or
  ship a second player build against a wider `note_table.c`
