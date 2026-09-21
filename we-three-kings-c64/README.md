# we-three-kings-c64

A 3-voice SID arrangement of "We Three Kings", built the same way as
`../o-come-all-ye-faithful-c64`: `tools/gen_pattern.py` reads
`reference/Christmas_Carols_-_We_Three_Kings.mid` and takes the
highest/second-highest/lowest of whatever's sounding at each
sixteenth-note row, across the MIDI's 4 named parts (bass/lead/
countermelody/countermelody 2 -- one line each, no chords within a
track, unlike `it-came-upon-a-midnight-clear-c64`'s strings), as
soprano/alto/bass.

Two things specific to this song:

- Like `it-came-upon-a-midnight-clear-c64`, this MIDI is an
  expressive, unquantized performance, so note boundaries are rounded
  to the nearest sixteenth-note row.
- The source MIDI is voiced unusually high (all "music box"-timbre,
  up to MIDI 101 = F7 -- well above a piano's top key), so
  `tools/gen_note_table.py` here generates a wider table (C2 through
  F7, not the usual C2-C6) rather than transposing the piece down.
  B7 and above would overflow SID's 16-bit frequency register, which
  is why the table stops at F7 with a little headroom instead of a
  full extra octave.

`src/sid.c`, the ADSR-bug avoidance and frequency-write-order fix in
`main.c`, and 3/4 time (12 rows/measure, not 4/4's 16) are carried
over from `it-came-upon-a-midnight-clear-c64` / `o-come-all-ye-faithful-c64`
-- see those projects' READMEs for why they're written the way they are.

## Build

```
make run       # builds build/we-three-kings.d64 and launches VICE
make pattern   # regenerate src/pattern.c from the reference MIDI (needs `mido`)
```
