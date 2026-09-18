#include <c64.h>

#include "doublebuf.h"
#include "drawline_asm.h"
#include "ship_table.h"

/* Phase 1, first milestone: the ship alone, rotating in place at
   screen center -- no thrust/motion/firing yet (see README.md's phase
   list). Same double-buffer + hand-asm-line approach as
   vector-scene/src/main.c's rotating square, just with the ship's
   5-segment outline instead of a 4-point square. Auto-rotates
   continuously for now; turning this into "rotate only while
   left/right is held" is the next step within this same phase. */

#define SHIP_CX 160
#define SHIP_CY 100

static void draw_ship(unsigned int angle, unsigned char *bitmap_base)
{
    unsigned char i;

    dla_bitmap_base = bitmap_base;

    for (i = 0; i < SHIP_POINTS - 1; i++) {
        int xa = SHIP_CX + SHIP_DX[angle][i];
        int ya = SHIP_CY + SHIP_DY[angle][i];
        int xb = SHIP_CX + SHIP_DX[angle][i + 1];
        int yb = SHIP_CY + SHIP_DY[angle][i + 1];

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
            draw_ship((unsigned int)last_angle[back], BUF_BITMAP[back]); /* erase */
        }
        draw_ship(angle, BUF_BITMAP[back]); /* draw */
        last_angle[back] = (int)angle;

        while (VIC.rasterline != 250) {
        }
        doublebuf_show(back);

        back = 1 - back;
        angle += 3; /* matches the prototype's 3-degrees/frame rotation step */
        if (angle >= 360) angle -= 360;
    }

    return 0;
}
