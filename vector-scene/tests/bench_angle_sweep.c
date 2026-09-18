#include <6502.h>
#include <c64.h>
#include <conio.h>
#include <string.h>

#include "hires.h"
#include "drawline_asm.h"
#include "circle_table.h"
#include "cia_timer.h"
#include "bench_report.h"

/* Answers a question raised while comparing bench_pair_asm/smc.c
   (random lines, biased shallow) against the rotating-square demo
   (uniform angle sweep, half the time in the 45-90 degree "y-branch
   fires every iteration" zone): is that really the mechanism? Draws a
   FIXED-radius line from screen center to each of 18 angles (0..170
   degrees in 10-degree steps -- covers the full 0-180 range once,
   which is enough by symmetry), timing trick #2 (draw_line_asm) and
   trick #2b (draw_line_smc) on the exact same line, and reports both
   costs per angle. See README.md's "Angle sweep" section. */

#define TRIALS_PER_ANGLE 10
#define NUM_ANGLES 18

/* static, not local: an earlier version accumulated per-angle totals
   in main()'s own locals and POKEd results to $2000+ once per angle,
   INSIDE the trials loop -- while cc65's software stack (locals,
   parameters) was at its deepest for this program (linking two full
   asm modules plus the circle table). That stack grows downward from
   __HIMEM__ ($4000) and reached down far enough to collide with those
   writes, corrupting them mid-loop -- silently produced two wrong
   readings (0 and ~9x too high) that looked like real draw_line_smc()
   anomalies until an isolated single-angle re-test at the exact same
   coordinates came back completely normal. Fixed by keeping totals in
   static storage (BSS, never overlapping the stack) throughout the
   loop, and only POKEing to $2000+ once, after the loop, when the
   stack is back to its shallow idle depth. See README.md. */
static unsigned long results_asm[NUM_ANGLES];
static unsigned long results_smc[NUM_ANGLES];

int main(void)
{
    unsigned char a;
    unsigned char t;

    memset(BITMAP, 0x00, (SCREEN_W / 8) * SCREEN_H);
    memset(SCREEN_MATRIX, 0x10, 1000);
    hires_on();
    dla_bitmap_base = BITMAP;

    for (a = 0; a < NUM_ANGLES; a++) {
        unsigned int angle = (unsigned int)a * 10;
        int cx = 160, cy = 100;
        int px = CIRCLE_X[angle], py = CIRCLE_Y[angle];
        int x0, y0, x1, y1;
        unsigned long total_asm = 0, total_smc = 0;

        if (cx <= px) {
            x0 = cx; y0 = cy; x1 = px; y1 = py;
        } else {
            x0 = px; y0 = py; x1 = cx; y1 = cy;
        }

        for (t = 0; t < TRIALS_PER_ANGLE; t++) {
            dla_x0 = x0; dla_y0 = y0; dla_x1 = x1; dla_y1 = y1;
            cia_timer_start();
            draw_line_asm();
            total_asm += cia_timer_stop();
            dla_x0 = x0; dla_y0 = y0; dla_x1 = x1; dla_y1 = y1;
            draw_line_asm(); /* erase */

            dla_x0 = x0; dla_y0 = y0; dla_x1 = x1; dla_y1 = y1;
            cia_timer_start();
            draw_line_smc();
            total_smc += cia_timer_stop();
            dla_x0 = x0; dla_y0 = y0; dla_x1 = x1; dla_y1 = y1;
            draw_line_smc(); /* erase */
        }

        results_asm[a] = total_asm / TRIALS_PER_ANGLE;
        results_smc[a] = total_smc / TRIALS_PER_ANGLE;
    }

    /* $3F00+ is cfg/lowmem.cfg's SCRATCH region -- see bench_report.h
       for why this needs to be linker-reserved, not a guessed address
       (this exact file is what exposed the bug in the guess). 18*8=144
       bytes fits comfortably in SCRATCH's 256. */
    for (a = 0; a < NUM_ANGLES; a++) {
        *(volatile unsigned long *)(0x3F00 + (unsigned int)a * 8)     = results_asm[a];
        *(volatile unsigned long *)(0x3F00 + (unsigned int)a * 8 + 4) = results_smc[a];
    }
    *(volatile unsigned char *)0x3F90 = 0xA5;

    VIC.ctrl1 &= ~0x20;
    VIC.addr = 0x14;
    CIA2.pra = (CIA2.pra & 0xFC) | 0x03;

    clrscr();
    cprintf("angle  asm(#2)  smc(#2b)\r\n");
    for (a = 0; a < NUM_ANGLES; a++) {
        cprintf("%3u   %6lu   %6lu\r\n", (unsigned)a * 10, results_asm[a], results_smc[a]);
    }

    while (1) {
    }

    return 0;
}
