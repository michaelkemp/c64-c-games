#include <c64.h>

#include "sid.h"
#include "note_table.h"
#include "pattern.h"

/* Frames of silence at the tail of a row whenever the following row
   isn't a HOLD for that voice -- keeps consecutive same-pitch notes
   (e.g. the repeated E's) audible as separate notes instead of one
   continuous tone. */
#define GATE_GAP_FRAMES 3

/* How long each drum hit steals voice 3 from the bass, in frames. */
#define DRUM_FRAMES 6

/* Noise "pitch" -- SID clocks its noise LFSR off the frequency
   register just like a tonal waveform, so this doesn't set a musical
   pitch, it sets the hit's brightness: low reads as a duller, lower
   thump; high reads as a brighter, hissier hit. Both drum types
   otherwise use the same NOISE waveform, just different envelopes
   (below). */
#define KICK_FREQ  0x0300
#define SNARE_FREQ 0x4000

/* Attack/decay/sustain/release pairs for SID.vN.ad/.sr. Melody and
   harmony share a near-instant attack, short decay down to a high
   sustain (a held note stays close to full volume), and a short
   release. Bass gets a touch more decay/release to suit its buzzier
   sawtooth tone. The two drum envelopes are all decay/release and no
   sustain, for a short percussive click rather than a held tone. */
#define MELODY_AD 0x06
#define MELODY_SR 0xC3
#define BASS_AD   0x09
#define BASS_SR   0xA2
#define KICK_AD   0x00
#define KICK_SR   0x02
#define SNARE_AD  0x01
#define SNARE_SR  0x03

/* Voice 3's bass note, remembered across HOLD rows and across drum
   hits (which silence voice 3 for DRUM_FRAMES) so it can be resumed
   at the right pitch, or left silent if it was resting. */
static unsigned v3_freq;
static unsigned char v3_active;

static void wait_frame(void)
{
    /* One tick per screen refresh (~50Hz PAL / ~60Hz NTSC): wait for
       the raster to reach line 250, then wait for it to leave that
       line again. Same "wait for a specific line to come back around"
       idiom asteroids-c64-simple/src/main.c's swap-timing comment
       describes, just driving the tempo here instead of frame
       pacing -- no CIA timer or IRQ needed for a single fixed tempo. */
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

/* Voices 1 and 2 (melody/harmony) only ever need "start a note",
   "rest", or "leave it alone (HOLD)" at a row's start -- no time-
   sharing to juggle, unlike voice 3. */
static void step_voice(struct __sid_voice *voice, unsigned char note)
{
    if (note == HOLD) {
        return;
    }
    if (note == NOTE_REST) {
        sid_note_off(voice);
        return;
    }
    sid_note_on(voice, NOTE_FREQ[note], WAVE_TRIANGLE);
}

static void play_drum(unsigned char drum)
{
    if (drum == DRUM_KICK) {
        sid_set_envelope(&SID.v3, KICK_AD, KICK_SR);
        sid_note_on(&SID.v3, KICK_FREQ, WAVE_NOISE);
    } else {
        sid_set_envelope(&SID.v3, SNARE_AD, SNARE_SR);
        sid_note_on(&SID.v3, SNARE_FREQ, WAVE_NOISE);
    }
    wait_frames(DRUM_FRAMES);
    sid_note_off(&SID.v3);

    sid_set_envelope(&SID.v3, BASS_AD, BASS_SR);
    if (v3_active) {
        sid_note_on(&SID.v3, v3_freq, WAVE_SAWTOOTH);
    }
}

static void play_row(const pattern_row_t *row, const pattern_row_t *next)
{
    unsigned char sound_frames;

    step_voice(&SID.v1, row->v1);
    step_voice(&SID.v2, row->v2);

    /* Voice 3's own note (if any) is resolved before the drum hit,
       not after -- the drum always plays first within the row, then
       the (possibly just-updated) bass note resumes for whatever's
       left of it. A HOLD here means "no change", so a drum hit in the
       middle of a held bass note correctly resumes the same pitch. */
    if (row->v3 == NOTE_REST) {
        v3_active = 0;
    } else if (row->v3 != HOLD) {
        v3_active = 1;
        v3_freq = NOTE_FREQ[row->v3];
    }

    if (row->drum != DRUM_NONE) {
        play_drum(row->drum);
        sound_frames = ROW_FRAMES - DRUM_FRAMES - GATE_GAP_FRAMES;
    } else {
        if (!v3_active) {
            sid_note_off(&SID.v3);
        } else if (row->v3 != HOLD) {
            sid_note_on(&SID.v3, v3_freq, WAVE_SAWTOOTH);
        }
        sound_frames = ROW_FRAMES - GATE_GAP_FRAMES;
    }

    wait_frames(sound_frames);

    /* Release any voice whose note ends here -- i.e. the next row
       isn't a HOLD for it -- so the tail GATE_GAP_FRAMES of every row
       are silent for that voice. Checked unconditionally, regardless
       of what this row itself did: this is what turns the *last* row
       of a multi-row HOLD into the note's actual release point. */
    if (next->v1 != HOLD) {
        sid_note_off(&SID.v1);
    }
    if (next->v2 != HOLD) {
        sid_note_off(&SID.v2);
    }
    if (next->v3 != HOLD) {
        sid_note_off(&SID.v3);
    }

    wait_frames(GATE_GAP_FRAMES);
}

int main(void)
{
    unsigned char i;

    sid_init();
    sid_set_envelope(&SID.v1, MELODY_AD, MELODY_SR);
    sid_set_envelope(&SID.v2, MELODY_AD, MELODY_SR);
    sid_set_envelope(&SID.v3, BASS_AD, BASS_SR);

    /* Loops forever -- this is the "prove SID can play a real,
       correctly-timed 3-part arrangement with percussion" foundation
       piece, not the full multi-song Christmas-demo pipeline yet.
       See README.md. */
    while (1) {
        for (i = 0; i < PATTERN_LENGTH; i++) {
            play_row(&PATTERN[i], &PATTERN[(i + 1) % PATTERN_LENGTH]);
        }
    }

    return 0;
}
