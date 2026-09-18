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

static unsigned char key_down(unsigned char col, unsigned char row)
{
    unsigned char saved = CIA1_PRA;
    unsigned char pressed;

    SEI(); /* the KERNAL's own IRQ handler scans the keyboard too --
              stop it from changing CIA1_PRA out from under us mid-scan */
    CIA1_PRA = ~(1 << col);
    pressed = !(CIA1_PRB & (1 << row));
    CIA1_PRA = saved;
    CLI();

    return pressed;
}

/* C64 keyboard matrix (column, row) -- see main.c's git history /
   README for the derivation if these ever need rechecking. */
#define KEY_W()     key_down(1, 1)
#define KEY_A()     key_down(2, 1)
#define KEY_D()     key_down(2, 2)
#define KEY_SPACE() key_down(4, 7)

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
    if (KEY_A()) {
        shipAngle = (shipAngle < 3) ? shipAngle + 357 : shipAngle - 3;
    }
    if (KEY_D()) {
        shipAngle += 3;
        if (shipAngle >= 360) shipAngle -= 360;
    }
    if (KEY_W()) {
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

    if (!KEY_SPACE()) return;

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

static void draw_ship(unsigned char *bitmap_base)
{
    unsigned char i;
    int cx = shipX >> POS_SHIFT;
    int cy = shipY >> POS_SHIFT;

    dla_bitmap_base = bitmap_base;

    for (i = 0; i < SHIP_POINTS - 1; i++) {
        int xa = cx + SHIP_DX[shipAngle][i];
        int ya = cy + SHIP_DY[shipAngle][i];
        int xb = cx + SHIP_DX[shipAngle][i + 1];
        int yb = cy + SHIP_DY[shipAngle][i + 1];

        if (xa <= xb) {
            dla_x0 = xa; dla_y0 = ya; dla_x1 = xb; dla_y1 = yb;
        } else {
            dla_x0 = xb; dla_y0 = yb; dla_x1 = xa; dla_y1 = ya;
        }
        draw_line_asm();
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
