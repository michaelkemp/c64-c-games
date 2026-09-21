# staff-editor

Composes 3-voice SID arrangements by hand-writing a `.json` song and
playing it back with `play_song.py`, instead of pixel-reading a
sheet-music screenshot (see `o-come-all-ye-faithful-c64/README.md` for
why that was abandoned) or hoping a downloaded MIDI happens to reduce
cleanly to 3 voices (see `it-came-upon-a-midnight-clear-c64` and
`we-three-kings-c64`).

There used to also be `staff_editor.py`, a point-and-click grand-staff
GUI with its own row-indexed JSON shape; it's been dropped -- editing
the JSON directly turned out to be faster to iterate with than
clicking through a GUI. If a point-and-click editor is ever wanted
again, it's in git history (see `git log -- staff-editor/staff_editor.py`).

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
  "instruments": {
    "1": {"name": "flute", "waveform": "triangle", "adsr": "49A5"},
    "2": {"name": "clarinet", "waveform": "pulse", "adsr": "08B4", "pulse_width": 0.25},
    "3": {"name": "electric piano", "waveform": "pulse", "adsr": "09B0", "pulse_width": 0.5,
          "filter_cutoff": 1500, "filter_resonance": 0.15},
    "4": {"name": "electric bass", "waveform": "saw", "adsr": "08C2",
          "filter_cutoff": 500, "filter_resonance": 0.2}
  },
  "voices": {
    "soprano": ["I1", "G4-4", "D4-2", "D4-2", "I3", "C4-1", "C4-1", "C4-1", "C4-1", "I1", "G4-4"],
    "alto":    ["I2", "E4-4", "B3-2", "B3-2", "A3-1", "A3-1", "A3-1", "A3-1", "E4-4"],
    "bass":    ["I4", "C3-4", "G2-2", "G2-2", "F2-1", "F2-1", "F2-1", "F2-1", "C3-4"]
  }
}
```

(`songs/chord_test.json` -- a C major chord for 4 beats, two G major
chords at 2 beats each, four F major chords at 1 beat each, then a C
major chord for 4 beats; root/third/fifth split across bass/alto/
soprano. Soprano starts on the "flute" instrument, switches to
"electric piano" for the four F-major quarter-note chords, then back to
"flute" for the final chord -- demonstrating an instrument change
mid-voice. Bass uses a filtered "electric bass" throughout.)

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
  - A token can also be `I<name>` (e.g. `"I1"`, `"Iflute"`) instead of
    a note -- this isn't a note itself; it switches that voice's
    *current instrument* to `instruments[<name>]` (see below), and
    every note token after it in that voice's list uses the new
    instrument until another `I<name>` token appears. It costs no
    duration and doesn't need to line up with anything in the other
    voices. A voice with no `I<name>` token yet, or a song with no
    `instruments` at all, uses a plain always-on default (triangle
    wave, full sustain) -- the same sound this format always had.
- **`instruments`** (optional): a dict of instrument name -> `{"waveform":
  ..., "adsr": ..., "pulse_width": ..., "filter_cutoff": ..., "filter_resonance":
  ...}`, referenced from `voices` by `I<name>` tokens. This is a
  Python-preview approximation of a SID voice, not real chip playback --
  see `SID_Instrument_Cookbook.pdf` for where these recipes come from
  and how to design more. Even done well, this is a stylized
  approximation, not a faithful reproduction of an acoustic instrument
  -- the cookbook itself calls its recipes "starting points," not
  models. A single SID oscillator (or this script's equivalent) is
  always going to sound like a synthesizer's take on a piano/flute/etc,
  not the real thing.
  - `waveform`: `"triangle"`, `"saw"`, `"pulse"`, or `"noise"`.
  - `adsr`: a 4-hex-digit string, same notation as the cookbook's
    `$ADSR` (attack, decay, sustain, release, each `0`-`F`). Attack/decay/
    release are looked up in the real SID's rate tables (`0` ~2ms up to
    `F` ~8s for attack, `0` ~6ms up to `F` ~24s for decay/release);
    sustain is a level from `0` (silent) to `F` (full volume), not a
    time. Attack ramps linearly (that phase really is, on real
    hardware); decay and release follow a decaying exponential rather
    than a straight line, matching the real chip's envelope generator
    (its own decrement rate slows as the level drops, giving a fast
    initial movement and a long settling tail instead of a flat fade).
  - `pulse_width` (only meaningful for `"pulse"`): fraction of the
    cycle spent high, `0`-`1` (`0.5` = a square wave, cookbook's "~25%"
    ≈ `0.25`).
  - `filter_cutoff` (optional, Hz), `filter_resonance` (optional,
    `0`-`1`, default `0`), and `filter_mode` (optional, one of
    `"lowpass"` (default), `"highpass"`, `"bandpass"`): a resonant
    filter applied to the raw waveform before the envelope, a rough
    stand-in for the SID's real multimode filter (which, like this one,
    derives low/high/band-pass taps from the same filter core). Several
    cookbook recipes (bass, "SID 'Bazz' bass," the alien lead) use
    lowpass to take the edge off a raw pulse/saw; a high resonance
    (`0.85`-ish) near a note's fundamental makes the filter
    self-oscillate into a near-pure tone -- the same "sine from a
    sawtooth" trick a real analog VCF (SID's or a Juno's) can do.
    Omit `filter_cutoff` for no filtering (the default).
  - If a note's duration is too short to fit its instrument's full
    attack+decay+release, those phases are compressed proportionally
    so the note still starts and ends cleanly rather than being cut
    off mid-envelope.

`play_song.py` also prints, as playback reaches each one, which
instrument is currently sounding in which voice (and, up front, a
table of every instrument the song defines) -- handy for auditioning a
large instrument set without staring at the JSON while it plays. See
`songs/sampler.json` below for the motivating case.

### `songs/sampler.json` -- the Roland Juno-6 manual's 42 patches

The Juno-6 owner's manual's "Sample Sounds" chapter (pages 18-28) walks
through 42 example patches (strings, organs, electric pianos, brass,
woodwinds, mallets, percussion, sound effects), each shown as slider
positions across the Juno's LFO / DCO / HPF / VCF / VCA / ENV / CHORUS
sections plus a paragraph of prose guidance. `songs/sampler.json`
translates all 42 into this format's `instruments`, one per Juno patch
number and name, and plays a one-octave C-major scale on each in turn
(`I1` through `I42`) so you can audition the whole set back-to-back:

```
python3 play_song.py songs/sampler.json
```

What carries over cleanly from the Juno to a SID voice (and so to this
format): DCO waveform selection -> `waveform`; VCF `FREQ`/`RES` ->
`filter_cutoff`/`filter_resonance`; `ENV`'s A/D/S/R -> `adsr`. The
manual's own self-oscillating-filter patches (`[7] Sine-Wave Organ`,
`[9] Steam Organ`, `[38] Whistle`) map onto high `filter_resonance`
here for exactly the reason above. `[12] Harpsichord`'s call to cut
low frequencies with the Juno's separate HPF, and `[39] Bird
Chirping`'s thin/bright character, are the two patches using
`filter_mode: "highpass"`; `[42] Wind`'s whizzing-air character uses
`"bandpass"`.

What does *not* carry over, because neither the SID nor this script
model it: the Juno's dedicated LFO (vibrato/growl/wah effects the
manual calls out for `[24] Violin`, `[28] Flute`'s growl, `[16]`/`[17]`
Wah/Phase Brass, etc.), envelope/LFO/keyboard-tracking routed into the
filter (the biggest single gap -- covers most of the "Synthesizer
Sound" and brass patches' character), the DCO's sub-oscillator
(`[2] Group Strings II`, `[33] Xylophone`), and Chorus (used on nearly
every patch). Each of those `sampler.json` instruments is the static
waveform+filter+envelope skeleton of its Juno patch, not the full
patch -- a reasonable-sounding starting point, same spirit as the SID
cookbook's recipes, not a slider-for-slider transcription. The manual
itself says as much about its own diagrams (page 18): the knob
positions shown "are not meant to be exact... please adjust settings
while actually playing."
