#include <6502.h>
#include <c64.h>
#include <stdlib.h>

#include "doublebuf.h"
#include "drawline_asm.h"
#include "clearbitmap_asm.h"
#include "ship_table.h"
#include "dir_table.h"

/* Ship demo, take 2. asteroids-c64/ (the first attempt) drew a ship
   near a screen edge as multiple partial copies -- one clipped against
   each edge the ship's own vertices poked past -- to make it look like
   it was smoothly sliding off one side and onto the other. That meant
   computing a real clipped line (Liang-Barsky, in fixed point) for
   every segment, every frame, and a single off-by-one in that math
   (confirmed, fixed, then found again in a different shape) was enough
   to hand draw_line_asm() a coordinate outside the bitmap -- which it
   trusts completely and has no bounds checking of its own for, so the
   result wasn't a misdrawn pixel, it was a wild pointer write anywhere
   in the 64K address space (observed live: it clobbered VIC-II's
   background color register).

   This version has no clipping math at all. update_ship()/
   update_bullets() instantly teleport the ship/bullet's position the
   moment it gets within one ship-radius (or bullet-radius) of any
   edge -- not a smooth wraparound, an actual jump, straight to the
   mirror position on the opposite side -- so its center is back
   inside a safe zone before that frame is ever drawn. draw_ship()/
   draw_bullets() still check every vertex they're about to draw
   against [0,SCREEN_W)x[0,SCREEN_H) directly, but as a backstop that
   should never actually trigger, not as the thing making wraparound
   look right: nothing this file computes is ever capable of being
   outside that box, and nothing gets a visible gap crossing an edge,
   because the position itself never lingers in the danger zone for a
   frame that gets drawn. */

#define POS_SHIFT   6                    /* 1/64-pixel precision */
#define X_WRAP      (320 << POS_SHIFT)
#define Y_WRAP      (200 << POS_SHIFT)
#define MAX_SPEED   320                  /* 5 px/frame, matches the prototype */
#define FRICTION_SHIFT 7                 /* v -= v>>7, ~0.992/frame decay */

/* Ship outline's farthest vertex from center is scale*sqrt(0.9^2+0.7^2)
   =~ 11.4px (see tools/gen_ship_table.py's BASE points); 12 is a
   whole-pixel ceiling on that, not a guess. update_ship() uses this to
   teleport the ship the INSTANT its center would put any vertex off
   bitmap, rather than let it drift smoothly into that zone and only
   then decide whether to draw -- see update_ship()'s comment. */
#define SHIP_MARGIN 12

#define MAX_BULLETS 4
#define BULLET_LIFETIME 45               /* frames, matches the prototype (~1.5s @ 30fps) */

/* Asteroids: 4 fixed lumpy-rock outlines (no rotation yet -- unlike
   the ship's per-heading SHIP_DX/DY, each shape covers its asteroid
   at every moment), lifted from the "concave" point lists in the JS
   prototype (github.com/michaelkemp/asteroids, gh-pages/index.html)
   and rescaled from its [-1,1] normalized units so the farthest
   vertex of any of the 4 lands at 14-15px from center -- ASTEROID_
   MARGIN is a whole-pixel ceiling on that (checked against the
   rounded tables below, not a guess), same instant-teleport
   reasoning as SHIP_MARGIN. Each shape is its own array (they're
   different lengths); AST_SHAPE_DX/DY/POINTS index them by shape id
   0-3. MAX_ASTEROIDS is 4 so init_asteroids() can hand out one of
   each shape. */
#define MAX_ASTEROIDS 4
#define ASTEROID_MARGIN 15

static const signed char AST0_DX[11] = { 13, 9, 13, 6, 0, -6, -13, -13, -6, 3, 13 };
static const signed char AST0_DY[11] = { 6, 0, -6, -13, -6, -13, -6, 6, 13, 13, 6 };

static const signed char AST1_DX[13] = { 6, 13, 6, 0, -6, -13, -9, -13, -6, -3, 6, 13, 6 };
static const signed char AST1_DY[13] = { -3, -6, -13, -9, -13, -6, 0, 6, 13, 9, 13, 3, -3 };

static const signed char AST2_DX[13] = { 3, 13, 13, 3, -6, -3, -13, -13, -6, 3, 6, 13, 3 };
static const signed char AST2_DY[13] = { 0, -3, -6, -13, -13, -6, -6, 3, 13, 9, 13, 6, 0 };

static const signed char AST3_DX[12] = { 13, 6, -3, -13, -6, -13, -6, 0, 0, 6, 13, 13 };
static const signed char AST3_DY[12] = { -3, -13, -13, -3, 0, 3, 13, 3, 13, 13, 3, -3 };

static const signed char * const AST_SHAPE_DX[4] = { AST0_DX, AST1_DX, AST2_DX, AST3_DX };
static const signed char * const AST_SHAPE_DY[4] = { AST0_DY, AST1_DY, AST2_DY, AST3_DY };
static const unsigned char AST_SHAPE_POINTS[4] = { 11, 13, 13, 12 };

#define MAX_POLY_POINTS 13   /* largest of SHIP_POINTS and AST_SHAPE_POINTS */

/* real-time keyboard matrix scan (CIA1) -- NOT conio's kbhit()/cgetc(),
   which buffer discrete keypresses and can't report "is this key
   currently held down" the way a game needs. Matrix positions below
   were confirmed against real hardware with tests/keytest.c (see
   asteroids-c64/'s README for how -- guessed/"authoritative reference"
   values were wrong twice before that). */
#define CIA1_PRA (*(volatile unsigned char *)0xDC00)
#define CIA1_PRB (*(volatile unsigned char *)0xDC01)

static unsigned char keyI, keyJ, keyK, keyL, keySpace;

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

static unsigned char shipAngle = 0;   /* index into the 24 headings, not degrees */
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

static int astX[MAX_ASTEROIDS];
static int astY[MAX_ASTEROIDS];
static int astVX[MAX_ASTEROIDS];
static int astVY[MAX_ASTEROIDS];
static unsigned char astShape[MAX_ASTEROIDS];

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

/* v -= v>>FRICTION_SHIFT, done symmetrically -- plain `v -= v>>7`
   looked like friction but wasn't: `>>` on a signed int is an
   ARITHMETIC shift, not symmetric around zero, so positive and
   negative velocity decayed at different rates. Shifting the
   magnitude instead of the signed value fixes both directions the
   same way; the +1/clamp keeps it from oscillating around zero. */
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

/* Instant teleport, not smooth wraparound: wrap() (called on x/y
   before this) keeps the position continuous (needed so velocity/
   friction math has no seam), but if left at that, the center would
   spend several frames drifting through [0,margin) /
   (SCREEN_W-margin, SCREEN_W) -- exactly the band where the matching
   draw_*()'s per-vertex check would keep rejecting it, i.e. visible
   empty frames. So the instant the center enters that band, jump it
   straight to the mirror position on the far side (offset by the
   full safe span, SCREEN_W-2*margin, not by SCREEN_W -- a
   whole-period shift is a no-op under wrap() and lands right back in
   the same danger band). That keeps the center inside [margin,
   SCREEN_W-margin) on every single frame its draw_*() ever sees it,
   so that bounds check is a backstop that should never actually
   fire, not the thing doing the work. margin must be a whole-pixel
   ceiling on the shape's farthest vertex from its own center (see
   SHIP_MARGIN/ASTEROID_MARGIN). */
static void teleport_near_edge(int *x, int *y, int margin)
{
    int cx = *x >> POS_SHIFT;
    int cy = *y >> POS_SHIFT;

    if (cx < margin) {
        *x += (SCREEN_W - 2 * margin) << POS_SHIFT;
    } else if (cx >= SCREEN_W - margin) {
        *x -= (SCREEN_W - 2 * margin) << POS_SHIFT;
    }
    if (cy < margin) {
        *y += (SCREEN_H - 2 * margin) << POS_SHIFT;
    } else if (cy >= SCREEN_H - margin) {
        *y -= (SCREEN_H - 2 * margin) << POS_SHIFT;
    }
}

static unsigned char keyKPrev;

static void update_ship(void)
{
    if (keyJ) {
        shipAngle = (shipAngle == 0) ? SHIP_HEADINGS - 1 : shipAngle - 1;
    }
    if (keyL) {
        shipAngle = (shipAngle + 1 == SHIP_HEADINGS) ? 0 : shipAngle + 1;
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
    teleport_near_edge(&shipX, &shipY, SHIP_MARGIN);

    /* Hyperspace (matches the prototype's down-arrow jump): triggers
       once per press, not every frame it's held. */
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

/* -2, -1, 1 or 2 px/frame, never 0 -- so no asteroid ends up sitting
   still. rand()%4-2 gives -2/-1/0/1; the 0 case gets remapped to 2. */
static int random_ast_vel(void)
{
    int v = (rand() % 4) - 2;
    if (v == 0) v = 2;
    return v << POS_SHIFT;
}

static void init_asteroids(void)
{
    unsigned char i;

    for (i = 0; i < MAX_ASTEROIDS; i++) {
        astX[i] = (int)(rand() % SCREEN_W) << POS_SHIFT;
        astY[i] = (int)(rand() % SCREEN_H) << POS_SHIFT;
        astVX[i] = random_ast_vel();
        astVY[i] = random_ast_vel();
        astShape[i] = i % 4;   /* one of each of the 4 shapes, for variety */
    }
}

static void update_asteroids(void)
{
    unsigned char i;

    for (i = 0; i < MAX_ASTEROIDS; i++) {
        astX[i] = wrap(astX[i] + astVX[i], X_WRAP);
        astY[i] = wrap(astY[i] + astVY[i], Y_WRAP);
        teleport_near_edge(&astX[i], &astY[i], ASTEROID_MARGIN);
    }
}

/* Shared by draw_ship() and draw_asteroids(): draws the closed
   outline `dx`/`dy` (npoints signed offsets from center, first point
   repeated as last) centered at (cx,cy).

   Checks every actual vertex, not a fixed worst-case radius around
   the center: a fixed margin sized for the shape's farthest point at
   ANY heading would blank it out earlier than necessary for most
   headings, since most of them don't reach that far in the direction
   of the nearby edge. Computing the real points and checking each one
   directly still costs nothing but comparisons -- no clipping math --
   and only blanks the shape when a point would truly land
   off-bitmap. */
static void draw_polygon(unsigned char *bitmap_base, int cx, int cy,
                          const signed char *dx, const signed char *dy,
                          unsigned char npoints)
{
    int px[MAX_POLY_POINTS], py[MAX_POLY_POINTS];
    unsigned char i;

    for (i = 0; i < npoints; i++) {
        px[i] = cx + dx[i];
        py[i] = cy + dy[i];
        if (px[i] < 0 || px[i] >= SCREEN_W || py[i] < 0 || py[i] >= SCREEN_H) {
            return;
        }
    }

    dla_bitmap_base = bitmap_base;

    for (i = 0; i < npoints - 1; i++) {
        dla_x0 = px[i];     dla_y0 = py[i];
        dla_x1 = px[i + 1]; dla_y1 = py[i + 1];
        if (dla_x0 > dla_x1) {
            int t;
            t = dla_x0; dla_x0 = dla_x1; dla_x1 = t;
            t = dla_y0; dla_y0 = dla_y1; dla_y1 = t;
        }
        draw_line_asm();
    }
}

static void draw_ship(unsigned char *bitmap_base)
{
    draw_polygon(bitmap_base, shipX >> POS_SHIFT, shipY >> POS_SHIFT,
                 SHIP_DX[shipAngle], SHIP_DY[shipAngle], SHIP_POINTS);
}

static void draw_asteroids(unsigned char *bitmap_base)
{
    unsigned char i;

    for (i = 0; i < MAX_ASTEROIDS; i++) {
        unsigned char shape = astShape[i];
        draw_polygon(bitmap_base, astX[i] >> POS_SHIFT, astY[i] >> POS_SHIFT,
                     AST_SHAPE_DX[shape], AST_SHAPE_DY[shape], AST_SHAPE_POINTS[shape]);
    }
}

/* A bullet is a single plotted pixel -- draw_line_asm()'s pixel_loop
   plots the first pixel before it ever checks x0==x1&&y0==y1, so a
   zero-length "line" is a correct, direct way to plot one point, not
   a special case. No margin/bounds check needed here at all: wrap()
   in update_bullets() already guarantees bx/by land in
   [0,SCREEN_W)x[0,SCREEN_H), and a single point has no extent that
   could poke past that on its own the way the ship's other vertices
   can. */
static void draw_bullets(unsigned char *bitmap_base)
{
    unsigned char i;

    dla_bitmap_base = bitmap_base;

    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bulletActive[i]) continue;

        dla_x0 = dla_x1 = bulletX[i] >> POS_SHIFT;
        dla_y0 = dla_y1 = bulletY[i] >> POS_SHIFT;
        draw_line_asm();
    }
}

int main(void)
{
    unsigned char back = 1; /* buffer 0 is shown first by doublebuf_init() */

    srand(VIC.rasterline); /* for hyperspace's random destination */
    doublebuf_init();
    init_asteroids();

    for (;;) {
        scan_keys();
        update_ship();
        try_fire();
        update_bullets();
        update_asteroids();

        if (back == 0) {
            clear_bitmap_buf0();
        } else {
            clear_bitmap_buf1();
        }
        draw_ship(BUF_BITMAP[back]);
        draw_bullets(BUF_BITMAP[back]);
        draw_asteroids(BUF_BITMAP[back]);

        /* Swap the instant drawing finishes, no wait for a specific
           raster line first -- when a frame's drawing runs long, that
           wait's cost isn't "a little extra," it's up to a full extra
           frame (waiting for line 250 to come back around). Measured
           as not a significant speedup on its own, but costs visible
           tearing when a swap lands mid-draw; kept anyway since
           scan_keys() is back to being called once per loop iteration
           (see its comment), where that wait's cost would otherwise
           still tax input responsiveness too. */
        doublebuf_show(back);

        back = 1 - back;
    }

    return 0;
}
