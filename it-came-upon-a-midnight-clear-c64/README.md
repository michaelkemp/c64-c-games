# it-came-upon-a-midnight-clear-c64

A 3-voice SID arrangement of "It Came Upon A Midnight Clear", built
the same way as `../o-come-all-ye-faithful-c64`: `tools/gen_pattern.py`
reads `reference/Christmas_Carols_-_It_Came_Upon_A_Midnight_Clear.mid`
and takes the highest/second-highest/lowest of whatever's sounding at
each sixteenth-note row as soprano/alto/bass.

Two differences from that sibling project, both explained in
`tools/gen_pattern.py`'s docstring:

- The source MIDI has 5 tracks, not 4: "lo str"/"hi str"/"horn" plus
  exact duplicates ("lo str dbl"/"hi str dbl") for a fuller string
  sound. The generator only reads the first 3. Each of those 3 tracks
  is itself a chordal string/horn part (not one line per voice like
  the hymn's SATB source), so the highest/second/lowest reduction is
  doing real work here, not just relabeling already-separated parts.
- This MIDI isn't quantized -- it's an expressive human performance,
  not a mechanically precise one -- so note boundaries are *rounded*
  to the nearest sixteenth-note row rather than asserted exact. That's
  an audible simplification (the rubato is gone), not a rounding
  error to be fixed.

`src/sid.c`, the ADSR-bug avoidance and frequency-write-order fix in
`main.c`, and the note-table generation approach are all carried over
unchanged from `o-come-all-ye-faithful-c64` -- see that project's
README for why they're written the way they are.

## Build

```
make run       # builds build/midnight-clear.d64 and launches VICE
make pattern   # regenerate src/pattern.c from the reference MIDI (needs `mido`)
```
