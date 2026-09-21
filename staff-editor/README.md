# staff-editor

Two ways to compose 3-voice SID arrangements by hand, instead of
pixel-reading a sheet-music screenshot (see
`o-come-all-ye-faithful-c64/README.md` for why that was abandoned) or
hoping a downloaded MIDI happens to reduce cleanly to 3 voices (see
`it-came-upon-a-midnight-clear-c64` and `we-three-kings-c64`):

- **`play_song.py`** -- write a `.json` song by hand in a plain-text
  format (see below) and play it back. The fast path: no GUI to click
  through, just a text file and a script.
- **`staff_editor.py`** -- a point-and-click grand-staff GUI. More
  discoverable, much slower to iterate with by hand-editing.

Both work in the same underlying model: 3 voices (soprano, alto,
bass), notes with a pitch and a duration. `staff_editor.py`'s saved
files aren't the same JSON shape `play_song.py` reads (the editor's is
row-indexed to match a song directory's `pattern.c`; `play_song.py`'s
is a flat per-voice sequence, easier to type by hand) -- converting
between them is straightforward if it turns out to be worth doing, but
hasn't been needed yet.

A `play_song.py`-format song.json can already be heard on actual SID
playback (not just this tool's preview audio): `../sid-tune-player-c64`
compiles one into a binary the C64 loads and plays at runtime -- see
that project's README.

## Hand-written songs (`play_song.py`)

```
python3 play_song.py songs/chord_test.json
```

A song is:

```json
{
  "title": "chord test",
  "tempo": 100,
  "time_signature": "4/4",
  "key_signature": "C",
  "voices": {
    "soprano": ["G4-4", "D4-2", "D4-2", "C4-1", "C4-1", "C4-1", "C4-1", "G4-4"],
    "alto":    ["E4-4", "B3-2", "B3-2", "A3-1", "A3-1", "A3-1", "A3-1", "E4-4"],
    "bass":    ["C3-4", "G2-2", "G2-2", "F2-1", "F2-1", "F2-1", "F2-1", "C3-4"]
  }
}
```

(`songs/chord_test.json` -- a C major chord for 4 beats, two G major
chords at 2 beats each, four F major chords at 1 beat each, then a C
major chord for 4 beats; root/third/fifth split across bass/alto/
soprano.)

- **`tempo`**: beats per minute. A "beat" is always a quarter note,
  regardless of the time signature's denominator -- `time_signature`
  is bookkeeping (and will matter once there's a hand-written-JSON ->
  `pattern.c` converter), not a factor in duration math.
- **`key_signature`**: a major key's tonic name (`"C"`, `"G"`, `"F"`,
  `"Bb"`, `"A"`, ... -- sharp and flat keys both work). Its sharps or
  flats apply automatically to plain letters; write `#`, `b`, or `n`
  (natural) directly on a note to override the key signature for just
  that one note.
- **`voices`**: each voice is a flat list of `"<note>-<duration>"`
  tokens, consumed in order -- there's no per-note timestamp, so a
  chord is just the same duration landing at the same position across
  all 3 voices' lists (as in the example above). A voice with a
  shorter total duration than the others just ends early rather than
  being an error.
  - `<note>` is `R` (rest) or scientific pitch notation: a letter
    `A`-`G`, an optional accidental (`#`/`b`/`n`), then an octave
    (middle C = `C4`, matching every song directory's convention).
  - `<duration>` is in beats (quarter notes) and can be fractional:
    `0.5` is an eighth note, `1.5` a dotted quarter, `4` a whole note
    at 4/4.

## The GUI (`staff_editor.py`)

Composes directly in the same model the song directories' `pattern.c`
files use: one row = one sixteenth note, 3 voices (soprano, alto,
bass), a voice either starts a new note, holds, or rests at each row.
A saved song is a JSON file in that same row/voice shape (see below),
not a separate format that needs translating.

### Run it

```
python3 staff_editor.py [songs/yourname.json]
```

Needs `pygame` and `numpy` (both already on this machine's system
Python; `pip install pygame numpy` elsewhere).

If the given file exists, it's loaded; otherwise you start blank and
Ctrl+S creates it there. Defaults to `songs/untitled.json`.

### Controls

| Key | Action |
|---|---|
| `1` / `2` / `3` | select active voice (soprano / alto / bass) |
| `4`-`8` | set note duration: whole / half / quarter / eighth / sixteenth (also resizes the selected note, if any) |
| `A`-`G` | enter a note of that letter **at the cursor**, in whichever octave is closest to the voice's previous note, then advance the cursor by the current duration -- this is how you move through a bar without clicking each note |
| `R` | rest at the cursor, then advance (also clears any note already there) |
| left click | place a note at that exact pitch/beat (current duration), or select an existing one; also moves the cursor to just after it |
| Up / Down | nudge the *selected* note by a semitone (adds/removes a `#` as needed) |
| Delete / Backspace | remove the selected note |
| Left / Right | move the cursor by one duration-step (view auto-scrolls to follow) |
| `[` / `]` | jump the cursor back/forward one measure |
| `K` | cycle key signature, 0-7 sharps |
| `T` | cycle time signature: 4/4, 3/4, 2/4, 6/8 |
| `M` | append one measure at the end |
| Space | play back the whole piece |
| Ctrl+S | save |
| Ctrl+O | reload from disk (discards unsaved changes) |
| Esc | quit |

Soprano and alto both live on the treble staff, bass on the bass staff
(a real grand staff, matching how the hymn screenshot notated it) --
they're told apart by color (see the legend in the corner), not by
stem direction. Half/whole notes are drawn as open noteheads, quarter
and shorter as filled, matching real notation; there are no stems or
beams.

Each voice has its own cursor (shown as a thin vertical line in that
voice's color) -- switching voices with `1`/`2`/`3` doesn't move where
you were in another voice.

## v1 scope -- what's here and what isn't

Here: one grand staff, all 5 standard durations, click-to-place *or*
keyboard note-entry with an auto-advancing cursor, semitone nudging,
key and time signatures (4 presets), scrolling, playback, JSON
save/load.

Not here yet, on purpose (see the conversation this was built in for
the reasoning -- start with a working core loop, grow it once the
interaction feels right):

- Dotted notes, ties, and triplets
- Flat spelling (chromatic notes are always spelled as a sharp of the
  letter below, matching every song directory's `note_table.h`
  convention -- there's no "Bb", only "A#"; key signatures are
  likewise sharps-only, so only major keys up to C# are selectable)
- Arbitrary time signatures (only the 4 presets `T` cycles through)
- Undo/redo
- More than 2 accidental-aware octaves' worth of vertical space before
  ledger lines get cramped -- fine for SATB-range material, would need
  more spacing to comfortably read very high/low material like
  `we-three-kings-c64`'s

## The GUI's JSON format (different from `play_song.py`'s, see above)

```json
{
  "title": "untitled",
  "time_signature": [4, 4],
  "rows_per_measure": 16,
  "measures": 8,
  "voices": {
    "soprano": [{"row": 0, "pitch": "G4", "duration": 4}, ...],
    "alto": [...],
    "bass": [...]
  }
}
```

`row` is a sixteenth-note index from the start of the piece; a voice's
note list only has entries where a note *starts* (no explicit
HOLD/REST rows, unlike a song directory's `pattern.c` -- those are
derived, not stored). This is intentionally close to what
`o-come-all-ye-faithful-c64/tools/gen_pattern.py` and its siblings
produce internally before they emit C -- turning a saved song.json
into a project's `pattern.c` is a short, not-yet-written script along
the same lines as those (expand each voice's note list into one
NOTE_*/HOLD/NOTE_REST cell per row).
