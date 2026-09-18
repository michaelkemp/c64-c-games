#include <c64.h>
#include <conio.h>
#include <string.h>

#include "hires.h"
#include "drawline_asm.h"

/* Correctness regression test for draw_line_asm() (src/drawline_asm.s)
   AND draw_line_smc() (src/drawline_asm_smc.s, the self-modified-code
   variant): 8 structural cases -- horizontal, vertical both
   directions, 45 degrees, shallow, steep -- each drawn with the proven
   C draw_line() and with both asm versions (endpoints swapped to
   satisfy their x1>=x0 requirement, same direction as the C call),
   then compared via a whole-bitmap checksum. All 8 must report OK for
   both.

   Note: an earlier version of this test compared draw_line_asm()
   (always left-to-right) against draw_line() called in its ORIGINAL,
   possibly right-to-left order, and found "mismatches" -- these turned
   out to be a real but benign property of this Bresenham formulation
   (tie-breaking depends on traversal direction; both directions draw a
   valid line, just not always the identical pixel set) rather than an
   asm bug, confirmed by re-comparing with both calls in the same
   direction (see README.md). Every case below deliberately calls
   draw_line() in the same direction draw_line_asm() will use. */

#define NUM_CASES 8

static const int CX0[NUM_CASES] = { 10, 10, 10, 10, 10, 10, 50,  10 };
static const int CY0[NUM_CASES] = { 50, 50, 50, 50, 50, 50, 10,  10 };
static const int CX1[NUM_CASES] = { 60, 10, 60, 60, 20, 60, 60,  60 };
static const int CY1[NUM_CASES] = { 50, 90, 90, 10, 60, 55, 190, 190 };

static unsigned long checksum(void)
{
    unsigned long sum = 0;
    unsigned int i;
    unsigned char *p = BITMAP;
    for (i = 0; i < (SCREEN_W / 8) * SCREEN_H; i++) {
        sum = sum * 31UL + p[i];
    }
    return sum;
}

int main(void)
{
    unsigned char i;
    unsigned char results_asm = 0; /* bit i set = case i matched (draw_line_asm) */
    unsigned char results_smc = 0; /* bit i set = case i matched (draw_line_smc) */

    memset(SCREEN_MATRIX, 0x10, 1000);
    hires_on();
    dla_bitmap_base = BITMAP;

    for (i = 0; i < NUM_CASES; i++) {
        unsigned long sum_c, sum_asm, sum_smc;
        int x0 = CX0[i], y0 = CY0[i], x1 = CX1[i], y1 = CY1[i];

        memset(BITMAP, 0x00, (SCREEN_W / 8) * SCREEN_H);
        draw_line(x0, y0, x1, y1);
        sum_c = checksum();

        memset(BITMAP, 0x00, (SCREEN_W / 8) * SCREEN_H);
        if (x0 <= x1) {
            dla_x0 = x0; dla_y0 = y0; dla_x1 = x1; dla_y1 = y1;
        } else {
            dla_x0 = x1; dla_y0 = y1; dla_x1 = x0; dla_y1 = y0;
        }
        draw_line_asm();
        sum_asm = checksum();

        memset(BITMAP, 0x00, (SCREEN_W / 8) * SCREEN_H);
        if (x0 <= x1) {
            dla_x0 = x0; dla_y0 = y0; dla_x1 = x1; dla_y1 = y1;
        } else {
            dla_x0 = x1; dla_y0 = y1; dla_x1 = x0; dla_y1 = y0;
        }
        draw_line_smc();
        sum_smc = checksum();

        if (sum_c == sum_asm) {
            results_asm |= (1 << i);
        }
        if (sum_c == sum_smc) {
            results_smc |= (1 << i);
        }
    }

    /* $3F00+ is cfg/lowmem.cfg's SCRATCH region -- see bench_report.h. */
    *(volatile unsigned char *)0x3F00 = results_asm;
    *(volatile unsigned char *)0x3F02 = results_smc;
    *(volatile unsigned char *)0x3F01 = 0xA5;

    VIC.ctrl1 &= ~0x20;
    VIC.addr = 0x14;
    CIA2.pra = (CIA2.pra & 0xFC) | 0x03;

    clrscr();
    for (i = 0; i < NUM_CASES; i++) {
        cprintf("case %u (%d,%d)-(%d,%d): asm=%s smc=%s\r\n", (unsigned)i,
                CX0[i], CY0[i], CX1[i], CY1[i],
                (results_asm & (1 << i)) ? "OK" : "MISMATCH",
                (results_smc & (1 << i)) ? "OK" : "MISMATCH");
    }

    while (1) {
    }

    return 0;
}
