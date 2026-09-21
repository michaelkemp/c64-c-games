#ifndef PATTERN_H
#define PATTERN_H

/* One row = one sixteenth note at ~105bpm, 50Hz PAL (~86ms) -- see
   main.c's wait_frame(). All 3 voices advance in lockstep on this
   grid; a voice still sounding a note from an earlier row uses HOLD
   to say so. Sixteenth-note resolution (rather than sid-player-c64's
   quarter-note grid) because this arrangement, pulled from a real
   MIDI performance (see tools/gen_pattern.py), actually has eighth-
   and sixteenth-note motion in it, not just quarters. */
#ifndef ROW_FRAMES
#define ROW_FRAMES 7
#endif

/* Per-voice cell values, in addition to a real NOTE_* from
   note_table.h: */
#define HOLD      0xFE /* keep playing whatever this voice already had */
#define NOTE_REST 0xFF /* silence */

/* No percussion here -- this arrangement is 3 melodic voices only
   (soprano, alto, bass), no noise-channel drums stealing voice 3 the
   way sid-player-c64's does. See tools/gen_pattern.py for how "bass"
   ended up meaning "the lowest of the 4 real SATB voices sounding at
   this instant", which is what lets it pick up the tenor whenever the
   notated bass rests. */
typedef struct {
    unsigned char soprano; /* NOTE_* or HOLD/NOTE_REST */
    unsigned char alto;    /* NOTE_* or HOLD/NOTE_REST */
    unsigned char bass;    /* NOTE_* or HOLD/NOTE_REST */
} pattern_row_t;

extern const pattern_row_t PATTERN[];

/* unsigned, not unsigned char like sid-player-c64's: one verse of this
   arrangement at sixteenth-note resolution is 320 rows, past what a
   char can hold. */
extern const unsigned PATTERN_LENGTH;

#endif
