#!/usr/bin/env python3
"""A point-and-click 3-voice staff editor for composing SID arrangements.

Composes directly in the same row/voice model the c64-c-games SID
player songs use (one row = one sixteenth note, HOLD/REST implicit),
so a saved song.json is a short step from a project's pattern.c --
see README.md.

Usage: staff_editor.py [song.json]
    Loads song.json if it exists; Ctrl+S saves back to that path
    (default: songs/untitled.json).

Controls:
    1/2/3        select active voice (soprano/alto/bass)
    4-8          set note duration: whole/half/quarter/eighth/sixteenth
                 (also resizes the selected note, if any)
    A-G          enter a note of that letter at the cursor, in the
                 octave closest to the voice's previous note, then
                 advance the cursor by the current duration
    R            rest at the cursor, then advance
    left click   place a note at that pitch/beat (current duration),
                 or select an existing one
    up/down      nudge the selected note by a semitone
    delete       remove the selected note
    left/right   move the cursor by one duration-step
    [ / ]        jump the cursor back/forward one measure
    k            cycle key signature (0-7 sharps)
    t            cycle time signature (4/4, 3/4, 2/4, 6/8)
    m            append one measure at the end
    space        play back the whole piece
    ctrl+s       save
    ctrl+o       reload from disk (discards unsaved changes)
    esc          quit
"""
import json
import os
import sys

import numpy as np
import pygame

DEFAULT_MEASURES = 8
SAMPLE_RATE = 44100
BPM = 120

VOICES = ("soprano", "alto", "bass")
VOICE_COLOR = {
    "soprano": (40, 90, 220),
    "alto": (30, 150, 60),
    "bass": (200, 40, 40),
}
DEFAULT_OCTAVE = {"soprano": 4, "alto": 4, "bass": 3}

DURATION_KEYS = None  # filled in after pygame import-time constants exist (see main())
DURATION_NAMES = {16: "whole", 8: "half", 4: "quarter", 2: "eighth", 1: "sixteenth"}

TIME_SIGNATURES = [(4, 4), (3, 4), (2, 4), (6, 8)]

LETTER_ORDER = "CDEFGAB"
SHARP_ORDER = "FCGDAEB"  # order sharps are added going around the circle of fifths
MAJOR_KEY_NAMES = ["C", "G", "D", "A", "E", "B", "F#", "C#"]  # indexed by sharps count 0-7

# semitone-within-octave -> (letter index into LETTER_ORDER, is_sharp)
SEMITONE_TO_LETTER = {
    0: (0, False), 1: (0, True), 2: (1, False), 3: (1, True), 4: (2, False),
    5: (3, False), 6: (3, True), 7: (4, False), 8: (4, True), 9: (5, False),
    10: (5, True), 11: (6, False),
}
LETTER_TO_SEMITONE = {0: 0, 1: 2, 2: 4, 3: 5, 4: 7, 5: 9, 6: 11}
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def midi_to_name(midi):
    return f"{NOTE_NAMES[midi % 12]}{midi // 12 - 1}"


def name_to_midi(name):
    """Parses names like 'G4' or 'C#4' (sharps only, matching this
    project's note_table.h convention -- see gen_note_table.py in the
    song directories)."""
    if name[1] == "#":
        letter, accidental, octave = name[0], "#", int(name[2:])
    else:
        letter, accidental, octave = name[0], "", int(name[1:])
    semitone = LETTER_TO_SEMITONE[LETTER_ORDER.index(letter)] + (1 if accidental else 0)
    return (octave + 1) * 12 + semitone


def midi_to_diatonic(midi):
    """Returns (diatonic_index, is_sharp). diatonic_index increases by
    1 per natural letter step (C->D->E->...), independent of
    accidental -- this is what determines the note's *line or space*
    on the staff; is_sharp just adds a # glyph at that position."""
    octave, semitone = midi // 12 - 1, midi % 12
    letter_index, is_sharp = SEMITONE_TO_LETTER[semitone]
    return octave * 7 + letter_index, is_sharp


def diatonic_to_midi(diatonic_index, key_sharps=0):
    """Inverse of midi_to_diatonic for a *natural-letter* position --
    used when placing a brand new note by click position. Applies the
    key signature's default sharp for that letter, same as typed note
    entry (see nearest_letter_midi)."""
    octave, letter_index = divmod(diatonic_index, 7)
    letter = LETTER_ORDER[letter_index]
    semitone = LETTER_TO_SEMITONE[letter_index]
    if letter in SHARP_ORDER[:key_sharps]:
        semitone += 1
    return (octave + 1) * 12 + semitone


def nearest_letter_midi(letter, key_sharps, reference_midi):
    """The letter-entry keys (A-G) don't say which octave -- pick
    whichever octave of that letter lands closest to reference_midi
    (the voice's previous note, or a sensible default), same idea as
    real notation software's note-input mode."""
    semitone = LETTER_TO_SEMITONE[LETTER_ORDER.index(letter)]
    if letter in SHARP_ORDER[:key_sharps]:
        semitone += 1
    ref_octave = reference_midi // 12 - 1
    candidates = [(ref_octave + delta + 1) * 12 + semitone for delta in (-1, 0, 1)]
    return min(candidates, key=lambda m: abs(m - reference_midi))


class Note:
    def __init__(self, row, midi, duration):
        self.row = row
        self.midi = midi
        self.duration = duration


class Song:
    def __init__(self, measures=DEFAULT_MEASURES):
        self.title = "untitled"
        self.measures = measures
        self.time_signature = (4, 4)
        self.key_signature = 0  # sharps, 0-7
        self.voices = {v: [] for v in VOICES}  # each: list[Note], sorted by row

    def rows_per_measure(self):
        num, den = self.time_signature
        return num * (16 // den)

    def total_rows(self):
        return self.measures * self.rows_per_measure()

    def note_at(self, voice, row):
        for n in self.voices[voice]:
            if n.row <= row < n.row + n.duration:
                return n
        return None

    def last_note_before(self, voice, row):
        candidates = [n for n in self.voices[voice] if n.row < row]
        return max(candidates, key=lambda n: n.row) if candidates else None

    def place_note(self, voice, row, midi, duration):
        """Removes any note(s) of this voice overlapping [row, row+dur)
        and inserts the new one, keeping the voice's list sorted."""
        self.voices[voice] = [n for n in self.voices[voice]
                               if n.row + n.duration <= row or n.row >= row + duration]
        note = Note(row, midi, duration)
        self.voices[voice].append(note)
        self.voices[voice].sort(key=lambda n: n.row)
        return note

    def resize_note(self, voice, note, duration):
        """Like place_note, but for a note that's already in the
        voice -- clears anything else it now overlaps."""
        self.voices[voice].remove(note)
        return self.place_note(voice, note.row, note.midi, duration)

    def delete_note(self, voice, note):
        self.voices[voice].remove(note)

    def to_dict(self):
        return {
            "title": self.title,
            "time_signature": list(self.time_signature),
            "key_signature_sharps": self.key_signature,
            "rows_per_measure": self.rows_per_measure(),
            "measures": self.measures,
            "voices": {
                v: [{"row": n.row, "pitch": midi_to_name(n.midi), "duration": n.duration}
                    for n in notes]
                for v, notes in self.voices.items()
            },
        }

    @classmethod
    def from_dict(cls, d):
        song = cls(measures=d.get("measures", DEFAULT_MEASURES))
        song.title = d.get("title", "untitled")
        ts = d.get("time_signature", [4, 4])
        song.time_signature = (ts[0], ts[1])
        song.key_signature = d.get("key_signature_sharps", 0)
        for v in VOICES:
            song.voices[v] = [Note(n["row"], name_to_midi(n["pitch"]), n.get("duration", 4))
                               for n in d.get("voices", {}).get(v, [])]
        return song

    def save(self, path):
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        with open(path, "w") as f:
            json.dump(self.to_dict(), f, indent=2)

    @classmethod
    def load(cls, path):
        with open(path) as f:
            return cls.from_dict(json.load(f))


# --- Staff geometry -----------------------------------------------------

LINE_SPACING = 12  # pixels between adjacent staff lines
STEP = LINE_SPACING // 2  # pixels per diatonic step (see midi_to_diatonic)
TREBLE_BOTTOM_Y = 260  # screen y of the treble staff's bottom line (E4)
TREBLE_BOTTOM_DIATONIC = 4 * 7 + 2  # E4

ROW_WIDTH = 22
STAFF_LEFT_MARGIN = 90
MEASURE_LINE_COLOR = (60, 60, 60)
STAFF_LINE_COLOR = (20, 20, 20)
CURSOR_COLOR = (150, 150, 150)
BG_COLOR = (255, 255, 255)


def diatonic_to_y(diatonic_index):
    return TREBLE_BOTTOM_Y - (diatonic_index - TREBLE_BOTTOM_DIATONIC) * STEP


def y_to_diatonic(y):
    return round(TREBLE_BOTTOM_DIATONIC - (y - TREBLE_BOTTOM_Y) / STEP)


TREBLE_LINE_DIATONICS = [TREBLE_BOTTOM_DIATONIC + 2 * i for i in range(5)]  # E,G,B,D,F
# Bass bottom line (G2) sits exactly 2 octaves+ down; derive from the
# same continuous diatonic scale so the grand staff spacing (middle C
# exactly between the two staves) falls out automatically.
BASS_BOTTOM_DIATONIC = 2 * 7 + 4  # G2
BASS_LINE_DIATONICS = [BASS_BOTTOM_DIATONIC + 2 * i for i in range(5)]


def row_to_x(row, scroll_row):
    return STAFF_LEFT_MARGIN + (row - scroll_row) * ROW_WIDTH


def x_to_row(x, scroll_row):
    return scroll_row + round((x - STAFF_LEFT_MARGIN) / ROW_WIDTH)


# --- Audio ---------------------------------------------------------------

def render_tone(freq, seconds, volume=0.2):
    """A triangle wave with a short fade in/out -- avoids the click-on-
    attack/release problems this whole family of projects spent a long
    session chasing on real SID hardware; here it's just to keep the
    preview pleasant, not simulating the chip."""
    n = int(SAMPLE_RATE * seconds)
    t = np.arange(n) / SAMPLE_RATE
    phase = (t * freq) % 1.0
    wave = 2 * np.abs(2 * phase - 1) - 1  # triangle, range [-1, 1]
    fade = min(n // 8, int(SAMPLE_RATE * 0.01))
    if fade > 0:
        env = np.ones(n)
        env[:fade] = np.linspace(0, 1, fade)
        env[-fade:] = np.linspace(1, 0, fade)
        wave = wave * env
    return wave * volume


def render_song(song):
    row_seconds = 60.0 / BPM / 4  # one sixteenth note
    total_samples = int(song.total_rows() * row_seconds * SAMPLE_RATE)
    mix = np.zeros(total_samples)
    for voice in VOICES:
        for note in song.voices[voice]:
            start = int(note.row * row_seconds * SAMPLE_RATE)
            seconds = note.duration * row_seconds * 0.9  # small gap between notes
            freq = 440.0 * 2 ** ((note.midi - 69) / 12)
            tone = render_tone(freq, seconds)
            end = min(start + len(tone), total_samples)
            mix[start:end] += tone[: end - start]
    peak = np.abs(mix).max()
    if peak > 0:
        mix = mix / max(peak, 1.0) * 0.8
    stereo = np.repeat((mix * 32767).astype(np.int16).reshape(-1, 1), 2, axis=1)
    return pygame.sndarray.make_sound(np.ascontiguousarray(stereo))


# --- Drawing ---------------------------------------------------------------

def draw_staff_lines(screen, line_diatonics, scroll_row, visible_rows):
    x0 = row_to_x(scroll_row, scroll_row)
    x1 = row_to_x(scroll_row + visible_rows, scroll_row)
    for d in line_diatonics:
        y = diatonic_to_y(d)
        pygame.draw.line(screen, STAFF_LINE_COLOR, (x0, y), (x1, y), 1)


def draw_measure_lines(screen, song, scroll_row, visible_rows, top_y, bottom_y):
    rpm = song.rows_per_measure()
    first_measure = scroll_row // rpm
    last_measure = (scroll_row + visible_rows) // rpm + 1
    for m in range(first_measure, min(last_measure, song.measures) + 1):
        row = m * rpm
        if row == 0:
            continue  # no barline before the very first note -- only after each measure
        x = row_to_x(row, scroll_row)
        pygame.draw.line(screen, MEASURE_LINE_COLOR, (x, top_y), (x, bottom_y), 1)


def draw_ledger_lines(screen, x, diatonic_index, staff_line_diatonics):
    lo, hi = min(staff_line_diatonics), max(staff_line_diatonics)
    step = 2  # ledger lines are 2 diatonic steps apart, same as staff lines
    if diatonic_index > hi:
        d = hi + step
        while d <= diatonic_index:
            y = diatonic_to_y(d)
            pygame.draw.line(screen, STAFF_LINE_COLOR, (x - 10, y), (x + 10, y), 1)
            d += step
    elif diatonic_index < lo:
        d = lo - step
        while d >= diatonic_index:
            y = diatonic_to_y(d)
            pygame.draw.line(screen, STAFF_LINE_COLOR, (x - 10, y), (x + 10, y), 1)
            d -= step


STEM_LENGTH = 30
FLAG_STEP = 8  # vertical spacing between a note's stacked flags


def draw_note(screen, font, note, voice, scroll_row, selected, staff_line_diatonics):
    x = row_to_x(note.row, scroll_row)
    if x < STAFF_LEFT_MARGIN - ROW_WIDTH:
        return
    diatonic, is_sharp = midi_to_diatonic(note.midi)
    y = diatonic_to_y(diatonic)
    draw_ledger_lines(screen, x, diatonic, staff_line_diatonics)
    color = VOICE_COLOR[voice]
    if selected:
        pygame.draw.circle(screen, (255, 210, 60), (x, y), STEP + 3, 3)

    # Notehead: open for half/whole, filled for quarter/eighth/sixteenth
    # -- same convention real notation uses.
    rect = (x - STEP, y - STEP + 2, STEP * 2, STEP * 2 - 4)
    if note.duration >= 8:
        pygame.draw.ellipse(screen, color, rect, 2)
    else:
        pygame.draw.ellipse(screen, color, rect)

    # Stem + flags: this (not the notehead shape) is what actually
    # tells quarter/eighth/sixteenth apart, since they share a filled
    # head. Whole notes get neither. Direction follows standard
    # notation: notes at or above the staff's middle line stem down
    # (on the left of the head), below it stem up (on the right).
    if note.duration < 16:
        middle = staff_line_diatonics[2]
        stem_up = diatonic < middle
        stem_x = x + STEP - 1 if stem_up else x - STEP + 1
        tip_y = y - STEM_LENGTH if stem_up else y + STEM_LENGTH
        pygame.draw.line(screen, color, (stem_x, y), (stem_x, tip_y), 2)

        flags = {2: 1, 1: 2}.get(note.duration, 0)
        for i in range(flags):
            fy = tip_y + (i * FLAG_STEP if stem_up else -i * FLAG_STEP)
            dx, dy = (10, 10) if stem_up else (-10, -10)
            pygame.draw.line(screen, color, (stem_x, fy), (stem_x + dx, fy + dy), 2)

    if is_sharp:
        label = font.render("#", True, color)
        screen.blit(label, (x - STEP - 12, y - 8))


def draw_cursor(screen, song, voice, cursor_row, scroll_row):
    x = row_to_x(cursor_row, scroll_row)
    staff_diatonics = TREBLE_LINE_DIATONICS if voice != "bass" else BASS_LINE_DIATONICS
    y0 = diatonic_to_y(max(staff_diatonics)) - 10
    y1 = diatonic_to_y(min(staff_diatonics)) + 10
    pygame.draw.line(screen, VOICE_COLOR[voice], (x, y0), (x, y1), 1)


def main():
    global DURATION_KEYS

    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(__file__), "songs", "untitled.json")

    pygame.mixer.pre_init(SAMPLE_RATE, -16, 2, 512)
    pygame.init()
    # Not function keys: those get intercepted by window managers and
    # laptop media-key overlays way too often to be reliable (F2 in
    # particular closed the window outright when this was tried).
    DURATION_KEYS = {
        pygame.K_4: 16, pygame.K_5: 8, pygame.K_6: 4, pygame.K_7: 2, pygame.K_8: 1,
    }
    LETTER_KEYS = {
        pygame.K_a: "A", pygame.K_b: "B", pygame.K_c: "C", pygame.K_d: "D",
        pygame.K_e: "E", pygame.K_f: "F", pygame.K_g: "G",
    }
    screen = pygame.display.set_mode((1100, 500))
    pygame.display.set_caption("staff-editor")
    font = pygame.font.SysFont("monospace", 16)
    small_font = pygame.font.SysFont("monospace", 13)
    clock = pygame.time.Clock()

    song = Song.load(path) if os.path.exists(path) else Song()

    active_voice = "soprano"
    current_duration = 4
    scroll_row = 0
    cursor_row = {v: 0 for v in VOICES}
    selected = None  # (voice, Note)
    status = f"loaded {path}" if os.path.exists(path) else f"new song -> {path}"

    def ensure_visible(row, visible_rows):
        nonlocal scroll_row
        if row < scroll_row:
            scroll_row = max(0, row - 1)
        elif row >= scroll_row + visible_rows:
            scroll_row = row - visible_rows + 2

    def sync_selection():
        """Selects whatever note (if any) sits exactly at the active
        voice's cursor, so navigating there with the keyboard -- not
        just clicking -- is enough to make it "the current note" for
        Up/Down, Delete, and the duration keys. Without this, moving
        the cursor onto a note left `selected` pointing at whatever
        was clicked or typed last (or nothing), so duration/pitch
        edits either did nothing or kept hitting a stale note.
        Deliberately not called after letter-entry/rest/click: those
        already set `selected` explicitly and move the cursor *past*
        the note they just touched, where this would just clear it."""
        nonlocal selected, status
        note = song.note_at(active_voice, cursor_row[active_voice])
        selected = (active_voice, note) if note else None
        if note:
            status = f"on {active_voice} {midi_to_name(note.midi)} @ row {note.row}"

    running = True
    while running:
        visible_rows = (screen.get_width() - STAFF_LEFT_MARGIN - 20) // ROW_WIDTH

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False

            elif event.type == pygame.KEYDOWN:
                mods = pygame.key.get_mods()
                if event.key == pygame.K_ESCAPE:
                    running = False
                elif event.key == pygame.K_1:
                    active_voice = "soprano"
                    sync_selection()
                elif event.key == pygame.K_2:
                    active_voice = "alto"
                    sync_selection()
                elif event.key == pygame.K_3:
                    active_voice = "bass"
                    sync_selection()
                elif event.key in DURATION_KEYS:
                    current_duration = DURATION_KEYS[event.key]
                    status = f"duration: {DURATION_NAMES[current_duration]}"
                    if selected:
                        v, n = selected
                        selected = (v, song.resize_note(v, n, current_duration))
                        status += f" (resized selected note)"
                elif event.key in LETTER_KEYS:
                    letter = LETTER_KEYS[event.key]
                    row = cursor_row[active_voice]
                    if row < song.total_rows():
                        prev = song.last_note_before(active_voice, row)
                        ref = prev.midi if prev else (DEFAULT_OCTAVE[active_voice] + 1) * 12
                        midi = nearest_letter_midi(letter, song.key_signature, ref)
                        note = song.place_note(active_voice, row, midi, current_duration)
                        selected = (active_voice, note)
                        cursor_row[active_voice] = row + current_duration
                        ensure_visible(cursor_row[active_voice], visible_rows)
                        status = f"{active_voice}: {midi_to_name(midi)} @ row {row}"
                elif event.key == pygame.K_r:
                    row = cursor_row[active_voice]
                    existing = song.note_at(active_voice, row)
                    if existing:
                        song.delete_note(active_voice, existing)
                        if selected == (active_voice, existing):
                            selected = None
                    cursor_row[active_voice] = row + current_duration
                    ensure_visible(cursor_row[active_voice], visible_rows)
                    status = f"rest @ row {row}"
                elif event.key == pygame.K_LEFT:
                    cursor_row[active_voice] = max(0, cursor_row[active_voice] - current_duration)
                    ensure_visible(cursor_row[active_voice], visible_rows)
                    sync_selection()
                elif event.key == pygame.K_RIGHT:
                    cursor_row[active_voice] = min(song.total_rows() - 1,
                                                    cursor_row[active_voice] + current_duration)
                    ensure_visible(cursor_row[active_voice], visible_rows)
                    sync_selection()
                elif event.key == pygame.K_LEFTBRACKET:
                    rpm = song.rows_per_measure()
                    cursor_row[active_voice] = max(0, cursor_row[active_voice] - rpm)
                    ensure_visible(cursor_row[active_voice], visible_rows)
                    sync_selection()
                elif event.key == pygame.K_RIGHTBRACKET:
                    rpm = song.rows_per_measure()
                    cursor_row[active_voice] = min(song.total_rows() - 1, cursor_row[active_voice] + rpm)
                    ensure_visible(cursor_row[active_voice], visible_rows)
                    sync_selection()
                elif event.key == pygame.K_k:
                    song.key_signature = (song.key_signature + 1) % 8
                    status = f"key: {MAJOR_KEY_NAMES[song.key_signature]} major ({song.key_signature} sharps)"
                elif event.key == pygame.K_t:
                    idx = TIME_SIGNATURES.index(song.time_signature)
                    song.time_signature = TIME_SIGNATURES[(idx + 1) % len(TIME_SIGNATURES)]
                    status = f"time signature: {song.time_signature[0]}/{song.time_signature[1]}"
                elif event.key == pygame.K_m:
                    song.measures += 1
                    status = f"added measure -- now {song.measures}"
                elif event.key == pygame.K_SPACE:
                    render_song(song).play()
                    status = "playing..."
                elif event.key in (pygame.K_DELETE, pygame.K_BACKSPACE) and selected:
                    v, n = selected
                    song.delete_note(v, n)
                    selected = None
                    status = "deleted note"
                elif event.key == pygame.K_UP and selected:
                    v, n = selected
                    n.midi += 1
                    status = f"{v} -> {midi_to_name(n.midi)}"
                elif event.key == pygame.K_DOWN and selected:
                    v, n = selected
                    n.midi -= 1
                    status = f"{v} -> {midi_to_name(n.midi)}"
                elif event.key == pygame.K_s and mods & pygame.KMOD_CTRL:
                    song.save(path)
                    status = f"saved {path}"
                elif event.key == pygame.K_o and mods & pygame.KMOD_CTRL:
                    if os.path.exists(path):
                        song = Song.load(path)
                        selected = None
                        status = f"reloaded {path}"
                    else:
                        status = f"no file at {path} yet"

            elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                mx, my = event.pos
                row = x_to_row(mx, scroll_row)
                if 0 <= row < song.total_rows() and mx >= STAFF_LEFT_MARGIN - ROW_WIDTH // 2:
                    existing = song.note_at(active_voice, row)
                    if existing is not None:
                        selected = (active_voice, existing)
                        cursor_row[active_voice] = existing.row + existing.duration
                        status = f"selected {active_voice} {midi_to_name(existing.midi)} @ row {existing.row}"
                    else:
                        diatonic = y_to_diatonic(my)
                        midi = diatonic_to_midi(diatonic, song.key_signature)
                        note = song.place_note(active_voice, row, midi, current_duration)
                        selected = (active_voice, note)
                        cursor_row[active_voice] = row + current_duration
                        status = f"placed {active_voice} {midi_to_name(midi)} @ row {row}"

        screen.fill(BG_COLOR)
        draw_staff_lines(screen, TREBLE_LINE_DIATONICS, scroll_row, visible_rows)
        draw_staff_lines(screen, BASS_LINE_DIATONICS, scroll_row, visible_rows)
        top_y = diatonic_to_y(max(TREBLE_LINE_DIATONICS)) - 20
        bottom_y = diatonic_to_y(min(BASS_LINE_DIATONICS)) + 20
        draw_measure_lines(screen, song, scroll_row, visible_rows, top_y, bottom_y)
        draw_cursor(screen, song, active_voice, cursor_row[active_voice], scroll_row)

        for voice in VOICES:
            staff_diatonics = TREBLE_LINE_DIATONICS if voice != "bass" else BASS_LINE_DIATONICS
            for note in song.voices[voice]:
                is_sel = selected is not None and selected[0] == voice and selected[1] is note
                draw_note(screen, font, note, voice, scroll_row, is_sel, staff_diatonics)

        clef_label = font.render("treble (S/A)", True, (0, 0, 0))
        screen.blit(clef_label, (10, diatonic_to_y(max(TREBLE_LINE_DIATONICS)) - 8))
        clef_label2 = font.render("bass (B)", True, (0, 0, 0))
        screen.blit(clef_label2, (10, diatonic_to_y(max(BASS_LINE_DIATONICS)) - 8))

        num, den = song.time_signature
        header = (f"voice: {active_voice}   duration: {DURATION_NAMES[current_duration]}   "
                  f"key: {MAJOR_KEY_NAMES[song.key_signature]} ({song.key_signature}#)   "
                  f"time: {num}/{den}   measures: {song.measures}")
        screen.blit(small_font.render(header, True, (0, 0, 0)), (10, 10))
        screen.blit(small_font.render(status, True, (90, 90, 90)), (10, 30))

        for i, voice in enumerate(VOICES):
            legend = small_font.render(voice, True, VOICE_COLOR[voice])
            screen.blit(legend, (900, 10 + i * 16))

        pygame.display.flip()
        clock.tick(60)

    pygame.quit()


if __name__ == "__main__":
    main()
