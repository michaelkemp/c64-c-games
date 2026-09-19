#ifndef SID_H
#define SID_H

#include <c64.h>

/* Waveform select bits for a SID voice's control register
   (struct __sid_voice's ctrl field, see <c64.h>); OR one of these
   with GATE to start a note. */
#define WAVE_TRIANGLE 0x10
#define WAVE_SAWTOOTH 0x20
#define WAVE_PULSE    0x40
#define WAVE_NOISE    0x80
#define GATE          0x01

void sid_init(void);
void sid_set_envelope(struct __sid_voice *voice, unsigned char ad, unsigned char sr);
void sid_note_on(struct __sid_voice *voice, unsigned freq, unsigned char waveform);
void sid_note_off(struct __sid_voice *voice);

#endif
