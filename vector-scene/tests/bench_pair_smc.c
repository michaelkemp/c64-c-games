#include <6502.h>
#include <c64.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>

#include "hires.h"
#include "drawline_asm.h"
#include "cia_timer.h"
#include "bench_report.h"

/* Trick #2b benchmark: identical to bench_pair_asm.c (same draw+erase
   PAIR, same 100 random left-to-right lines, same srand() seed source,
   same chained wraparound-safe timer) -- the only change is drawing
   with src/drawline_asm_smc.s (self-modified absolute addressing for
   the pixel toggle) instead of src/drawline_asm.s (zero-page indirect).
   See README.md for the comparison. */

#define TRIALS 100

int main(void)
{
    unsigned char i;
    unsigned long total = 0;
    unsigned long avg_cycles;
    unsigned long avg_us;
    int lx, ly, rx, ry;

    srand(VIC.rasterline);

    memset(BITMAP, 0x00, (SCREEN_W / 8) * SCREEN_H);
    memset(SCREEN_MATRIX, 0x10, 1000);
    hires_on();
    dla_bitmap_base = BITMAP;

    for (i = 0; i < TRIALS; i++) {
        lx = rand() % (SCREEN_W / 2);
        ly = rand() % SCREEN_H;
        rx = (SCREEN_W / 2) + rand() % (SCREEN_W / 2);
        ry = rand() % SCREEN_H;

        dla_x0 = lx; dla_y0 = ly; dla_x1 = rx; dla_y1 = ry;

        cia_timer_start();
        draw_line_smc(); /* draw */
        dla_x0 = lx; dla_y0 = ly; dla_x1 = rx; dla_y1 = ry; /* mutates x0/y0 in place */
        draw_line_smc(); /* erase */
        total += cia_timer_stop();
    }

    VIC.ctrl1 &= ~0x20;
    VIC.addr = 0x14;
    CIA2.pra = (CIA2.pra & 0xFC) | 0x03;

    avg_cycles = total / TRIALS;
    avg_us = (avg_cycles * 1015UL) / 1000UL;
    report_result(avg_cycles);

    clrscr();
    cprintf("self-modified-code draw+erase pair\r\n");
    cprintf("%u random left-to-right lines\r\n\r\n", (unsigned)TRIALS);
    cprintf("average: %lu cycles (%lu us)\r\n", avg_cycles, avg_us);

    while (1) {
    }

    return 0;
}
