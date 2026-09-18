#include <6502.h>
#include <c64.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>

#include "hires.h"
#include "cia_timer.h"
#include "bench_report.h"

/* Baseline: times a draw+erase PAIR, the actual per-object cost every
   frame pays today (hires-bounce/src/main.c's "redraw at old position to
   erase, then redraw at new position"). Trick benchmarks in this folder
   (bench_pair_dirtylist.c etc.) measure the exact same pair so the
   comparison is apples-to-apples. 100 random left-to-right lines,
   individually timed with the wraparound-safe chained CIA timer (see
   cia_timer.h -- a plain 16-bit Timer A silently under-reports any line
   longer than ~90 pixels in this build), averaged. */

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

    for (i = 0; i < TRIALS; i++) {
        lx = rand() % (SCREEN_W / 2);
        ly = rand() % SCREEN_H;
        rx = (SCREEN_W / 2) + rand() % (SCREEN_W / 2);
        ry = rand() % SCREEN_H;

        cia_timer_start();
        draw_line(lx, ly, rx, ry); /* draw */
        draw_line(lx, ly, rx, ry); /* erase: 2nd XOR cancels the 1st */
        total += cia_timer_stop();
    }

    VIC.ctrl1 &= ~0x20;
    VIC.addr = 0x14;
    CIA2.pra = (CIA2.pra & 0xFC) | 0x03;

    avg_cycles = total / TRIALS;
    avg_us = (avg_cycles * 1015UL) / 1000UL;
    report_result(avg_cycles);

    clrscr();
    cprintf("baseline draw+erase pair\r\n");
    cprintf("%u random left-to-right lines\r\n\r\n", (unsigned)TRIALS);
    cprintf("average: %lu cycles (%lu us)\r\n", avg_cycles, avg_us);

    while (1) {
    }

    return 0;
}
