#ifndef PATTERN_H
#define PATTERN_H

/* One row = one sixteenth note; see main.c's wait_frame(). All 3
   voices advance in lockstep on this grid; a voice still sounding a
   note from an earlier row uses HOLD to say so. 3/4 time (unlike
   o-come-all-ye-faithful-c64's 4/4): 12 rows per measure, not 16. */
#ifndef ROW_FRAMES
#define ROW_FRAMES 6
#endif

#define HOLD      0xFE /* keep playing whatever this voice already had */
#define NOTE_REST 0xFF /* silence */

typedef struct {
    unsigned char soprano; /* NOTE_* or HOLD/NOTE_REST */
    unsigned char alto;    /* NOTE_* or HOLD/NOTE_REST */
    unsigned char bass;    /* NOTE_* or HOLD/NOTE_REST */
} pattern_row_t;

extern const pattern_row_t PATTERN[];
extern const unsigned PATTERN_LENGTH;

#endif
