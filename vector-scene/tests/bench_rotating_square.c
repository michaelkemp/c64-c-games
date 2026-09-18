#include <6502.h>
#include <c64.h>
#include <conio.h>

#include "doublebuf.h"
#include "drawline_asm.h"
#include "circle_table.h"
#include "cia_timer.h"
#include "bench_report.h"

/* Measured variant of src/main.c's rotating-square demo: same double
   buffering, same hand-written 6502 draw, same one-degree-per-frame
   rotation, but for a fixed number of frames, with the erase+draw work
   timed by the chained wraparound-safe CIA timer (not the crude
   vsync-frame-count the visual demo uses) and reported both on-screen
   and via tools/bench.sh. See README.md's "The rotating square demo"
   section. */

#define TOTAL_FRAMES (360 * 3) /* three full rotations */

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
    unsigned char back = 1;
    int last_angle[2] = { -1, -1 };
    unsigned int frame;
    unsigned long total_cycles = 0;
    unsigned long avg_cycles, avg_us;
    unsigned long frames_pal, frames_ntsc;

    doublebuf_init();

    for (frame = 0; frame < TOTAL_FRAMES; frame++) {
        cia_timer_start();

        if (last_angle[back] >= 0) {
            draw_square((unsigned int)last_angle[back], BUF_BITMAP[back]);
        }
        draw_square(angle, BUF_BITMAP[back]);

        total_cycles += cia_timer_stop();
        last_angle[back] = (int)angle;

        while (VIC.rasterline != 250) {
        }
        doublebuf_show(back);

        back = 1 - back;
        angle++;
        if (angle >= 360) angle = 0;
    }

    /* back to standard text mode/bank to report */
    VIC.ctrl1 &= ~0x20;
    VIC.addr = 0x14;
    CIA2.pra = (CIA2.pra & 0xFC) | 0x03;

    avg_cycles = total_cycles / TOTAL_FRAMES;
    avg_us = (avg_cycles * 1015UL) / 1000UL;
    report_result(avg_cycles);

    /* NTSC ~17095 cycles/frame @ 59.83Hz, PAL ~19656 @ 50.12Hz -- same
       reference bench_scene.c uses. */
    frames_pal = avg_cycles / 19656UL;
    frames_ntsc = avg_cycles / 17095UL;

    clrscr();
    cprintf("rotating square: %u frames\r\n\r\n", (unsigned)TOTAL_FRAMES);
    cprintf("erase+draw (8 lines) average:\r\n");
    cprintf("  %lu cycles (%lu us)\r\n\r\n", avg_cycles, avg_us);
    cprintf("pal  50hz frame budgets used: %lu\r\n", frames_pal);
    cprintf("ntsc 60hz frame budgets used: %lu\r\n", frames_ntsc);

    while (1) {
    }

    return 0;
}
