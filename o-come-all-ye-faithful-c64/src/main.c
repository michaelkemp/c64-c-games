#include <c64.h>

#ifdef STEP_MODE
#include <conio.h>
#endif

#include "sid.h"
#include "note_table.h"
#include "pattern.h"

#ifdef STEP_MODE
/* Index-matched to the NOTE_* enum in note_table.h (C2..C6), for
   printing what's actually playing in step mode. */
static const char *note_name[NUM_NOTES] = {
    "C2", "C#2", "D2", "D#2", "E2", "F2", "F#2", "G2", "G#2", "A2", "A#2", "B2",
    "C3", "C#3", "D3", "D#3", "E3", "F3", "F#3", "G3", "G#3", "A3", "A#3", "B3",
    "C4", "C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4", "A4", "A#4", "B4",
    "C5", "C#5", "D5", "D#5", "E5", "F5", "F#5", "G5", "G#5", "A5", "A#5", "B5",
    "C6",
};
#endif

/* Frames of silence at the tail of a row whenever the following row
   isn't a HOLD for that voice -- keeps consecutive same-pitch notes
   (e.g. the repeated G's this arrangement opens on) audible as
   separate notes instead of one continuous tone. Small relative to
   ROW_FRAMES=7 (sixteenth-note rows leave much less room than
   sid-player-c64's quarter-note ones did) but still enough to be a
   clean gap. */
#ifndef GATE_GAP_FRAMES
#define GATE_GAP_FRAMES 2
#endif

/* Attack/decay/sustain/release pairs for SID.vN.ad/.sr.

   All four of attack, decay, and release are rate 0 (fastest) on both
   voices -- not because that's the desired *sound* on its own, but to
   avoid the SID envelope generator's real, documented "ADSR delay
   bug" (codebase64: base:classic_hard-restart_and_about_adsr_in_generally).
   The envelope's rate timer is a free-running counter that just keeps
   watching for a match against whatever the current A/D/R rate
   implies; dropping to a *smaller* rate than the one just active
   (equal or larger is fine) usually means the counter already ran
   past that smaller value's match point this lap, so it has to run
   all the way around again -- about 32768 cycles, ~1.7 frames, ~34ms
   -- before the envelope even starts moving. This project's original
   envelope (attack 0, decay 6/9, release 0-3) hit exactly that case
   on every single decay-to-release step (a bigger rate dropping to a
   smaller one), so release didn't even begin for ~34ms of our 40ms
   GATE_GAP_FRAMES gap -- the envelope was still sitting wherever decay
   had left it, not anywhere near silent, when the next note's GATE
   went high. That's the click investigated at length in this
   project's commit history (isolating bass alone, or soprano+alto
   together, never reproduced it; only soprano+bass did; slowing the
   whole tempo down hid it without fixing it -- all of that is
   consistent with a fixed ~34ms stall relative to a shrinking row
   time, not with a genuine decay/release timing problem). Keeping
   attack=decay=release=0 for every voice means every A->D->R->A
   transition in the cycle is "equal", never "smaller", so the bug
   never triggers. Sustain level (which is unaffected by this bug) is
   still what shapes each voice's held volume. */
#define VOICE_AD 0x00
#define VOICE_SR 0xC0
#define BASS_AD  0x00
#define BASS_SR  0xA0

static void wait_frame(void)
{
    /* One tick per screen refresh (~50Hz PAL / ~60Hz NTSC): wait for
       the raster to reach line 250, then wait for it to leave that
       line again. Same idiom sid-player-c64/src/main.c and
       asteroids-c64-simple/src/main.c use -- no CIA timer or IRQ
       needed for a single fixed tempo. */
    while (VIC.rasterline != 250) ;
    while (VIC.rasterline == 250) ;
}

static void wait_frames(unsigned char n)
{
    unsigned char i;
    for (i = 0; i < n; i++) {
        wait_frame();
    }
}

/* All 3 voices here only ever need "start a note", "rest", or "leave
   it alone (HOLD)" at a row's start -- unlike sid-player-c64's voice
   3, nothing time-shares with percussion, so there's no per-voice
   special case. */
static void step_voice(struct __sid_voice *voice, unsigned char note, unsigned char waveform)
{
    if (note == HOLD) {
        return;
    }
    if (note == NOTE_REST) {
        sid_note_off(voice);
        return;
    }
    sid_note_on(voice, NOTE_FREQ[note], waveform);
}

static void play_row(const pattern_row_t *row, const pattern_row_t *next)
{
    /* SOLO_VOICE (1/2/3) mutes the other two voices; MUTE_V1/2/3 mutes
       just that one. Diagnostic builds only, for isolating which
       voice(s) a note-to-note click needs to be present -- other
       voices' held notes mask gaps in the full mix. Not a real
       feature. */
#if (!defined(SOLO_VOICE) || SOLO_VOICE == 1) && !defined(MUTE_V1)
    step_voice(&SID.v1, row->soprano, WAVE_TRIANGLE);
#endif
#if (!defined(SOLO_VOICE) || SOLO_VOICE == 2) && !defined(MUTE_V2)
    step_voice(&SID.v2, row->alto, WAVE_TRIANGLE);
#endif
#if (!defined(SOLO_VOICE) || SOLO_VOICE == 3) && !defined(MUTE_V3)
    step_voice(&SID.v3, row->bass, WAVE_SAWTOOTH);
#endif

    wait_frames(ROW_FRAMES - GATE_GAP_FRAMES);

    /* Release any voice whose note ends here -- i.e. the next row
       isn't a HOLD for it -- so the tail GATE_GAP_FRAMES of every row
       are silent for that voice. Checked unconditionally, regardless
       of what this row itself did: this is what turns the *last* row
       of a multi-row HOLD into the note's actual release point. */
    if (next->soprano != HOLD) {
        sid_note_off(&SID.v1);
    }
    if (next->alto != HOLD) {
        sid_note_off(&SID.v2);
    }
    if (next->bass != HOLD) {
        sid_note_off(&SID.v3);
    }

    wait_frames(GATE_GAP_FRAMES);
}

#ifdef STEP_MODE
/* Prints one voice's row on screen at (0, y): a fixed 4-char label
   ("S: "/"A: "/"B: ") then the note name for a real NOTE_*, or a
   marker for HOLD/NOTE_REST. cclearxy first so a shorter name doesn't
   leave stray characters from whatever was there before (e.g. "C4"
   printed over "C#4"). 40 columns is the whole screen width on a
   C64, so this is one line per voice rather than a wide table. */
static void print_cell(const char *label, unsigned char y, unsigned char note)
{
    cclearxy(0, y, 40);
    gotoxy(0, y);
    cputs(label);
    if (note == HOLD) {
        cputs("-hold-");
    } else if (note == NOTE_REST) {
        cputs("-rest-");
    } else {
        cputs(note_name[note]);
    }
}
#endif

int main(void)
{
    unsigned i;
#ifdef STEP_MODE
    const pattern_row_t *row;

    /* Debug build (make run-step): pauses after every row that
       changes at least one voice (i.e. skips rows that are HOLD in
       all 3 -- nothing to hear there) and shows what just started
       playing, so a bad-sounding transition can be pinned down to a
       specific pair of notes by ear instead of guessing from a whole
       bar or the whole ~45-second loop. Not how the song normally
       plays -- see README.md / `make run`. */
    clrscr();
    cputs("step mode: a key advances one note change\r\n\r\n");
#endif

    sid_init();
    sid_set_envelope(&SID.v1, VOICE_AD, VOICE_SR);
    sid_set_envelope(&SID.v2, VOICE_AD, VOICE_SR);
    sid_set_envelope(&SID.v3, BASS_AD, BASS_SR);

    /* Loops forever, same as sid-player-c64 -- this is one song's
       playback engine, not the multi-song Christmas-demo pipeline
       both READMEs describe as future work. */
    while (1) {
        for (i = 0; i < PATTERN_LENGTH; i++) {
            play_row(&PATTERN[i], &PATTERN[(i + 1) % PATTERN_LENGTH]);
#ifdef STEP_MODE
            row = &PATTERN[i];
            if (row->soprano != HOLD || row->alto != HOLD || row->bass != HOLD) {
                cclearxy(0, 3, 40);
                gotoxy(0, 3);
                cprintf("row %u (measure %u)", i, i / 16 + 1);
                print_cell("S: ", 4, row->soprano);
                print_cell("A: ", 5, row->alto);
                print_cell("B: ", 6, row->bass);
                cgetc();
            }
#endif
        }
    }

    return 0;
}
