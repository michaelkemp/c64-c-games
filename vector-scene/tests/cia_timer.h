#ifndef CIA_TIMER_H
#define CIA_TIMER_H

/* Wraparound-safe cycle timer -- CIA1 Timer A and Timer B chained into a
   single 32-bit down-counter, same technique as
   hires-bounce/tests/bench_scene.c uses for a whole scene.

   Why this matters here, not just for whole scenes: while calibrating
   this folder's benchmarks (see README.md's "A real bug in the baseline
   number" section), a single hires-bounce-style draw_line() call turned
   out to cost roughly 700-750 cycles per pixel in this cc65 build, not
   the ~24 cycles/pixel a plain 16-bit Timer A reading of "~3800 cycles
   average" implies. Any line with a path length over ~90 pixels
   (65536 / ~730) silently overflows Timer A's 16-bit range; because of
   a real 6526 CIA quirk (on underflow the counter reloads from its
   latch and, in one-shot mode, then stops -- so a too-slow trial reads
   back as if it took exactly 0 cycles instead of erroring or reporting
   something implausibly large), that failure is silent and just pulls
   the average down. hires-bounce/tests/bench_line.c's published "~3800
   average" is very likely exactly this: a mix of real short-line
   readings and silently-zeroed long-line ones. Every benchmark in this
   folder uses the chained 32-bit timer below instead, specifically to
   not repeat that mistake. */

#define CIA1_TALO (*(volatile unsigned char *)0xDC04)
#define CIA1_TAHI (*(volatile unsigned char *)0xDC05)
#define CIA1_TBLO (*(volatile unsigned char *)0xDC06)
#define CIA1_TBHI (*(volatile unsigned char *)0xDC07)
#define CIA1_CRA  (*(volatile unsigned char *)0xDC0E)
#define CIA1_CRB  (*(volatile unsigned char *)0xDC0F)

static void cia_timer_start(void)
{
    SEI();
    CIA1_TALO = 0xFF;
    CIA1_TAHI = 0xFF;
    CIA1_TBLO = 0xFF;
    CIA1_TBHI = 0xFF;
    CIA1_CRA = 0x11; /* bit4 force-load, bit3=0 continuous, bit0 start */
    CIA1_CRB = 0x51; /* bit4 force-load, bit6:5=10 count TA underflows, bit0 start */
}

static unsigned long cia_timer_stop(void)
{
    unsigned char a_lo, a_hi, b_lo, b_hi;
    unsigned int a_remaining, b_remaining;
    unsigned long a_elapsed_in_chunk, b_chunks_elapsed;

    CIA1_CRA = 0x00;
    CIA1_CRB = 0x00;
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

#endif
