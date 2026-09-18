#include <c64.h>

#include "doublebuf.h"
#include "drawline_asm.h"
#include "circle_table.h"
#include "clearbitmap_asm.h"

/* Same rotating-square demo as main.c, but the back buffer is fully
   cleared (trick #4, src/clearbitmap_asm.s) before drawing the new
   square, instead of erasing only the old square's pixels via a
   second XOR redraw (main.c's approach, trick #2). No need to track
   the previous angle at all -- a full clear removes everything
   unconditionally. See README.md's "Trick #4" section for the
   measured comparison. */

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

    doublebuf_init();

    for (;;) {
        if (back == 0) {
            clear_bitmap_buf0();
        } else {
            clear_bitmap_buf1();
        }
        draw_square(angle, BUF_BITMAP[back]);

        while (VIC.rasterline != 250) {
        }
        doublebuf_show(back);

        back = 1 - back;
        angle++;
        if (angle >= 360) angle = 0;
    }

    return 0;
}
