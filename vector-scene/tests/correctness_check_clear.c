#include <c64.h>
#include <conio.h>
#include <string.h>

#include "hires.h"
#include "doublebuf.h"
#include "clearbitmap_asm.h"

/* Correctness check for clear_bitmap_buf0/1() (src/clearbitmap_asm.s):
   fills both bitmaps with a non-zero pattern, clears each with the
   unrolled routine, then verifies every one of the 8000 bytes is
   zero AND that nothing outside that exact range (checked via the
   byte just before the bitmap and the byte just after byte 7999) was
   touched. See README.md's "Trick #4" section. */

int main(void)
{
    unsigned char *b0 = BUF_BITMAP[0];
    unsigned char *b1 = BUF_BITMAP[1];
    unsigned int i;
    unsigned char ok0 = 1, ok1 = 1;
    unsigned char guard0_before, guard0_after, guard1_before, guard1_after;

    doublebuf_init(); /* clears LORAM for buf1, sets up both bitmaps/screens */

    /* re-fill with a non-zero pattern so a real clear is being tested */
    memset(b0, 0xFF, 8000);
    memset(b1, 0xFF, 8000);
    b0[-1] = 0xAA;      /* guard byte just before buf0's bitmap */
    b0[8000] = 0xAA;    /* guard byte just after buf0's bitmap */
    b1[-1] = 0xAA;
    b1[8000] = 0xAA;

    clear_bitmap_buf0();
    clear_bitmap_buf1();

    for (i = 0; i < 8000; i++) {
        if (b0[i] != 0x00) ok0 = 0;
        if (b1[i] != 0x00) ok1 = 0;
    }
    guard0_before = b0[-1];
    guard0_after = b0[8000];
    guard1_before = b1[-1];
    guard1_after = b1[8000];

    *(volatile unsigned char *)0x3F00 = ok0;
    *(volatile unsigned char *)0x3F01 = ok1;
    *(volatile unsigned char *)0x3F02 = guard0_before;
    *(volatile unsigned char *)0x3F03 = guard0_after;
    *(volatile unsigned char *)0x3F04 = guard1_before;
    *(volatile unsigned char *)0x3F05 = guard1_after;
    *(volatile unsigned char *)0x3F06 = 0xA5;

    VIC.ctrl1 &= ~0x20;
    VIC.addr = 0x14;
    CIA2.pra = (CIA2.pra & 0xFC) | 0x03;

    clrscr();
    cprintf("buf0 all-zero: %s\r\n", ok0 ? "YES" : "NO");
    cprintf("buf1 all-zero: %s\r\n", ok1 ? "YES" : "NO");
    cprintf("guard bytes (should be AA AA AA AA): %02x %02x %02x %02x\r\n",
            guard0_before, guard0_after, guard1_before, guard1_after);

    while (1) {
    }

    return 0;
}
