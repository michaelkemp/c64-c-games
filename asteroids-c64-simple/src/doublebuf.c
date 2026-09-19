#include <c64.h>
#include <string.h>

#include "doublebuf.h" /* also defines SCREEN_W, SCREEN_H */

#define CPU_PORT (*(volatile unsigned char *)0x0001)

unsigned char * const BUF_SCREEN[2] = { BUF_SCREEN_0, BUF_SCREEN_1 };
unsigned char * const BUF_BITMAP[2] = { BUF_BITMAP_0, BUF_BITMAP_1 };

/* CIA2.pra bits 0-1 select the VIC bank, inverted: 11=bank0, 10=bank1,
   01=bank2, 00=bank3 (see hires-bounce/README.md's register table).
   Buffer 0 -> bank 1 -> 10 = 0x02. Buffer 1 -> bank 2 -> 01 = 0x01. */
static const unsigned char BANK_BITS[2] = { 0x02, 0x01 };

void doublebuf_init(void)
{
    /* Bank out BASIC ROM ($A000-$BFFF) so CPU reads of buffer 1's
       bitmap see real RAM, not ROM -- draw_line_asm()'s pixel toggle
       reads-modifies-writes, so this matters even though writes alone
       would have reached RAM either way. KERNAL (HIRAM, $E000-$FFFF)
       stays mapped in -- cprintf()/clrscr() still work. */
    CPU_PORT &= ~0x01;

    memset(BUF_BITMAP[0], 0x00, (SCREEN_W / 8) * SCREEN_H);
    memset(BUF_BITMAP[1], 0x00, (SCREEN_W / 8) * SCREEN_H);
    memset(BUF_SCREEN[0], 0x10, 1000);
    memset(BUF_SCREEN[1], 0x10, 1000);

    VIC.ctrl2 &= ~0x10; /* MCM=0: plain (non-multicolor) hi-res */
    VIC.ctrl1 |= 0x20;  /* BMM=1: bitmap mode on */

    doublebuf_show(0);
}

void doublebuf_show(unsigned char which)
{
    CIA2.pra = (CIA2.pra & 0xFC) | BANK_BITS[which];
    VIC.addr = 0x08; /* screen @ bank+0, bitmap @ bank+$2000 -- same for both banks */
}
