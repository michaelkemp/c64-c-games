#include <6502.h>
#include <c64.h>
#include <conio.h>
#include <string.h>

#include "hires.h"

/* Benchmark for a "realistic scene": one ship + NUM_ASTEROIDS asteroids,
   redrawn XOR-style every frame (erase old position, move, draw new
   position -- exactly what main.c does for a single line, scaled up to
   a real object count). Answers a direct question raised while working
   on this project: at Asteroids-like scene complexity, how many cycles
   does one frame actually cost against real 50/60Hz frame budgets?

   Shapes/scale/count are not guessed -- they're taken straight from the
   JS prototype this project is porting (../../../asteroids/index.html):
     - `fighter` (the ship outline) * ship.scale (12)
     - `rocks[0].concave` (one asteroid outline) * BIG (40)
     - difficulty[0].ROCKS = 3 -- the easiest level's asteroid count,
       matching ~/Desktop/asteroids.png (3 big asteroids + the ship). */

/* 20, not bench_line.c's 100 -- this scene turned out expensive enough
   (see time_frame()'s comment) that 100 trials would take uncomfortably
   long to sit through; 20 is still plenty to average out incidental
   per-position variance (byte-alignment/column-crossing counts differ
   slightly by exact pixel position). */
#define TRIALS        20
#define NUM_ASTEROIDS 3

#define SHIP_POINTS 6
#define SHIP_HALF   12 /* ship.scale in the JS prototype */

/* fighter = [[1,0],[-0.9,0.7],[-0.6,0.5],[-0.6,-0.5],[-0.9,-0.7],[1,0]]
   times ship.scale (12), rounded to the nearest pixel. Already closed
   (first point repeats as the last). */
static const signed char SHIP_DX[SHIP_POINTS] = { 12, -11, -7, -7, -11, 12 };
static const signed char SHIP_DY[SHIP_POINTS] = { 0, 8, 6, -6, -8, 0 };

#define ROCK_POINTS 11
#define ROCK_HALF   40 /* BIG in the JS prototype */

/* rocks[0].concave times BIG (40). Same shape reused for all three
   asteroids -- real variety (different rock/rotation per asteroid) is
   an asteroids/ concern, not this benchmark's. */
static const signed char ROCK_DX[ROCK_POINTS] = { 40, 30, 40, 20, 0, -20, -40, -40, -20, 10, 40 };
static const signed char ROCK_DY[ROCK_POINTS] = { 20, 0, -20, -40, -20, -40, -20, 20, 40, 40, 20 };

typedef struct {
    int cx, cy;
    int vx, vy;
} Obj;

static Obj ship = { 160, 100, 1, 1 };
static Obj asteroids[NUM_ASTEROIDS] = {
    { 70, 60, 1, 1 },
    { 230, 80, -1, 1 },
    { 130, 150, 1, -1 },
};

/* Direct volatile addresses, not c64.h's CIA1 macro -- see bench_line.c
   for why. Timer A and B are cascaded into one 32-bit down-counter (see
   time_frame()): a single 16-bit timer, as bench_line.c uses for one
   line, turned out not to be enough range for a whole scene. */
#define CIA1_TALO (*(volatile unsigned char *)0xDC04)
#define CIA1_TAHI (*(volatile unsigned char *)0xDC05)
#define CIA1_TBLO (*(volatile unsigned char *)0xDC06)
#define CIA1_TBHI (*(volatile unsigned char *)0xDC07)
#define CIA1_CRA  (*(volatile unsigned char *)0xDC0E)
#define CIA1_CRB  (*(volatile unsigned char *)0xDC0F)

static void draw_shape(const signed char *dx, const signed char *dy, unsigned char n, int cx, int cy)
{
    unsigned char i;
    for (i = 0; i + 1 < n; i++) {
        draw_line(cx + dx[i], cy + dy[i], cx + dx[i + 1], cy + dy[i + 1]);
    }
}

static void move_obj(Obj *o, int half)
{
    o->cx += o->vx;
    if (o->cx < half) {
        o->cx = half;
        o->vx = -o->vx;
    } else if (o->cx > SCREEN_W - 1 - half) {
        o->cx = SCREEN_W - 1 - half;
        o->vx = -o->vx;
    }

    o->cy += o->vy;
    if (o->cy < half) {
        o->cy = half;
        o->vy = -o->vy;
    } else if (o->cy > SCREEN_H - 1 - half) {
        o->cy = SCREEN_H - 1 - half;
        o->vy = -o->vy;
    }
}

/* One simulated game frame: erase everything at its current position
   (XOR redraw cancels it), move it, draw it at the new position. */
static void run_frame(void)
{
    unsigned char i;

    draw_shape(SHIP_DX, SHIP_DY, SHIP_POINTS, ship.cx, ship.cy);
    for (i = 0; i < NUM_ASTEROIDS; i++) {
        draw_shape(ROCK_DX, ROCK_DY, ROCK_POINTS, asteroids[i].cx, asteroids[i].cy);
    }

    move_obj(&ship, SHIP_HALF);
    for (i = 0; i < NUM_ASTEROIDS; i++) {
        move_obj(&asteroids[i], ROCK_HALF);
    }

    draw_shape(SHIP_DX, SHIP_DY, SHIP_POINTS, ship.cx, ship.cy);
    for (i = 0; i < NUM_ASTEROIDS; i++) {
        draw_shape(ROCK_DX, ROCK_DY, ROCK_POINTS, asteroids[i].cx, asteroids[i].cy);
    }
}

/* Times one run_frame() call in raw 6502 cycles, using Timer A and
   Timer B cascaded into a single 32-bit down-counter.
   ---
   The first version of this benchmark used a single 16-bit Timer A,
   the same technique bench_line.c uses for one line -- and it silently
   reported "average: 0 cycles". The real cause is a genuine 6526 CIA
   quirk, not a typo: on underflow, the counter reloads from its latch
   *regardless of one-shot/continuous mode* -- one-shot only controls
   whether it then keeps counting or stops. So if run_frame() takes
   longer than Timer A's full 16-bit range (65,536 cycles, ~66ms), the
   counter underflows, silently reloads to $FFFF, and stops -- reading
   back a "remaining" count of $FFFF, which is indistinguishable from
   "the timer never started". Elapsed came out as 0 every single trial
   because the real cost is well over 65,536 cycles for this scene --
   itself a finding (that alone already means "many real video frames
   per redraw", the question this benchmark exists to answer), just not
   one this timer could put a number on.

   Fix: chain Timer B to count Timer A's underflows (CRB's INMODE = 2,
   "count Timer A underflows"), giving a combined 32-bit range (up to
   ~4.3 billion cycles, ~4300 real seconds -- comfortably enough).
   Elapsed = (B's completed underflow-counts * 65536) + A's count within
   the current chunk. */
static unsigned long time_frame(void)
{
    unsigned char a_lo, a_hi, b_lo, b_hi;
    unsigned int a_remaining, b_remaining;
    unsigned long a_elapsed_in_chunk, b_chunks_elapsed;

    SEI();
    CIA1_TALO = 0xFF;
    CIA1_TAHI = 0xFF;
    CIA1_TBLO = 0xFF;
    CIA1_TBHI = 0xFF;
    CIA1_CRA = 0x11; /* bit4 force-load, bit3=0 continuous, bit0 start */
    CIA1_CRB = 0x51; /* bit4 force-load, bit6:5=10 count TA underflows, bit0 start */

    run_frame();

    CIA1_CRA = 0x00; /* stop */
    CIA1_CRB = 0x00; /* stop */
    a_lo = CIA1_TALO;
    a_hi = CIA1_TAHI;
    b_lo = CIA1_TBLO;
    b_hi = CIA1_TBHI;
    CLI();

    a_remaining = (unsigned int)a_lo | ((unsigned int)a_hi << 8);
    b_remaining = (unsigned int)b_lo | ((unsigned int)b_hi << 8);

    a_elapsed_in_chunk = 0xFFFFul - a_remaining;
    b_chunks_elapsed = 0xFFFFul - b_remaining;

    return (b_chunks_elapsed * 0x10000ul) + a_elapsed_in_chunk;
}

int main(void)
{
    unsigned char i;
    unsigned long total = 0;
    unsigned long avg_cycles;
    unsigned long avg_ms;
    unsigned long frames_pal, frames_ntsc;
    unsigned int pct_ntsc60, pct_pal50;

    memset(BITMAP, 0x00, (SCREEN_W / 8) * SCREEN_H);
    memset(SCREEN_MATRIX, 0x10, 1000);
    hires_on();

    run_frame(); /* draw the starting frame so the first timed erase has something to cancel */

    for (i = 0; i < TRIALS; i++) {
        total += time_frame();
    }

    /* back to text mode to report the result */
    VIC.ctrl1 &= ~0x20;
    VIC.addr = 0x14;
    CIA2.pra = (CIA2.pra & 0xFC) | 0x03;

    avg_cycles = total / TRIALS;
    avg_ms = avg_cycles / 985UL; /* approx PAL cycles-per-ms (985248 Hz / 1000) */

    /* NTSC ~17095 cycles/frame @ 59.83Hz, PAL ~19656 @ 50.12Hz. */
    frames_pal = avg_cycles / 19656UL;
    frames_ntsc = avg_cycles / 17095UL;
    pct_pal50 = (unsigned int)((avg_cycles * 100UL) / 19656UL);
    pct_ntsc60 = (unsigned int)((avg_cycles * 100UL) / 17095UL);

    clrscr();
    cprintf("scene: ship + %u asteroids, %u trials\r\n\r\n", (unsigned)NUM_ASTEROIDS, (unsigned)TRIALS);
    cprintf("average: %lu cycles (%lu ms)\r\n\r\n", avg_cycles, avg_ms);
    cprintf("pal  50hz: %lu frames (%u%%)\r\n", frames_pal, pct_pal50);
    cprintf("ntsc 60hz: %lu frames (%u%%)\r\n", frames_ntsc, pct_ntsc60);

    while (1) {
    }

    return 0;
}
