#ifndef CLEARBITMAP_ASM_H
#define CLEARBITMAP_ASM_H

/* Trick #4 (src/clearbitmap_asm.s): unrolled full-bitmap clear, one
   hardcoded routine per double-buffer target. Matches
   doublebuf.h's BUF_BITMAP[0]/[1] (buffer 0 = $6000, buffer 1 =
   $A000). */

void clear_bitmap_buf0(void);
void clear_bitmap_buf1(void);

#endif
