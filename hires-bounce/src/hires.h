#ifndef HIRES_H
#define HIRES_H

/* Shared VIC-II hi-res bitmap setup + line drawing. See README.md for
   the register layout, the XOR erase/redraw trick, and why draw_line()
   is written the way it is. Used by both main.c (the bouncing-line demo)
   and tests/bench_line.c (the draw-time benchmark) so there's exactly
   one implementation of the line routine being demonstrated/measured. */

#define SCREEN_W 320
#define SCREEN_H 200

/* VIC bank 1 ($4000-$7FFF): screen matrix at bank+$0000, bitmap at
   bank+$2000. */
extern unsigned char * const SCREEN_MATRIX;
extern unsigned char * const BITMAP;

void hires_on(void);
void draw_line(int x0, int y0, int x1, int y1);

#endif
