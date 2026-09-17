#include <c64.h>
#include <string.h>

#include "hires.h"

/* Two points bounce around a 320x200 hi-res bitmap; a line is drawn
   between them every frame. See README.md for the VIC-II register
   layout and the XOR erase/redraw trick this relies on. hires.c/.h
   hold the bitmap setup and line-drawing code -- tests/bench_line.c
   reuses the exact same draw_line(). */

typedef struct {
    int x, y;
    int vx, vy;
} Point;

static void move_point(Point *p)
{
    p->x += p->vx;
    if (p->x < 0) {
        p->x = 0;
        p->vx = -p->vx;
    } else if (p->x > SCREEN_W - 1) {
        p->x = SCREEN_W - 1;
        p->vx = -p->vx;
    }

    p->y += p->vy;
    if (p->y < 0) {
        p->y = 0;
        p->vy = -p->vy;
    } else if (p->y > SCREEN_H - 1) {
        p->y = SCREEN_H - 1;
        p->vy = -p->vy;
    }
}

int main(void)
{
    Point a = { 40, 20, 3, 2 };
    Point b = { 260, 160, -2, 3 };

    memset(BITMAP, 0x00, (SCREEN_W / 8) * SCREEN_H);
    memset(SCREEN_MATRIX, 0x10, 1000); /* white pixels (bit=1) on black (bit=0) */

    hires_on();

    draw_line(a.x, a.y, b.x, b.y); /* first draw: XOR on a blank bitmap == set */

    for (;;) {
        /* Crude vsync: wait for the raster beam to reach the bottom
           border so each step below is drawn during blank, not torn
           mid-frame. Not IRQ-driven -- good enough for this demo. */
        while (VIC.rasterline != 250) {
        }

        draw_line(a.x, a.y, b.x, b.y); /* erase: 2nd XOR cancels the 1st */
        move_point(&a);
        move_point(&b);
        draw_line(a.x, a.y, b.x, b.y); /* draw the line at its new position */
    }

    return 0;
}
