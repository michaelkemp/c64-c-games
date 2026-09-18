#include <c64.h>

#include "doublebuf.h"
#include "drawline_asm.h"
#include "circle_table.h"

/* The demo this whole folder was built to answer: a rotating square,
   corners taken from CIRCLE_X/Y (a precomputed circle, radius 95,
   centered on screen) at 0/90/180/270 degrees apart, advancing one
   degree per frame -- drawn with the hand-written 6502 line routine
   (trick #2) into whichever of the two VIC-bank buffers (trick #3)
   isn't currently on screen. See README.md's "The rotating square
   demo" section for the measured per-frame cost.

   Each buffer holds exactly one square at a time. Every frame: erase
   the square THIS buffer was showing two frames ago (same trick
   hires-bounce/src/main.c uses for a single line -- draw the same
   coordinates again, XOR cancels), draw the new one, wait for vertical
   blank, then flip. Never touches the buffer currently on screen. */

static void draw_square(unsigned int base_angle, unsigned char *bitmap_base)
{
    unsigned int idx[4];
    unsigned char i;

    idx[0] = base_angle;
    idx[1] = base_angle + 90;
    if (idx[1] >= 360) idx[1] -= 360;
    idx[2] = base_angle + 180;
    if (idx[2] >= 360) idx[2] -= 360;
    idx[3] = base_angle + 270;
    if (idx[3] >= 360) idx[3] -= 360;

    dla_bitmap_base = bitmap_base;

    for (i = 0; i < 4; i++) {
        unsigned int a = idx[i];
        unsigned int b = idx[(i + 1) & 3];
        int xa = CIRCLE_X[a], ya = CIRCLE_Y[a];
        int xb = CIRCLE_X[b], yb = CIRCLE_Y[b];

        if (xa <= xb) {
            dla_x0 = xa; dla_y0 = ya; dla_x1 = xb; dla_y1 = yb;
        } else {
            dla_x0 = xb; dla_y0 = yb; dla_x1 = xa; dla_y1 = ya;
        }
        draw_line_asm();
    }
}

int main(void)
{
    unsigned int angle = 0;
    unsigned char back = 1; /* buffer 0 is shown first by doublebuf_init() */
    int last_angle[2] = { -1, -1 };

    doublebuf_init();

    for (;;) {
        if (last_angle[back] >= 0) {
            draw_square((unsigned int)last_angle[back], BUF_BITMAP[back]); /* erase */
        }
        draw_square(angle, BUF_BITMAP[back]); /* draw */
        last_angle[back] = (int)angle;

        while (VIC.rasterline != 250) {
        }
        doublebuf_show(back);

        back = 1 - back;
        angle++;
        if (angle >= 360) angle = 0;
    }

    return 0;
}
