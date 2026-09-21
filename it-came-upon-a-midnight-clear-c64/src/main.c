#include <c64.h>

#include "sid.h"
#include "note_table.h"
#include "pattern.h"

/* Frames of silence at the tail of a row whenever the following row
   isn't a HOLD for that voice -- keeps consecutive same-pitch notes
   audible as separate notes instead of one continuous tone. */
#define GATE_GAP_FRAMES 2

/* Attack/decay/sustain/release: all rate 0 (fastest) on both voices.
   See o-come-all-ye-faithful-c64/src/main.c's long comment for why --
   short version: the SID envelope generator has a documented "ADSR
   delay bug" (codebase64: base:classic_hard-restart_and_about_adsr_in_generally)
   where dropping to a *smaller* rate than the one just active can
   stall the envelope for ~34ms before it even starts moving. Keeping
   attack=decay=release=0 for every voice means no transition in the
   cycle is ever a decrease, so the bug can't trigger. Found the hard
   way on that sibling project; applied here from the start. */
#define VOICE_AD 0x00
#define VOICE_SR 0xC0
#define BASS_AD  0x00
#define BASS_SR  0xA0

static void wait_frame(void)
{
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
    step_voice(&SID.v1, row->soprano, WAVE_TRIANGLE);
    step_voice(&SID.v2, row->alto, WAVE_TRIANGLE);
    step_voice(&SID.v3, row->bass, WAVE_SAWTOOTH);

    wait_frames(ROW_FRAMES - GATE_GAP_FRAMES);

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

int main(void)
{
    unsigned i;

    sid_init();
    sid_set_envelope(&SID.v1, VOICE_AD, VOICE_SR);
    sid_set_envelope(&SID.v2, VOICE_AD, VOICE_SR);
    sid_set_envelope(&SID.v3, BASS_AD, BASS_SR);

    while (1) {
        for (i = 0; i < PATTERN_LENGTH; i++) {
            play_row(&PATTERN[i], &PATTERN[(i + 1) % PATTERN_LENGTH]);
        }
    }

    return 0;
}
