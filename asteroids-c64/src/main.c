#include <6502.h>
#include <c64.h>

#include "doublebuf.h"
#include "drawline_asm.h"
#include "clearbitmap_asm.h"
#include "ship_table.h"
#include "dir_table.h"

/* Phase 1: the ship, rotating/thrusting/firing under real keyboard
   control (WASD to turn/thrust, space to fire) -- see README.md's
   phase list. Uses trick #4 (full clear) rather than trick #2/#3's
   per-object erase: with a variable number of live bullets appearing
   and disappearing, tracking "what did this buffer show last time" is
   real bookkeeping, and this scene is 100% moving content anyway
   (no static background yet), exactly the case vector-scene/README.md
   found full clear winning for.

   Position/velocity are fixed-point (POS_SHIFT fractional bits) --
   the 6502 has no hardware float, and this is the standard technique
   for smooth sub-pixel motion with integer-only pixel rendering. */

#define POS_SHIFT   6                    /* 1/64-pixel precision */
#define X_WRAP      (320 << POS_SHIFT)
#define Y_WRAP      (200 << POS_SHIFT)
#define MAX_SPEED   320                  /* 5 px/frame, matches the prototype */
#define FRICTION_SHIFT 7                 /* v -= v>>7, ~0.992/frame decay */

#define MAX_BULLETS 4
#define BULLET_LIFETIME 45               /* frames, matches the prototype (~1.5s @ 30fps) */

/* ---- real-time keyboard matrix scan (CIA1) -- NOT conio's
   kbhit()/cgetc(), which buffer discrete keypresses and can't report
   "is this key currently held down" the way a game needs. ---- */
#define CIA1_PRA (*(volatile unsigned char *)0xDC00)
#define CIA1_PRB (*(volatile unsigned char *)0xDC01)

/* C64 keyboard matrix (column, row) -- see main.c's git history /
   README for the derivation if these ever need rechecking:
   W=(1,1) A=(2,1) D=(2,2) SPACE=(4,7). */

static unsigned char keyW, keyA, keyD, keySpace;

/* Scans all four keys in ONE SEI/CLI block, not four separate ones.
   Found by testing: with A and D each wired to their own separate
   key_down() call (each with its own SEI/select/read/restore/CLI),
   the ship's heading drifted on its own with nobody touching the
   keyboard -- but only when A's *and* D's checks were both active;
   either alone was rock solid. The KERNAL's own IRQ handler scans the
   keyboard too, and it was landing in the brief CLI-to-next-SEI gap
   between the two separate calls, disturbing CIA1_PRA/PRB just
   before the second call's own select+read. Scanning everything in
   one uninterruptible block removes that gap entirely. A and D also
   share column 2, so that column only needs to be selected once. */
static void scan_keys(void)
{
    unsigned char saved = CIA1_PRA;

    SEI();
    CIA1_PRA = ~(1 << 1);
    keyW = !(CIA1_PRB & (1 << 1));
    CIA1_PRA = ~(1 << 2);
    keyA = !(CIA1_PRB & (1 << 1));
    keyD = !(CIA1_PRB & (1 << 2));
    CIA1_PRA = ~(1 << 4);
    keySpace = !(CIA1_PRB & (1 << 7));
    CIA1_PRA = saved;
    CLI();
}

static unsigned int shipAngle = 0;
static int shipX = 160 << POS_SHIFT;
static int shipY = 100 << POS_SHIFT;
static int shipVX = 0;
static int shipVY = 0;

static unsigned char bulletActive[MAX_BULLETS];
static int bulletX[MAX_BULLETS];
static int bulletY[MAX_BULLETS];
static int bulletVX[MAX_BULLETS];
static int bulletVY[MAX_BULLETS];
static unsigned char bulletLife[MAX_BULLETS];

static int wrap(int pos, int limit)
{
    while (pos < 0) pos += limit;
    while (pos >= limit) pos -= limit;
    return pos;
}

static void clamp_speed(int *v)
{
    if (*v > MAX_SPEED) *v = MAX_SPEED;
    if (*v < -MAX_SPEED) *v = -MAX_SPEED;
}

static void update_ship(void)
{
    if (keyA) {
        shipAngle = (shipAngle < 3) ? shipAngle + 357 : shipAngle - 3;
    }
    if (keyD) {
        shipAngle += 3;
        if (shipAngle >= 360) shipAngle -= 360;
    }
    if (keyW) {
        shipVX += THRUST_DX[shipAngle];
        shipVY += THRUST_DY[shipAngle];
    }

    shipVX -= shipVX >> FRICTION_SHIFT;
    shipVY -= shipVY >> FRICTION_SHIFT;
    clamp_speed(&shipVX);
    clamp_speed(&shipVY);

    shipX = wrap(shipX + shipVX, X_WRAP);
    shipY = wrap(shipY + shipVY, Y_WRAP);
}

static void try_fire(void)
{
    unsigned char i;

    if (!keySpace) return;

    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bulletActive[i]) {
            /* spawn just ahead of the nose (ship_table point 0) */
            bulletX[i] = shipX + ((int)SHIP_DX[shipAngle][0] << POS_SHIFT);
            bulletY[i] = shipY + ((int)SHIP_DY[shipAngle][0] << POS_SHIFT);
            bulletVX[i] = BULLET_DX[shipAngle];
            bulletVY[i] = BULLET_DY[shipAngle];
            bulletLife[i] = BULLET_LIFETIME;
            bulletActive[i] = 1;
            break;
        }
    }
}

static void update_bullets(void)
{
    unsigned char i;

    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bulletActive[i]) continue;

        if (--bulletLife[i] == 0) {
            bulletActive[i] = 0;
            continue;
        }
        bulletX[i] = wrap(bulletX[i] + bulletVX[i], X_WRAP);
        bulletY[i] = wrap(bulletY[i] + bulletVY[i], Y_WRAP);
    }
}

/* Screen-space clamp before every draw_line_asm() call --
   draw_line_asm() does raw pointer arithmetic into the bitmap with no
   bounds checking of its own, so a segment endpoint that ends up
   negative or past 319/199 isn't a cosmetic glitch, it's a write to
   whatever memory that arithmetic happens to land on.

   First a trivial-reject test: if both endpoints are outside the
   screen on the *same* side (both left of 0, both right of 319, etc),
   none of the segment is visible -- skip it outright, rather than
   clamping it, or it would get snapped onto the boundary and draw a
   spurious line that doesn't belong there. Otherwise, at least part
   of the segment is genuinely on-screen: clamp each coordinate to the
   valid range and draw that. This isn't exact geometric line
   clipping (the clamped line's slope near the boundary isn't quite
   the true intersection), but for the short segments this project
   draws it draws right up to the edge instead of either vanishing or
   corrupting memory, which is what actually matters here. */
static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void draw_segment_clamped(int xa, int ya, int xb, int yb)
{
    if ((xa < 0 && xb < 0) || (xa >= SCREEN_W && xb >= SCREEN_W)) return;
    if ((ya < 0 && yb < 0) || (ya >= SCREEN_H && yb >= SCREEN_H)) return;

    xa = clampi(xa, 0, SCREEN_W - 1);
    xb = clampi(xb, 0, SCREEN_W - 1);
    ya = clampi(ya, 0, SCREEN_H - 1);
    yb = clampi(yb, 0, SCREEN_H - 1);

    if (xa <= xb) {
        dla_x0 = xa; dla_y0 = ya; dla_x1 = xb; dla_y1 = yb;
    } else {
        dla_x0 = xb; dla_y0 = yb; dla_x1 = xa; dla_y1 = ya;
    }
    draw_line_asm();
}

static void draw_ship_at(int cx, int cy)
{
    unsigned char i;

    for (i = 0; i < SHIP_POINTS - 1; i++) {
        draw_segment_clamped(cx + SHIP_DX[shipAngle][i], cy + SHIP_DY[shipAngle][i],
                              cx + SHIP_DX[shipAngle][i + 1], cy + SHIP_DY[shipAngle][i + 1]);
    }
}

#define SHIP_MARGIN 16 /* >= the ship's own max vertex radius (~11px), plus slack */

static void draw_ship(unsigned char *bitmap_base)
{
    int cx = shipX >> POS_SHIFT;
    int cy = shipY >> POS_SHIFT;
    int xs[2], ys[2];
    unsigned char nx = 1, ny = 1, i, j;

    dla_bitmap_base = bitmap_base;

    xs[0] = cx;
    ys[0] = cy;

    /* Near a corner, both an x-shift-only and a y-shift-only copy are
       needed, not just one combined diagonal shift: with only the
       diagonal copy, the slice of ship near the x-edge (needing an
       x-shift with y untouched) and the slice near the y-edge
       (needing a y-shift with x untouched) both fell outside every
       copy actually drawn -- confirmed by testing near (0,0), where
       nothing appeared at all despite everything else working.
       Drawing every combination of the applicable x and y shifts (up
       to 2x2 = 4 copies right at a corner) covers all of them;
       draw_segment_clamped()'s trivial-reject means each copy only
       contributes whatever part of it is actually relevant. */
    if (cx < SHIP_MARGIN) {
        xs[1] = cx + SCREEN_W;
        nx = 2;
    } else if (cx >= SCREEN_W - SHIP_MARGIN) {
        xs[1] = cx - SCREEN_W;
        nx = 2;
    }
    if (cy < SHIP_MARGIN) {
        ys[1] = cy + SCREEN_H;
        ny = 2;
    } else if (cy >= SCREEN_H - SHIP_MARGIN) {
        ys[1] = cy - SCREEN_H;
        ny = 2;
    }

    for (i = 0; i < nx; i++) {
        for (j = 0; j < ny; j++) {
            draw_ship_at(xs[i], ys[j]);
        }
    }
}

static void draw_bullets(unsigned char *bitmap_base)
{
    unsigned char i;

    dla_bitmap_base = bitmap_base;

    for (i = 0; i < MAX_BULLETS; i++) {
        int bx, by;
        if (!bulletActive[i]) continue;
        bx = bulletX[i] >> POS_SHIFT;
        by = bulletY[i] >> POS_SHIFT;
        dla_x0 = bx; dla_y0 = by; dla_x1 = bx; dla_y1 = by; /* single pixel */
        draw_line_asm();
    }
}

int main(void)
{
    unsigned char back = 1; /* buffer 0 is shown first by doublebuf_init() */

    doublebuf_init();

    for (;;) {
        scan_keys();
        update_ship();
        try_fire();
        update_bullets();

        if (back == 0) {
            clear_bitmap_buf0();
        } else {
            clear_bitmap_buf1();
        }
        draw_ship(BUF_BITMAP[back]);
        draw_bullets(BUF_BITMAP[back]);

        while (VIC.rasterline != 250) {
        }
        doublebuf_show(back);

        back = 1 - back;
    }

    return 0;
}
