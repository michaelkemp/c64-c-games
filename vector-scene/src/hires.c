#include <c64.h>
#include <stdlib.h>

#include "hires.h"

unsigned char * const SCREEN_MATRIX = (unsigned char *)0x4000;
unsigned char * const BITMAP        = (unsigned char *)0x6000;

/* row * 320, precomputed so draw_line() never runs a multiply at runtime. */
static const unsigned int ROW_OFFSET[SCREEN_H / 8] = {
    0 * 320,  1 * 320,  2 * 320,  3 * 320,  4 * 320,
    5 * 320,  6 * 320,  7 * 320,  8 * 320,  9 * 320,
    10 * 320, 11 * 320, 12 * 320, 13 * 320, 14 * 320,
    15 * 320, 16 * 320, 17 * 320, 18 * 320, 19 * 320,
    20 * 320, 21 * 320, 22 * 320, 23 * 320, 24 * 320
};

void hires_on(void)
{
    CIA2.pra = (CIA2.pra & 0xFC) | 0x02; /* VIC bank 1: $4000-$7FFF */
    VIC.addr = 0x08;                     /* screen @ bank+0, bitmap @ bank+$2000 */
    VIC.ctrl2 &= ~0x10;                  /* MCM=0: plain (non-multicolor) hi-res */
    VIC.ctrl1 |= 0x20;                   /* BMM=1: bitmap mode on */
}

/* Integer Bresenham, all octants, endpoints inclusive. Identical to
   hires-bounce/src/hires.c -- this is the unmodified baseline every
   trick in this folder gets measured against. */
void draw_line(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    int e2;

    unsigned char *ptr = BITMAP + ROW_OFFSET[y0 >> 3] + (unsigned int)(x0 & ~7) + (unsigned int)(y0 & 7);
    unsigned char mask = 0x80 >> (x0 & 7);

    int y_step  = (sy > 0) ? 1 : -1;
    int y_cross = (sy > 0) ? (320 - 7) : -(320 - 7);
    unsigned char y_wrap_at = (sy > 0) ? 7 : 0;

    for (;;) {
        *ptr ^= mask;

        if (x0 == x1 && y0 == y1) {
            break;
        }

        e2 = 2 * err;

        if (e2 >= dy) {
            err += dy;
            if (sx > 0) {
                mask >>= 1;
                if (mask == 0) {
                    mask = 0x80;
                    ptr += 8;
                }
            } else {
                mask <<= 1;
                if (mask == 0) {
                    mask = 0x01;
                    ptr -= 8;
                }
            }
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            ptr += ((y0 & 7) == y_wrap_at) ? y_cross : y_step;
            y0 += sy;
        }
    }
}
