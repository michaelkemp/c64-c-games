#include <6502.h>
#include <c64.h>
#include <conio.h>
#include <stdlib.h>

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
#define CPU_PORT (*(volatile unsigned char *)0x0001)

/* C64 keyboard matrix positions -- verified against an authoritative
   reference (twice) as (column,row) = I=(1,4) J=(2,4) K=(5,4) L=(2,5)
   SPACE=(4,7), which were correct... as (column,row) pairs. The real
   bug, found by the user systematically testing which physical key
   actually triggered each action: N/F/C/"."/Z were firing/rotating/
   hyperspace-ing/thrusting instead of space/L/J/K/I -- and every one
   of those five turned out to be the exact TRANSPOSE of the intended
   key ((1,4) vs Z's real (4,1), (4,7) vs N's real (7,4), etc, 5 for
   5). That means the verified column/row *numbers* were never wrong;
   CIA1_PRA (used here to SELECT) and CIA1_PRB (used here to TEST) were
   swapped the whole time -- what this file called a key's "column"
   needs to go into the PRB test, and what it called the "row" needs
   to go into the PRA select, the opposite of every version before
   this one. Below, each key's SELECT value is its old "row" number
   and its TEST value is its old "column" number. */

static unsigned char keyI, keyJ, keyK, keyL, keySpace;

/* Scans all five keys in ONE SEI/CLI block, not five separate ones.
   Found by testing (back when this was W/A/D): with two of the keys
   each wired to their own separate key_down() call (each with its own
   SEI/select/read/restore/CLI), the ship's heading drifted on its own
   with nobody touching the keyboard -- but only when both checks were
   active; either alone was rock solid. The KERNAL's own IRQ handler
   scans the keyboard too, and it was landing in the brief
   CLI-to-next-SEI gap between the two separate calls, disturbing
   CIA1_PRA/PRB just before the second call's own select+read. Scanning
   everything in one uninterruptible block removes that gap entirely.
   I, J and K now share one select (their old "row", 4) too, so that
   line only needs selecting once for all three. */
static void scan_keys(void)
{
    unsigned char saved = CIA1_PRA;

    SEI();
    CIA1_PRA = ~(1 << 4);
    keyI = !(CIA1_PRB & (1 << 1));
    keyJ = !(CIA1_PRB & (1 << 2));
    keyK = !(CIA1_PRB & (1 << 5));
    CIA1_PRA = ~(1 << 5);
    keyL = !(CIA1_PRB & (1 << 2));
    CIA1_PRA = ~(1 << 7);
    keySpace = !(CIA1_PRB & (1 << 4));
    CIA1_PRA = saved;
    CLI();
}

static unsigned int frameCount = 0; /* wraps at 65536 frames (~20min @ 50Hz) -- fine for debug */

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

/* v -= v>>FRICTION_SHIFT, done symmetrically. Plain `v -= v>>7` looked
   like friction but wasn't: `>>` on a signed int is an ARITHMETIC
   shift, which is not symmetric around zero. For 0<=v<128, v>>7 floors
   to exactly 0, so positive velocity never decayed at all. For
   -128<v<0, arithmetic shift rounds toward negative infinity, giving
   -1, so negative velocity DID decay, by 1/frame. That's exactly the
   reported bug: left/up (negative) slowed to a stop, right/down
   (positive) never did. Shifting the magnitude instead of the signed
   value, and clamping so the decay can't overshoot past zero and
   oscillate, fixes both directions the same way. */
static void apply_friction(int *v)
{
    if (*v > 0) {
        *v -= (*v >> FRICTION_SHIFT) + 1;
        if (*v < 0) *v = 0;
    } else if (*v < 0) {
        *v += ((-*v) >> FRICTION_SHIFT) + 1;
        if (*v > 0) *v = 0;
    }
}

static unsigned char keyKPrev;

static void update_ship(void)
{
    if (keyJ) {
        shipAngle = (shipAngle < 3) ? shipAngle + 357 : shipAngle - 3;
    }
    if (keyL) {
        shipAngle += 3;
        if (shipAngle >= 360) shipAngle -= 360;
    }
    if (keyI) {
        shipVX += THRUST_DX[shipAngle];
        shipVY += THRUST_DY[shipAngle];
    }

    apply_friction(&shipVX);
    apply_friction(&shipVY);
    clamp_speed(&shipVX);
    clamp_speed(&shipVY);

    shipX = wrap(shipX + shipVX, X_WRAP);
    shipY = wrap(shipY + shipVY, Y_WRAP);

    /* Hyperspace (matches the prototype's down-arrow jump): triggers
       once per press, not every frame it's held -- edge-detected
       against last frame's state, same idea as a bullet's cooldown. */
    if (keyK && !keyKPrev) {
        shipX = (int)(rand() % SCREEN_W) << POS_SHIFT;
        shipY = (int)(rand() % SCREEN_H) << POS_SHIFT;
        shipVX = 0;
        shipVY = 0;
    }
    keyKPrev = keyK;
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

/* Screen-space clipping before every draw_line_asm() call --
   draw_line_asm() does raw pointer arithmetic into the bitmap with no
   bounds checking of its own, so a segment endpoint that ends up
   negative or past 319/199 isn't a cosmetic glitch, it's a write to
   whatever memory that arithmetic happens to land on.

   A first version just clamped each coordinate independently to the
   valid range instead of computing the real line/boundary
   intersection -- cheaper, but wrong: clamping distorts the segment's
   slope near the edge, which is what "the shape deforms, it tries to
   maintain a full triangle and is crushed" was. This is the real
   fix: proper Liang-Barsky line clipping (standard, textbook
   computational geometry) against the [0,SCREEN_W-1]x[0,SCREEN_H-1]
   box, computed with a fixed-point parameter (T_SCALE) instead of
   floats, since the 6502 has no hardware float. Segments fully inside
   already (the common case, whenever the ship isn't near an edge)
   skip the clipping math entirely. */
#define T_SCALE 256L

static unsigned char clip_segment(int x0, int y0, int x1, int y1,
                                   int *rx0, int *ry0, int *rx1, int *ry1)
{
    long dx, dy, t0, t1, p, q, r;

    if (x0 >= 0 && x0 < SCREEN_W && x1 >= 0 && x1 < SCREEN_W &&
        y0 >= 0 && y0 < SCREEN_H && y1 >= 0 && y1 < SCREEN_H) {
        *rx0 = x0; *ry0 = y0; *rx1 = x1; *ry1 = y1;
        return 1;
    }

    dx = x1 - x0;
    dy = y1 - y0;
    t0 = 0;
    t1 = T_SCALE;

    /* left: x >= 0 */
    p = -dx; q = x0;
    if (p == 0) { if (q < 0) return 0; }
    else {
        r = (q * T_SCALE) / p;
        if (p < 0) { if (r > t1) return 0; if (r > t0) t0 = r; }
        else       { if (r < t0) return 0; if (r < t1) t1 = r; }
    }
    /* right: x <= SCREEN_W-1 */
    p = dx; q = (SCREEN_W - 1) - x0;
    if (p == 0) { if (q < 0) return 0; }
    else {
        r = (q * T_SCALE) / p;
        if (p < 0) { if (r > t1) return 0; if (r > t0) t0 = r; }
        else       { if (r < t0) return 0; if (r < t1) t1 = r; }
    }
    /* top: y >= 0 */
    p = -dy; q = y0;
    if (p == 0) { if (q < 0) return 0; }
    else {
        r = (q * T_SCALE) / p;
        if (p < 0) { if (r > t1) return 0; if (r > t0) t0 = r; }
        else       { if (r < t0) return 0; if (r < t1) t1 = r; }
    }
    /* bottom: y <= SCREEN_H-1 */
    p = dy; q = (SCREEN_H - 1) - y0;
    if (p == 0) { if (q < 0) return 0; }
    else {
        r = (q * T_SCALE) / p;
        if (p < 0) { if (r > t1) return 0; if (r > t0) t0 = r; }
        else       { if (r < t0) return 0; if (r < t1) t1 = r; }
    }

    if (t0 > t1) return 0;

    *rx0 = x0 + (int)((dx * t0) / T_SCALE);
    *ry0 = y0 + (int)((dy * t0) / T_SCALE);
    *rx1 = x0 + (int)((dx * t1) / T_SCALE);
    *ry1 = y0 + (int)((dy * t1) / T_SCALE);

    /* Belt-and-suspenders: T_SCALE's fixed-point division truncates
       rather than rounds, so a t0/t1 that lands just inside [0,T_SCALE]
       can still map back to a coordinate exactly one pixel *outside*
       the box (e.g. y=-1 or y=SCREEN_H) -- confirmed by sweeping this
       function standalone. draw_line_asm() has no bounds checking of
       its own and computes its row offset from y0 with an 8-bit shift,
       so an out-of-range y here doesn't just draw one wrong pixel, it
       indexes past the row-offset table and toggles a bit at whatever
       address the garbage offset lands on -- observed live as VIC-II's
       background color register getting clobbered (solid blue screen).
       Clamping the already-near-boundary result is a 0-1px nudge, not
       the old whole-segment clamp that crushed the ship's slope. */
    if (*rx0 < 0) *rx0 = 0; else if (*rx0 >= SCREEN_W) *rx0 = SCREEN_W - 1;
    if (*rx1 < 0) *rx1 = 0; else if (*rx1 >= SCREEN_W) *rx1 = SCREEN_W - 1;
    if (*ry0 < 0) *ry0 = 0; else if (*ry0 >= SCREEN_H) *ry0 = SCREEN_H - 1;
    if (*ry1 < 0) *ry1 = 0; else if (*ry1 >= SCREEN_H) *ry1 = SCREEN_H - 1;

    return 1;
}

/* Not a bug we're guessing might exist -- a hard backstop against ANY
   bad data reaching draw_line_asm(), current bug or future one. That
   function has no bounds checking of its own and a bad y0/y1 doesn't
   just misdraw a pixel, it indexes off the end of its row-offset table
   and XORs a bit at whatever address the garbage offset lands on (this
   is exactly how the clip_segment off-by-one clobbered VIC-II's
   background color register). Rather than keep finding these one
   live-play-session at a time, this checks everything actually about
   to be sent, every single call, and if it's ever wrong, stops dead
   and dumps every value that could explain why -- so the next one (if
   any) gets diagnosed from a screen of real data, not another round of
   theorizing. This is deliberately not cheap; that's fine here.

   No cprintf(): its variadic-printf engine alone overflowed main.c
   past __HIMEM__ outright the first time this was tried (a link
   error, not a silent overflow -- cfg/lowmem.cfg doing its job). A
   from-scratch hand-rolled digit-poke version (no conio at all) was
   tried next and was WORSE -- cc65's C-to-6502 codegen for an
   equivalent manual division/pointer loop turned out bulkier than
   itoa()'s own hand-tuned asm. itoa()+cputs() (one label, one number,
   one line, no format string parsing) is the size that actually fits,
   found by measuring, not guessing -- see cfg/lowmem.cfg's __HIMEM__
   comment for where the room this needed came from. */
static void panic_line(const char *label, int val)
{
    char buf[8];
    cputs(label);
    cputs(itoa(val, buf, 10));
    cputs("\r\n");
}

static void panic_bad_line(const char *tag, int rawXa, int rawYa, int rawXb, int rawYb)
{
    SEI();

    CPU_PORT |= 0x01;                    /* bank BASIC ROM back in (KERNAL stayed in) */
    CIA2.pra = (CIA2.pra & 0xFC) | 0x03; /* VIC bank 0 -- default $0000-$3FFF */
    VIC.ctrl1 &= ~0x20;                  /* BMM=0: back to text mode */
    VIC.ctrl2 &= ~0x10;                  /* MCM=0 */
    VIC.addr = 0x15;                     /* screen $0400, charset $1000 -- KERNAL default */
    VIC.bordercolor = COLOR_RED;
    VIC.bgcolor0 = COLOR_BLACK;

    clrscr();
    cputs(tag);
    panic_line("frame  ", (int)frameCount);
    panic_line("raw x0 ", rawXa);
    panic_line("raw y0 ", rawYa);
    panic_line("raw x1 ", rawXb);
    panic_line("raw y1 ", rawYb);
    panic_line("dla x0 ", dla_x0);
    panic_line("dla y0 ", dla_y0);
    panic_line("dla x1 ", dla_x1);
    panic_line("dla y1 ", dla_y1);

    for (;;) {
    }
}

static void draw_segment_clamped(int xa, int ya, int xb, int yb)
{
    int rawXa = xa, rawYa = ya, rawXb = xb, rawYb = yb;

    if (!clip_segment(xa, ya, xb, yb, &xa, &ya, &xb, &yb)) return;

    if (xa <= xb) {
        dla_x0 = xa; dla_y0 = ya; dla_x1 = xb; dla_y1 = yb;
    } else {
        dla_x0 = xb; dla_y0 = yb; dla_x1 = xa; dla_y1 = ya;
    }

    if (dla_x0 < 0 || dla_x1 >= SCREEN_W || dla_x0 > dla_x1 ||
        dla_y0 < 0 || dla_y0 >= SCREEN_H ||
        dla_y1 < 0 || dla_y1 >= SCREEN_H) {
        panic_bad_line("*** LINE DRAW PANIC ***\r\n", rawXa, rawYa, rawXb, rawYb);
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
        /* A single pixel is real, but hard to be sure you're seeing it
           at C64 resolution -- a tiny 3px diagonal is unmistakable, so
           testing whether space fires at all doesn't come down to
           squinting at the screen. */
        draw_segment_clamped(bx - 1, by - 1, bx + 1, by + 1);
    }
}

int main(void)
{
    unsigned char back = 1; /* buffer 0 is shown first by doublebuf_init() */

    srand(VIC.rasterline); /* for hyperspace's random destination */
    doublebuf_init();

    for (;;) {
        frameCount++;
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
