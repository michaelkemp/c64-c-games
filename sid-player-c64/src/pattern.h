#ifndef PATTERN_H
#define PATTERN_H

/* One row = one quarter note at ~125bpm, 50Hz PAL (~480ms) -- see
   main.c's wait_frame(). The whole arrangement (melody, harmony,
   bass, drums) advances in lockstep on this grid; a voice that's
   still sounding a note from an earlier row uses HOLD to say so. */
#define ROW_FRAMES 24

/* Per-voice cell values, in addition to a real NOTE_* from
   note_table.h: */
#define HOLD      0xFE /* keep playing whatever this voice already had */
#define NOTE_REST 0xFF /* silence */

/* Voice 3 (bass) is time-shared with percussion: on a row with a drum
   hit, voice 3 plays a short noise burst first, then resumes (or
   starts) its bass note for the rest of the row -- see main.c's
   play_row()/play_drum(). */
#define DRUM_NONE  0
#define DRUM_KICK  1
#define DRUM_SNARE 2

typedef struct {
    unsigned char v1;   /* melody:  NOTE_* or HOLD/NOTE_REST */
    unsigned char v2;   /* harmony: NOTE_* or HOLD/NOTE_REST */
    unsigned char v3;   /* bass:    NOTE_* or HOLD/NOTE_REST */
    unsigned char drum; /* DRUM_NONE / DRUM_KICK / DRUM_SNARE */
} pattern_row_t;

extern const pattern_row_t PATTERN[];
extern const unsigned char PATTERN_LENGTH;

#endif
