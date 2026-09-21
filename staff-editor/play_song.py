#!/usr/bin/env python3
"""Plays a hand-written song.json -- see songs/chord_test.json for an
example and README.md for the format. No GUI; just parses and plays.

Each voice is a flat, sequential list of "<note>-<duration>" tokens
consumed one after another (no per-note timestamp needed -- a chord is
just the same duration landing at the same point in all 3 voices'
lists). <note> is a rest ("R") or a pitch: a letter A-G, an optional
accidental (# sharp, b flat, n natural -- overriding the key
signature for just this note), and an octave (scientific pitch
notation, middle C = C4). <duration> is in quarter-note beats, and can
be fractional (0.5 = an eighth note, 1.5 = a dotted quarter, ...).

Usage: play_song.py songs/chord_test.json
"""
import json
import re
import sys

import numpy as np
import pygame

SAMPLE_RATE = 44100

LETTER_ORDER = "CDEFGAB"
LETTER_TO_SEMITONE = {0: 0, 1: 2, 2: 4, 3: 5, 4: 7, 5: 9, 6: 11}

# Major-key signatures, by tonic name, as {letter: +1 (sharp) or -1 (flat)}.
# Only the keys reachable without double sharps/flats.
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


def parse_note(token, key_accidentals):
    """('C4', {}) -> 60 (MIDI); honors the key signature's default
    accidental for that letter unless the token itself has one."""
    m = NOTE_RE.match(token)
    if not m:
        raise ValueError(f"bad note {token!r} -- expected e.g. 'C4', 'F#3', 'Bb5', 'Cn4'")
    letter, accidental, octave = m.group(1), m.group(2), int(m.group(3))
    semitone = LETTER_TO_SEMITONE[LETTER_ORDER.index(letter)]
    if accidental == "#":
        semitone += 1
    elif accidental == "b":
        semitone -= 1
    elif accidental == "":  # no explicit accidental -- key signature applies
        semitone += key_accidentals.get(letter, 0)
    # accidental == "n": explicit natural, ignore key signature
    return (octave + 1) * 12 + semitone


def parse_voice(tokens, key_accidentals):
    """Returns [(midi_or_None, duration_beats), ...]; None = rest."""
    events = []
    for tok in tokens:
        note_part, _, dur_part = tok.rpartition("-")
        if not note_part:  # no '-' found at all
            raise ValueError(f"bad token {tok!r} -- expected '<note>-<duration>'")
        duration = float(dur_part)
        midi = None if note_part == "R" else parse_note(note_part, key_accidentals)
        events.append((midi, duration))
    return events


def render_tone(freq, seconds, volume=0.2):
    """Triangle wave with a short fade in/out to avoid clicks -- see
    staff_editor.py's render_tone for the same idea (not simulating
    real SID hardware, just keeping the preview pleasant)."""
    n = int(SAMPLE_RATE * seconds)
    if n <= 0:
        return np.zeros(0)
    t = np.arange(n) / SAMPLE_RATE
    phase = (t * freq) % 1.0
    wave = 2 * np.abs(2 * phase - 1) - 1
    fade = min(n // 8, int(SAMPLE_RATE * 0.01))
    if fade > 0:
        env = np.ones(n)
        env[:fade] = np.linspace(0, 1, fade)
        env[-fade:] = np.linspace(1, 0, fade)
        wave *= env
    return wave * volume


def render_song(song):
    tempo = song["tempo"]
    beat_seconds = 60.0 / tempo
    key_accidentals = KEY_SIGNATURES.get(song.get("key_signature", "C"), {})

    parsed = {voice: parse_voice(tokens, key_accidentals) for voice, tokens in song["voices"].items()}
    total_beats = max(sum(dur for _, dur in events) for events in parsed.values())
    total_samples = int(total_beats * beat_seconds * SAMPLE_RATE) + 1

    mix = np.zeros(total_samples)
    for events in parsed.values():
        t = 0.0
        for midi, duration in events:
            if midi is not None:
                start = int(t * beat_seconds * SAMPLE_RATE)
                freq = 440.0 * 2 ** ((midi - 69) / 12)
                tone = render_tone(freq, duration * beat_seconds * 0.9)
                end = min(start + len(tone), total_samples)
                mix[start:end] += tone[: end - start]
            t += duration

    peak = np.abs(mix).max()
    if peak > 0:
        mix = mix / max(peak, 1.0) * 0.8
    stereo = np.repeat((mix * 32767).astype(np.int16).reshape(-1, 1), 2, axis=1)
    return pygame.sndarray.make_sound(np.ascontiguousarray(stereo)), total_beats


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} song.json", file=sys.stderr)
        sys.exit(1)

    with open(sys.argv[1]) as f:
        song = json.load(f)

    pygame.mixer.pre_init(SAMPLE_RATE, -16, 2, 512)
    pygame.init()
    # A window, small as it is, is what lets SDL actually deliver the
    # Esc keypress below -- without one there's nothing for keyboard
    # focus to land on. Ctrl+C works either way (see the try/except),
    # this is just for Esc.
    screen = pygame.display.set_mode((420, 90))
    pygame.display.set_caption("play_song")
    font = pygame.font.SysFont("monospace", 14)
    clock = pygame.time.Clock()

    sound, total_beats = render_song(song)
    seconds = total_beats * 60.0 / song["tempo"]
    title = song.get("title", sys.argv[1])
    print(f"{title}: {total_beats:g} beats @ {song['tempo']}bpm "
          f"({seconds:.1f}s) -- playing... (Esc or Ctrl+C to stop)")
    sound.play()

    try:
        stopped_early = False
        while pygame.mixer.get_busy():
            for event in pygame.event.get():
                if event.type == pygame.QUIT or (
                        event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                    stopped_early = True
            if stopped_early:
                break
            screen.fill((255, 255, 255))
            screen.blit(font.render(f"playing: {title}", True, (0, 0, 0)), (10, 10))
            screen.blit(font.render("Esc to stop", True, (90, 90, 90)), (10, 30))
            pygame.display.flip()
            clock.tick(30)
    except KeyboardInterrupt:
        stopped_early = True

    if stopped_early:
        pygame.mixer.stop()
        print("stopped")
    pygame.quit()


if __name__ == "__main__":
    main()
