#include "pattern.h"
#include "note_table.h"

/* 3-part harmony + noise-channel percussion arrangement of "Jingle
   Bells" (public domain), hand-transcribed -- authored content, not
   generated, same reasoning song.c (this file's single-voice
   predecessor) had.

   Voice 1 (melody) is the same tune this project started with. Voice
   2 (harmony) shadows it a diatonic third below, within the C major
   scale (E->C, G->E, C->A, D->B, F->D) -- the cheap, reliably-
   consonant way to add a second part under a melody without any real
   chord logic. Voice 3 is a simple root-note bass line (I/IV/V:
   C3/F3/G3) that changes on phrase boundaries rather than every beat,
   ending on the standard V-I (G3->C3) cadence.

   SID only has 3 voices, so percussion time-shares voice 3 with the
   bass rather than getting a dedicated channel: on a DRUM_KICK/SNARE
   row, main.c's play_row() plays a short noise burst on voice 3
   first, then resumes (or starts) that row's bass note for whatever's
   left of the row. "Kick" vs "snare" is pure noise-channel trickery --
   both use the same NOISE waveform, but SID clocks its noise LFSR off
   the frequency register same as any tonal waveform, so a low value
   there reads as a duller, lower-pitched hit and a high one reads as
   a brighter, hissier hit (main.c's KICK_FREQ/SNARE_FREQ). The drum
   pattern itself is a plain steady kick-on-1/snare-on-3 grid, laid
   down independently of where the bass line happens to change. */

const pattern_row_t PATTERN[] = {
    /* Jingle bells, jingle bells (bass: tonic pedal, C3) */
    { NOTE_E4, NOTE_C4, NOTE_C3, DRUM_KICK  },
    { NOTE_E4, NOTE_C4, HOLD,    DRUM_NONE  },
    { NOTE_E4, NOTE_C4, HOLD,    DRUM_SNARE },
    { HOLD,    HOLD,    HOLD,    DRUM_NONE  },
    { NOTE_E4, NOTE_C4, HOLD,    DRUM_KICK  },
    { NOTE_E4, NOTE_C4, HOLD,    DRUM_NONE  },
    { NOTE_E4, NOTE_C4, HOLD,    DRUM_SNARE },
    { HOLD,    HOLD,    HOLD,    DRUM_NONE  },

    /* jingle all the way */
    { NOTE_E4, NOTE_C4, HOLD,    DRUM_KICK  },
    { NOTE_G4, NOTE_E4, HOLD,    DRUM_NONE  },
    { NOTE_C4, NOTE_A3, HOLD,    DRUM_SNARE },
    { NOTE_D4, NOTE_B3, HOLD,    DRUM_NONE  },
    { NOTE_E4, NOTE_C4, HOLD,    DRUM_KICK  },
    { HOLD,    HOLD,    HOLD,    DRUM_NONE  },
    { HOLD,    HOLD,    HOLD,    DRUM_SNARE },
    { HOLD,    HOLD,    HOLD,    DRUM_NONE  },

    /* Oh what fun it is to ride (bass: IV, F3) */
    { NOTE_F4, NOTE_D4, NOTE_F3, DRUM_KICK  },
    { NOTE_F4, NOTE_D4, HOLD,    DRUM_NONE  },
    { NOTE_F4, NOTE_D4, HOLD,    DRUM_SNARE },
    { NOTE_F4, NOTE_D4, HOLD,    DRUM_NONE  },
    { NOTE_F4, NOTE_D4, HOLD,    DRUM_KICK  },

    /* in a one-horse open sleigh (bass: I, then a V passing chord) */
    { NOTE_E4, NOTE_C4, NOTE_C3, DRUM_NONE  },
    { NOTE_E4, NOTE_C4, HOLD,    DRUM_SNARE },
    { NOTE_E4, NOTE_C4, HOLD,    DRUM_NONE  },
    { NOTE_E4, NOTE_C4, HOLD,    DRUM_KICK  },
    { NOTE_D4, NOTE_B3, NOTE_G3, DRUM_NONE  },
    { NOTE_D4, NOTE_B3, HOLD,    DRUM_SNARE },
    { NOTE_E4, NOTE_C4, NOTE_C3, DRUM_NONE  },

    /* final cadence: V (G3) -> I (C3) */
    { NOTE_D4, NOTE_B3, NOTE_G3, DRUM_KICK  },
    { HOLD,    HOLD,    HOLD,    DRUM_NONE  },
    { NOTE_G4, NOTE_E4, NOTE_C3, DRUM_SNARE },
    { HOLD,    HOLD,    HOLD,    DRUM_NONE  },

    { NOTE_REST, NOTE_REST, NOTE_REST, DRUM_NONE },
    { NOTE_REST, NOTE_REST, NOTE_REST, DRUM_NONE },
};

const unsigned char PATTERN_LENGTH = sizeof(PATTERN) / sizeof(PATTERN[0]);
