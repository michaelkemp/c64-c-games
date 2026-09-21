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
    1       1     format version (1)
    2       1     ROW_FRAMES: PAL video frames per sixteenth-note row
                  (derived from tempo -- see beats_to_row_frames())
    3       2     ROW_COUNT, little-endian
    5       ROW_COUNT*3   one byte per voice per row (soprano, alto,
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

HOLD = 0xFE
NOTE_REST = 0xFF
FIRST_MIDI = 36  # C2 -- must match src/note_table.h's NOTE_C2
NUM_NOTES = 49  # C2..C6 inclusive -- must match src/note_table.h's NUM_NOTES


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
    events = []
    for tok in tokens:
        note_part, _, dur_part = tok.rpartition("-")
        if not note_part:
            raise ValueError(f"bad token {tok!r} -- expected '<note>-<duration>'")
        duration = float(dur_part)
        midi = None if note_part == "R" else parse_note(note_part, key_accidentals)
        events.append((midi, duration))
    return events


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
    voice_rows = {}
    for voice, tokens in song["voices"].items():
        events = parse_voice(tokens, key_accidentals)
        voice_rows[voice] = voice_to_rows(events, tokens)

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

    header = struct.pack("<BBBH", ord("T"), 1, row_frames, row_count)
    with open(out_path, "wb") as f:
        f.write(header)
        f.write(body)

    print(f"{song.get('title', in_path)}: {row_count} rows, ROW_FRAMES={row_frames} "
          f"-> {out_path} ({len(header) + len(body)} bytes)")


if __name__ == "__main__":
    main()
