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
    /* Writing voice->freq as one 16-bit store lets the compiler pick
       the byte order, which for a plain assignment is low byte then
       high byte -- so for one instant between those two writes, the
       oscillator (which free-runs regardless of GATE) sees the *old*
       high byte paired with the *new* low byte. If the note is
       jumping to a very different pitch, that stray value can be a
       long way from either the old or new frequency. Writing the high
       byte first instead means the brief wrong intermediate value is
       old-low-byte-under-the-new-high-byte, which is at most about a
       semitone off the new pitch rather than a potentially wild jump
       -- a much smaller, less audible transient. Explicit byte order
       via a cast, since the struct assignment above doesn't let us
       choose it. */
    unsigned char *freq_bytes = (unsigned char *)&voice->freq;
    freq_bytes[1] = (unsigned char)(freq >> 8);
    freq_bytes[0] = (unsigned char)(freq & 0xFF);

    voice->ctrl = waveform | GATE;
}

void sid_note_off(struct __sid_voice *voice)
{
    voice->ctrl &= ~GATE; /* drop GATE only -- starts the release phase */
}
