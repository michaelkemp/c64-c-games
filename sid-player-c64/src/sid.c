#include "sid.h"

void sid_init(void)
{
    unsigned char i;
    unsigned char *regs = (unsigned char *)0xD400;

    /* SID powers on with whatever garbage was left in it; clear all 25
       registers before touching anything, same reasoning as
       doublebuf_init()'s memset()s in asteroids-c64-simple. Per-voice
       envelopes are the caller's job (main.c), not this function's --
       voice 3 now runs under two different envelopes (a bass one and
       a drum one, swapped in and out per row), so there's no single
       "the" envelope for it to set up here. */
    for (i = 0; i < 25; i++) {
        regs[i] = 0x00;
    }

    SID.amp = 0x0F; /* master volume 15/15, filter off (bits 4-7 = 0) */
}

void sid_set_envelope(struct __sid_voice *voice, unsigned char ad, unsigned char sr)
{
    voice->ad = ad;
    voice->sr = sr;
}

void sid_note_on(struct __sid_voice *voice, unsigned freq, unsigned char waveform)
{
    voice->freq = freq;
    voice->ctrl = waveform | GATE;
}

void sid_note_off(struct __sid_voice *voice)
{
    voice->ctrl &= ~GATE; /* drop GATE only -- starts the release phase */
}
