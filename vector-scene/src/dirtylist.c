#include <stdlib.h>

#include "hires.h"
#include "dirtylist.h"

/* Same address/mask table this project's hires.c uses -- duplicated
   here rather than shared because draw_line_dirty() isn't a thin
   wrapper around draw_line(): it needs its own copy of the loop body to
   push into the dirty list at the one point per pixel where hires.c's
   draw_line() just does *ptr ^= mask. */
static const unsigned int ROW_OFFSET[SCREEN_H / 8] = {
    0 * 320,  1 * 320,  2 * 320,  3 * 320,  4 * 320,
    5 * 320,  6 * 320,  7 * 320,  8 * 320,  9 * 320,
    10 * 320, 11 * 320, 12 * 320, 13 * 320, 14 * 320,
    15 * 320, 16 * 320, 17 * 320, 18 * 320, 19 * 320,
    20 * 320, 21 * 320, 22 * 320, 23 * 320, 24 * 320
};

void draw_line_dirty(int x0, int y0, int x1, int y1, DirtyList *dirty)
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

    unsigned int n = 0;

    for (;;) {
        *ptr ^= mask;
        dirty->addr[n] = ptr;
        dirty->mask[n] = mask;
        n++;

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

    dirty->count = n;
}

/* The whole point: no error term, no mask/address-stepping decisions --
   just replay what draw_line_dirty() already worked out. */
void erase_dirty(const DirtyList *dirty)
{
    unsigned int i;
    unsigned int n = dirty->count;

    for (i = 0; i < n; i++) {
        *(dirty->addr[i]) ^= dirty->mask[i];
    }
}
