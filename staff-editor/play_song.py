#!/usr/bin/env python3
"""Plays a hand-written song.json -- see songs/chord_test.json for an
example and README.md for the format. No GUI; just parses and plays.

Each voice is a flat, sequential list of tokens consumed one after
another (no per-note timestamp needed -- a chord is just the same
duration landing at the same point in all 3 voices' lists). Most
tokens are "<note>-<duration>": <note> is a rest ("R") or a pitch: a
letter A-G, an optional accidental (# sharp, b flat, n natural --
overriding the key signature for just this note), and an octave
(scientific pitch notation, middle C = C4). <duration> is in
quarter-note beats, and can be fractional (0.5 = an eighth note, 1.5 =
a dotted quarter, ...).

A token of the form "I<name>" (e.g. "I1", "Iflute") is not a note --
it switches the voice's *current instrument* to song["instruments"][name]
from that point on, affecting every note that follows it in that
voice's list until the next instrument token. See README.md for the
instrument JSON shape (waveform + SID-style ADSR + pulse width).

Usage: play_song.py songs/chord_test.json
"""
import json
import re
import sys

import numpy as np
import pygame

SAMPLE_RATE = 44100

# Real SID envelope-generator rate tables (ms), indexed 0-15. Decay and
# release share one table on real hardware. Source: SID_Instrument_Cookbook.pdf.
ATTACK_MS = [2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000]
DECAY_RELEASE_MS = [6, 24, 48, 72, 114, 168, 204, 240, 300, 750, 1500, 2400, 3000, 9000, 15000, 24000]

DEFAULT_INSTRUMENT = {"waveform": "triangle", "adsr": "00F0", "pulse_width": 0.5}

INSTRUMENT_TOKEN_RE = re.compile(r"^I(\w+)$")

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
    """Returns [(midi_or_None, duration_beats, instrument_name_or_None), ...];
    None midi = rest. instrument_name_or_None tracks the most recent "I<name>"
    token seen in this voice (None until the first one appears)."""
    events = []
    instrument_name = None
    for tok in tokens:
        m = INSTRUMENT_TOKEN_RE.match(tok)
        if m:
            instrument_name = m.group(1)
            continue
        note_part, _, dur_part = tok.rpartition("-")
        if not note_part:  # no '-' found at all
            raise ValueError(f"bad token {tok!r} -- expected '<note>-<duration>' or 'I<name>'")
        duration = float(dur_part)
        midi = None if note_part == "R" else parse_note(note_part, key_accidentals)
        events.append((midi, duration, instrument_name))
    return events


def parse_adsr(adsr_hex):
    """'$09A0'-style code (attack, decay, sustain, release as hex nibbles)
    -> (attack_s, decay_s, sustain_level 0-1, release_s)."""
    hexits = adsr_hex.lstrip("$")
    if len(hexits) != 4:
        raise ValueError(f"bad adsr {adsr_hex!r} -- expected 4 hex digits, e.g. '09A0'")
    a, d, s, r = (int(c, 16) for c in hexits)
    return ATTACK_MS[a] / 1000.0, DECAY_RELEASE_MS[d] / 1000.0, s / 15.0, DECAY_RELEASE_MS[r] / 1000.0


def resolve_instrument(instruments, name):
    """name=None (no "I<name>" token seen yet) -> a plain, always-on
    default so songs without instruments sound as they did before this
    feature existed."""
    if name is None:
        spec = DEFAULT_INSTRUMENT
    elif name not in instruments:
        raise ValueError(f"unknown instrument {name!r} -- add it to the song's \"instruments\"")
    else:
        spec = instruments[name]
    return {
        "waveform": spec.get("waveform", "triangle"),
        "adsr": parse_adsr(spec.get("adsr", "00F0")),
        "pulse_width": spec.get("pulse_width", 0.5),
        "filter_cutoff": spec.get("filter_cutoff"),
        "filter_resonance": spec.get("filter_resonance", 0.0),
        "filter_mode": spec.get("filter_mode", "lowpass"),
    }


def render_waveform(waveform, freq, t, pulse_width):
    phase = (t * freq) % 1.0
    if waveform == "triangle":
        return 2 * np.abs(2 * phase - 1) - 1
    if waveform in ("saw", "sawtooth"):
        return 2 * phase - 1
    if waveform in ("pulse", "square"):
        return np.where(phase < pulse_width, 1.0, -1.0)
    if waveform == "noise":
        # Real SID noise is a pitched LFSR, not this -- an unpitched random
        # approximation is enough for the percussion recipes in the cookbook.
        return np.random.uniform(-1.0, 1.0, len(t))
    raise ValueError(f"unknown waveform {waveform!r}")


def exp_ramp(n, start, target):
    """A decaying-exponential ramp from start to target over n samples --
    approximates the real SID's decay/release behavior, where the
    envelope counter's own decrement rate slows down as the level drops
    (fast initial movement, long settling tail), instead of a straight
    line. tau is picked so the ramp is ~99% of the way to target by
    sample n; this single-time-constant curve doesn't reproduce the
    chip's exact piecewise rate table, but is much closer to it than a
    linear fade."""
    if n <= 0:
        return np.zeros(0)
    tau = n / 4.6  # exp(-4.6) ~= 0.01
    return target + (start - target) * np.exp(-np.arange(n) / tau)


def render_envelope(n, attack_s, decay_s, sustain_level, release_s):
    """Fits attack/decay/sustain-hold/release into exactly n samples,
    compressing the phases proportionally if the note is too short for
    them (a real SID would instead cut the gate and let release run past
    the note's nominal length, but this keeps every note self-contained).
    Attack is linear (that phase really is, on real hardware); decay and
    release use exp_ramp."""
    a_n = int(SAMPLE_RATE * attack_s)
    d_n = int(SAMPLE_RATE * decay_s)
    r_n = int(SAMPLE_RATE * release_s)
    if a_n + d_n + r_n > n:
        scale = n / max(a_n + d_n + r_n, 1)
        a_n, d_n, r_n = int(a_n * scale), int(d_n * scale), int(r_n * scale)
    hold_n = n - a_n - d_n - r_n

    env = np.empty(n)
    idx = 0
    if a_n:
        env[idx:idx + a_n] = np.linspace(0.0, 1.0, a_n, endpoint=False)
        idx += a_n
    if d_n:
        env[idx:idx + d_n] = exp_ramp(d_n, 1.0, sustain_level)
        idx += d_n
    if hold_n:
        env[idx:idx + hold_n] = sustain_level
        idx += hold_n
    if r_n:
        env[idx:idx + r_n] = exp_ramp(r_n, sustain_level, 0.0)
    return env


def apply_filter(wave, cutoff_hz, resonance=0.0, mode="lowpass"):
    """A resonant 2-pole state-variable filter (Chamberlin topology) --
    a rough stand-in for the SID's real multimode filter, which (like
    this one) derives low-pass, high-pass, and band-pass taps from the
    same core and lets you pick one. Not a model of the real filter's
    exact response -- just enough to round off (lowpass), thin out
    (highpass), or narrow (bandpass) a raw pulse/saw edge. resonance is
    0 (gentle) to just under 1 (ringing, can self-oscillate near 1 --
    kept below that here)."""
    n = len(wave)
    if n == 0 or not cutoff_hz:
        return wave
    if mode not in ("lowpass", "highpass", "bandpass"):
        raise ValueError(f"unknown filter_mode {mode!r}")
    f = 2 * np.sin(np.pi * min(cutoff_hz, SAMPLE_RATE * 0.49) / SAMPLE_RATE)
    damping = max(1.0 - min(resonance, 0.99), 0.02)
    low = 0.0
    band = 0.0
    out = np.empty(n)
    for i, x in enumerate(wave):
        high = x - low - damping * band
        band += f * high
        low += f * band
        out[i] = low if mode == "lowpass" else high if mode == "highpass" else band
    return out


def render_note(freq, seconds, instrument, volume=0.2):
    """Approximates a SID voice: instrument's waveform, optionally
    filtered, shaped by its ADSR envelope (not simulating real SID
    hardware, just keeping the preview pleasant and roughly evocative of
    the intended instrument)."""
    n = int(SAMPLE_RATE * seconds)
    if n <= 0:
        return np.zeros(0)
    t = np.arange(n) / SAMPLE_RATE
    wave = render_waveform(instrument["waveform"], freq, t, instrument["pulse_width"])
    wave = apply_filter(wave, instrument["filter_cutoff"], instrument["filter_resonance"], instrument["filter_mode"])
    attack_s, decay_s, sustain_level, release_s = instrument["adsr"]
    env = render_envelope(n, attack_s, decay_s, sustain_level, release_s)
    return wave * env * volume


def render_song(song):
    tempo = song["tempo"]
    beat_seconds = 60.0 / tempo
    key_accidentals = KEY_SIGNATURES.get(song.get("key_signature", "C"), {})
    instruments = song.get("instruments", {})

    parsed = {voice: parse_voice(tokens, key_accidentals) for voice, tokens in song["voices"].items()}
    total_beats = max(sum(dur for _, dur, _ in events) for events in parsed.values())
    total_samples = int(total_beats * beat_seconds * SAMPLE_RATE) + 1

    mix = np.zeros(total_samples)
    for events in parsed.values():
        t = 0.0
        for midi, duration, instrument_name in events:
            if midi is not None:
                start = int(t * beat_seconds * SAMPLE_RATE)
                freq = 440.0 * 2 ** ((midi - 69) / 12)
                instrument = resolve_instrument(instruments, instrument_name)
                tone = render_note(freq, duration * beat_seconds * 0.9, instrument)
                end = min(start + len(tone), total_samples)
                mix[start:end] += tone[: end - start]
            t += duration

    peak = np.abs(mix).max()
    if peak > 0:
        mix = mix / max(peak, 1.0) * 0.8
    stereo = np.repeat((mix * 32767).astype(np.int16).reshape(-1, 1), 2, axis=1)
    sound = pygame.sndarray.make_sound(np.ascontiguousarray(stereo))
    return sound, total_beats, instrument_change_events(parsed, instruments, beat_seconds)


def instrument_change_events(parsed, instruments, beat_seconds):
    """[(start_seconds, voice_name, instrument_display_name), ...], one
    entry per point where a voice's current instrument changes (the
    first note/rest of the song counts as a change from "default"),
    sorted by time -- lets a player announce what's playing as playback
    reaches that point, without needing per-note timestamps stored in
    the song itself."""
    events = []
    for voice_name, voice_events in parsed.items():
        t = 0.0
        last_name = None
        for _, duration, instrument_name in voice_events:
            display = "default" if instrument_name is None else \
                instruments.get(instrument_name, {}).get("name", instrument_name)
            if display != last_name:
                events.append((t * beat_seconds, voice_name, display))
                last_name = display
            t += duration
    events.sort(key=lambda e: e[0])
    return events


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
    screen = pygame.display.set_mode((480, 110))
    pygame.display.set_caption("play_song")
    font = pygame.font.SysFont("monospace", 14)
    clock = pygame.time.Clock()

    sound, total_beats, announce_events = render_song(song)
    seconds = total_beats * 60.0 / song["tempo"]
    title = song.get("title", sys.argv[1])
    print(f"{title}: {total_beats:g} beats @ {song['tempo']}bpm "
          f"({seconds:.1f}s) -- playing... (Esc or Ctrl+C to stop)")
    if song.get("instruments"):
        print("Instruments:")
        for key, spec in song["instruments"].items():
            print(f"  I{key}: {spec.get('name', key)}")
    sound.play()
    start_ticks = pygame.time.get_ticks()

    try:
        stopped_early = False
        current_label = "default"
        next_event = 0
        while pygame.mixer.get_busy():
            for event in pygame.event.get():
                if event.type == pygame.QUIT or (
                        event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                    stopped_early = True
            if stopped_early:
                break
            # No sample-accurate playback position is available from a
            # Sound/Channel (that's only exposed for pygame.mixer.music) --
            # wall-clock time since play() started is accurate enough for
            # this console/on-screen readout.
            pos_s = (pygame.time.get_ticks() - start_ticks) / 1000.0
            while next_event < len(announce_events) and announce_events[next_event][0] <= pos_s:
                _, voice_name, current_label = announce_events[next_event]
                print(f"  [{pos_s:5.1f}s] {voice_name}: {current_label}")
                next_event += 1
            screen.fill((255, 255, 255))
            screen.blit(font.render(f"playing: {title}", True, (0, 0, 0)), (10, 10))
            screen.blit(font.render(f"now: {current_label}", True, (0, 90, 0)), (10, 30))
            screen.blit(font.render("Esc to stop", True, (90, 90, 90)), (10, 50))
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
