#!/usr/bin/env python3
"""Compiles a hand-written song.json (see ../../staff-editor/README.md
for the format) into a compact binary .tune file this player loads
from disk at runtime -- see src/main.c and the top-level README.md for
why that's the point of this project (one compiled player, many song
files, no per-song rebuild).

Note parsing (parse_note/parse_voice/KEY_SIGNATURES) is intentionally
duplicated from staff-editor/play_song.py rather than imported, same
reasoning every song directory's tools/gen_pattern.py already
duplicates a little logic: each tool directory stays buildable/
readable on its own.

.tune binary format (all a player needs to play a song -- see
src/main.c's load_tune()):

    offset  size  meaning
    0       1     'T' magic byte
    1       1     format version (2)
    2       1     ROW_FRAMES: PAL video frames per sixteenth-note row
                  (derived from tempo -- see beats_to_row_frames())
    3       2     ROW_COUNT, little-endian
    5       15    3 voices' instruments (soprano, alto, bass), 5 bytes
                  each: waveform select bits (WAVE_TRIANGLE/SAWTOOTH/
                  PULSE/NOISE from src/sid.h), pulse width (little-
                  endian, 0-4095, only meaningful for WAVE_PULSE), AD
                  register, SR register -- see resolve_instrument()
                  below. One instrument per voice for the *whole song*
                  (no mid-song "I<name>" switching, and no filter --
                  see README.md's "Not here yet" for why).
    20      ROW_COUNT*3   one byte per voice per row (soprano, alto,
                  bass, in that order): either a NOTE_* index into the
                  player's compiled-in note_table.h (0 = NOTE_C2, ...,
                  matching this project's C2-C6 range), or HOLD
                  (0xFE), or NOTE_REST (0xFF) -- the exact same
                  per-row model every song directory's pattern.c uses,
                  just as loaded bytes instead of compiled-in C.

Usage: tools/gen_tune.py song.json output.tune
"""
import json
import re
import struct
import sys

LETTER_ORDER = "CDEFGAB"
LETTER_TO_SEMITONE = {0: 0, 1: 2, 2: 4, 3: 5, 4: 7, 5: 9, 6: 11}

_SHARP_KEYS = ["C", "G", "D", "A", "E", "B", "F#", "C#"]
_SHARP_ORDER = "FCGDAEB"
_FLAT_KEYS = ["C", "F", "Bb", "Eb", "Ab", "Db", "Gb", "Cb"]
_FLAT_ORDER = "BEADGCF"
KEY_SIGNATURES = {}
for _n, _key in enumerate(_SHARP_KEYS):
    KEY_SIGNATURES[_key] = {letter: 1 for letter in _SHARP_ORDER[:_n]}
for _n, _key in enumerate(_FLAT_KEYS):
    KEY_SIGNATURES.setdefault(_key, {letter: -1 for letter in _FLAT_ORDER[:_n]})

NOTE_RE = re.compile(r"^([A-G])([#bn]?)(-?\d+)$")
INSTRUMENT_TOKEN_RE = re.compile(r"^I(\w+)$")

HOLD = 0xFE
NOTE_REST = 0xFF
FIRST_MIDI = 36  # C2 -- must match src/note_table.h's NOTE_C2
NUM_NOTES = 49  # C2..C6 inclusive -- must match src/note_table.h's NUM_NOTES

# Mirrors src/sid.h's #defines -- must stay byte-for-byte identical, same
# "no shared source of truth across the Python/C language boundary"
# reasoning as HOLD/NOTE_REST above.
WAVEFORM_BITS = {"triangle": 0x10, "saw": 0x20, "sawtooth": 0x20, "pulse": 0x40, "square": 0x40, "noise": 0x80}
DEFAULT_INSTRUMENT = {"waveform": "triangle", "adsr": "00F0", "pulse_width": 0.5}


def parse_note(token, key_accidentals):
    m = NOTE_RE.match(token)
    if not m:
        raise ValueError(f"bad note {token!r} -- expected e.g. 'C4', 'F#3', 'Bb5', 'Cn4'")
    letter, accidental, octave = m.group(1), m.group(2), int(m.group(3))
    semitone = LETTER_TO_SEMITONE[LETTER_ORDER.index(letter)]
    if accidental == "#":
        semitone += 1
    elif accidental == "b":
        semitone -= 1
    elif accidental == "":
        semitone += key_accidentals.get(letter, 0)
    return (octave + 1) * 12 + semitone


def parse_voice(tokens, key_accidentals):
    """Returns (events, note_tokens, instrument_name_or_None) -- note_tokens
    is `tokens` with any "I<name>" entries filtered out, same length and
    order as `events`, for callers (voice_to_rows) that zip the two
    together for error messages. Unlike staff-editor/play_song.py, an
    "I<name>" token can't appear more than once with a *different* name
    -- this player loads one instrument per voice at startup and never
    rewrites it, so mid-song instrument switching (which play_song.py
    supports) isn't representable in the .tune format yet."""
    events = []
    note_tokens = []
    instrument_name = None
    for tok in tokens:
        m = INSTRUMENT_TOKEN_RE.match(tok)
        if m:
            if instrument_name is not None and m.group(1) != instrument_name:
                raise ValueError(
                    f"this player loads one instrument per voice at startup -- "
                    f"can't switch from {instrument_name!r} to {m.group(1)!r} mid-song "
                    f"(that works in staff-editor/play_song.py's preview, not here yet)")
            instrument_name = m.group(1)
            continue
        note_part, _, dur_part = tok.rpartition("-")
        if not note_part:
            raise ValueError(f"bad token {tok!r} -- expected '<note>-<duration>' or 'I<name>'")
        duration = float(dur_part)
        midi = None if note_part == "R" else parse_note(note_part, key_accidentals)
        events.append((midi, duration))
        note_tokens.append(tok)
    return events, note_tokens, instrument_name


def resolve_instrument(instruments, name):
    """name=None -> the same always-on triangle default
    staff-editor/play_song.py falls back to. Returns 5 packed bytes:
    waveform select bits, pulse width (little-endian), AD register, SR
    register -- no filter (the SID's filter is one shared circuit
    across all 3 voices, not per-voice, so "every voice gets its own
    filter_cutoff" -- which the JSON format allows and the Python
    preview fakes by rendering voices independently -- can't be
    reproduced on real hardware without picking which voice actually
    owns the filter; not done here yet, see README.md)."""
    if name is None:
        spec = DEFAULT_INSTRUMENT
    elif name not in instruments:
        raise ValueError(f"unknown instrument {name!r} -- add it to the song's \"instruments\"")
    else:
        spec = instruments[name]
    waveform = spec.get("waveform", "triangle")
    if waveform not in WAVEFORM_BITS:
        raise ValueError(f"unknown waveform {waveform!r} -- this player only knows {sorted(WAVEFORM_BITS)}")
    adsr = spec.get("adsr", "00F0").lstrip("$")
    if len(adsr) != 4:
        raise ValueError(f"bad adsr {adsr!r} -- expected 4 hex digits, e.g. '09A0'")
    ad, sr = int(adsr[0:2], 16), int(adsr[2:4], 16)
    pulse_width = round(spec.get("pulse_width", 0.5) * 4095)
    return struct.pack("<BHBB", WAVEFORM_BITS[waveform], pulse_width, ad, sr)


def midi_to_index(midi, token):
    index = midi - FIRST_MIDI
    if not 0 <= index < NUM_NOTES:
        raise ValueError(
            f"note {token!r} (MIDI {midi}) is outside this player's compiled "
            f"note_table.h range (C2-C6) -- regenerate a wider table with "
            f"tools/gen_note_table.py (see we-three-kings-c64 for an example "
            f"of extending it) or transpose the song")
    return index


def voice_to_rows(events, tokens):
    """Expands (midi_or_None, duration_beats) events into one cell per
    sixteenth-note row: NOTE_* index on the row a note starts, HOLD for
    the rest of its duration, NOTE_REST for a rest."""
    rows = []
    for (midi, duration), tok in zip(events, tokens):
        n_rows = round(duration * 4)
        if n_rows <= 0:
            raise ValueError(f"duration in {tok!r} rounds to 0 sixteenth-note rows")
        if midi is None:
            rows.extend([NOTE_REST] * n_rows)
        else:
            rows.append(midi_to_index(midi, tok))
            rows.extend([HOLD] * (n_rows - 1))
    return rows


def beats_to_row_frames(tempo):
    """PAL is ~50Hz; a row is a sixteenth note, so 4 rows/beat."""
    row_frames = round(50 * 60 / (tempo * 4))
    if not 1 <= row_frames <= 255:
        raise ValueError(f"tempo {tempo} gives ROW_FRAMES={row_frames}, outside 1-255")
    return row_frames


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} song.json output.tune", file=sys.stderr)
        sys.exit(1)
    in_path, out_path = sys.argv[1], sys.argv[2]

    with open(in_path) as f:
        song = json.load(f)

    key_accidentals = KEY_SIGNATURES.get(song.get("key_signature", "C"), {})
    instruments = song.get("instruments", {})
    voice_rows = {}
    voice_instruments = {}
    for voice, tokens in song["voices"].items():
        events, note_tokens, instrument_name = parse_voice(tokens, key_accidentals)
        voice_rows[voice] = voice_to_rows(events, note_tokens)
        voice_instruments[voice] = resolve_instrument(instruments, instrument_name)

    row_count = max(len(rows) for rows in voice_rows.values())
    for voice, rows in voice_rows.items():
        if len(rows) < row_count:
            print(f"note: padding {voice} with {row_count - len(rows)} rows of rest "
                  f"(shorter than the other voices)", file=sys.stderr)
            rows.extend([NOTE_REST] * (row_count - len(rows)))

    row_frames = beats_to_row_frames(song["tempo"])

    body = bytearray()
    for i in range(row_count):
        for voice in ("soprano", "alto", "bass"):
            body.append(voice_rows[voice][i])

    header = struct.pack("<BBBH", ord("T"), 2, row_frames, row_count)
    instrument_block = b"".join(voice_instruments[voice] for voice in ("soprano", "alto", "bass"))
    with open(out_path, "wb") as f:
        f.write(header)
        f.write(instrument_block)
        f.write(body)

    print(f"{song.get('title', in_path)}: {row_count} rows, ROW_FRAMES={row_frames} "
          f"-> {out_path} ({len(header) + len(instrument_block) + len(body)} bytes)")


if __name__ == "__main__":
    main()
