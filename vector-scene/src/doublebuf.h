#ifndef DOUBLEBUF_H
#define DOUBLEBUF_H

/* Trick #3: real double buffering across two VIC-II banks, instead of
   XOR-erase-then-redraw on one buffer. Buffer 0 is VIC bank 1
   ($4000-$7FFF, screen@$4000, bitmap@$6000 -- the same layout
   hires.c/hires_on() has always used). Buffer 1 is VIC bank 2
   ($8000-$BFFF, screen@$8000, bitmap@$A000).

   $A000-$BFFF is BASIC ROM from the CPU's side by default. CPU WRITES
   always reach the real RAM underneath regardless (ROM only affects
   what a READ sees) -- but draw_line_asm()'s pixel toggle is a
   read-modify-write (it XORs against the existing byte), so it needs
   READS to see real RAM too, or every toggle XORs against whatever
   BASIC ROM happens to contain instead of the actual bitmap content.
   doublebuf_init() clears the CPU port's LORAM bit once to fix this;
   see doublebuf.c. */

#define BUF_SCREEN_0 ((unsigned char *)0x4000)
#define BUF_BITMAP_0 ((unsigned char *)0x6000)
#define BUF_SCREEN_1 ((unsigned char *)0x8000)
#define BUF_BITMAP_1 ((unsigned char *)0xA000)

extern unsigned char * const BUF_SCREEN[2];
extern unsigned char * const BUF_BITMAP[2];

/* Clears both bitmaps and screen matrices, bank out BASIC ROM so
   buffer 1's bitmap reads correctly, and turns on hi-res mode
   displaying buffer 0. Call once at startup. */
void doublebuf_init(void);

/* Switches the VIC to display buffer `which` (0 or 1). Cheap (a couple
   of register writes) -- call during vertical blank to avoid a
   mid-frame tear. */
void doublebuf_show(unsigned char which);

#endif
