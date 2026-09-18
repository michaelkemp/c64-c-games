#ifndef DRAWLINE_ASM_H
#define DRAWLINE_ASM_H

/* Trick #2: hand-written 6502 line draw (src/drawline_asm.s). Set
   dla_x0/y0/x1/y1 then call draw_line_asm() -- parameters are passed
   through these globals rather than cc65's normal call convention (see
   drawline_asm.s for why). Restriction: dla_x1 >= dla_x0; swap
   endpoints yourself first if that doesn't already hold.

   dla_bitmap_base selects which bitmap to draw into (e.g. $6000 for
   the single hires-bounce-style buffer, or whichever VIC bank a
   double-buffered caller is currently drawing into) -- set it at least
   once before the first call; single-buffer callers only need to set
   it once, ever. */

extern int dla_x0, dla_y0, dla_x1, dla_y1;
extern unsigned char *dla_bitmap_base;

void draw_line_asm(void);

/* Trick #2b (src/drawline_asm_smc.s): identical interface and
   restriction, self-modified absolute addressing for the pixel toggle
   instead of a zero-page indirect pointer. See its header comment. */
void draw_line_smc(void);

#endif
