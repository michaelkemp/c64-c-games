#include <6502.h>
#include <c64.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>

#include "hires.h"
#include "dirtylist.h"
#include "cia_timer.h"
#include "bench_report.h"

/* Trick #1 benchmark: same draw+erase PAIR as bench_pair_baseline.c,
   same 100 random left-to-right lines, same srand() seed source, same
   chained wraparound-safe timer -- the only thing that changes is how
   the erase half is done (dirty-list replay instead of a second
   Bresenham walk). See README.md for the comparison. */

#define TRIALS 100

int main(void)
{
    unsigned char i;
    unsigned long total = 0;
    unsigned long avg_cycles;
    unsigned long avg_us;
    int lx, ly, rx, ry;
    static DirtyList dirty;

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
        draw_line_dirty(lx, ly, rx, ry, &dirty); /* draw + record */
        erase_dirty(&dirty);                     /* erase: replay, no Bresenham */
        total += cia_timer_stop();
    }

    VIC.ctrl1 &= ~0x20;
    VIC.addr = 0x14;
    CIA2.pra = (CIA2.pra & 0xFC) | 0x03;

    avg_cycles = total / TRIALS;
    avg_us = (avg_cycles * 1015UL) / 1000UL;
    report_result(avg_cycles);

    clrscr();
    cprintf("dirty-list draw+erase pair\r\n");
    cprintf("%u random left-to-right lines\r\n\r\n", (unsigned)TRIALS);
    cprintf("average: %lu cycles (%lu us)\r\n", avg_cycles, avg_us);

    while (1) {
    }

    return 0;
}
