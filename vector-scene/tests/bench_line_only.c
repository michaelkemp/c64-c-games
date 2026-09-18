#include <6502.h>
#include <c64.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>

#include "hires.h"
#include "bench_report.h"

/* Sanity check for tools/bench.sh: single draw_line() per trial (matches
   hires-bounce/tests/bench_line.c exactly), reported through
   report_result() instead of eyeballing cprintf. Used to isolate whether
   an unexpectedly low number from bench_pair_baseline.c is a real
   draw_line() behavior or a harness bug -- see README.md. */

#define TRIALS 100

#define CIA1_TALO (*(volatile unsigned char *)0xDC04)
#define CIA1_TAHI (*(volatile unsigned char *)0xDC05)
#define CIA1_CRA  (*(volatile unsigned char *)0xDC0E)

static unsigned int time_draw(int x0, int y0, int x1, int y1)
{
    unsigned char lo, hi;
    unsigned int remaining;

    SEI();
    CIA1_TALO = 0xFF;
    CIA1_TAHI = 0xFF;
    CIA1_CRA = 0x19;

    draw_line(x0, y0, x1, y1);

    CIA1_CRA = 0x08;
    lo = CIA1_TALO;
    hi = CIA1_TAHI;
    CLI();

    remaining = (unsigned int)lo | ((unsigned int)hi << 8);
    return 0xFFFFu - remaining;
}

int main(void)
{
    unsigned char i;
    long total = 0;
    unsigned int avg_cycles;
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

        total += time_draw(lx, ly, rx, ry);
    }

    VIC.ctrl1 &= ~0x20;
    VIC.addr = 0x14;
    CIA2.pra = (CIA2.pra & 0xFC) | 0x03;

    avg_cycles = (unsigned int)(total / TRIALS);
    report_result(avg_cycles);

    clrscr();
    cprintf("single draw_line() only\r\n");
    cprintf("average: %u cycles\r\n", avg_cycles);

    while (1) {
    }

    return 0;
}
