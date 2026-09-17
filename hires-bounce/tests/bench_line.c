#include <6502.h>
#include <c64.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>

#include "hires.h"

/* Benchmark for draw_line() (hires.c): 100 lines, each from a random
   point on the left half of the screen to a random point on the right
   half, timed individually with CIA1 Timer A (counts real 6502 cycles),
   then reports the average. See README.md's "Making it fast" section --
   this is what verifies that section's claims stay true rather than
   rotting the next time draw_line() changes. */

#define TRIALS 100

/* Accessed directly by address, not via c64.h's CIA1 struct macro,
   because that macro isn't declared volatile -- these reads/writes are
   hardware register accesses with side effects the optimizer must not
   reorder or elide. */
#define CIA1_TALO (*(volatile unsigned char *)0xDC04)
#define CIA1_TAHI (*(volatile unsigned char *)0xDC05)
#define CIA1_CRA  (*(volatile unsigned char *)0xDC0E)

/* Times one draw_line() call in raw 6502 cycles. CIA1 Timer A is loaded
   with $FFFF and counts down once (one-shot) at the system clock rate;
   elapsed cycles = $FFFF minus whatever's left when it's stopped again.
   IRQs are disabled for the measured window -- this timer is the same
   one the KERNAL's default jiffy IRQ normally uses, so leaving
   interrupts on here would both misfire the KERNAL's handler (since
   we've just repointed its clock source) and add unrelated cycles to
   the count. */
static unsigned int time_draw(int x0, int y0, int x1, int y1)
{
    unsigned char lo, hi;
    unsigned int remaining;

    SEI();
    CIA1_TALO = 0xFF;
    CIA1_TAHI = 0xFF;
    CIA1_CRA = 0x19; /* bit4 force-load, bit3 one-shot, bit0 start */

    draw_line(x0, y0, x1, y1);

    CIA1_CRA = 0x08; /* stop (clear start) */
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
    unsigned long avg_us;
    int lx, ly, rx, ry;

    srand(VIC.rasterline);

    memset(BITMAP, 0x00, (SCREEN_W / 8) * SCREEN_H);
    memset(SCREEN_MATRIX, 0x10, 1000);
    hires_on();

    for (i = 0; i < TRIALS; i++) {
        lx = rand() % (SCREEN_W / 2);         /* left half:  x in 0..159 */
        ly = rand() % SCREEN_H;
        rx = (SCREEN_W / 2) + rand() % (SCREEN_W / 2); /* right half: x in 160..319 */
        ry = rand() % SCREEN_H;

        total += time_draw(lx, ly, rx, ry);
    }

    /* Back to normal text mode to report the result. */
    VIC.ctrl1 &= ~0x20;
    VIC.addr = 0x14;
    CIA2.pra = (CIA2.pra & 0xFC) | 0x03;

    avg_cycles = (unsigned int)(total / TRIALS);

    /* us = cycles * (1000000 / 985248), i.e. cycles / PAL cycles-per-us.
       cc65 has no 64-bit type to do that with the exact 1000000
       multiplier without overflowing a 32-bit unsigned long once
       avg_cycles exceeds ~4295 (1000000 * 4295 already brushes the
       ~4.29 billion ceiling) -- an earlier version did exactly that and
       silently wrapped around, printing a nonsense value. 1015/1000 is
       PAL's ratio (1/0.985248) rounded to 3 places -- within ~0.003% of
       exact, and 1015x leaves headroom up to avg_cycles ~4.2 million,
       far past anything a single line draw can reach. */
    avg_us = ((unsigned long)avg_cycles * 1015UL) / 1000UL;

    clrscr();
    cprintf("draw_line() benchmark\r\n");
    cprintf("%u random left-to-right lines\r\n\r\n", (unsigned)TRIALS);
    cprintf("average: %u cycles (%lu us)\r\n", avg_cycles, avg_us);

    while (1) {
    }

    return 0;
}
