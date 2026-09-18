#ifndef HIRES_H
#define HIRES_H

/* Baseline hi-res bitmap setup + line drawing, copied unmodified from
   hires-bounce/src/hires.{c,h}. This is the control every trick in this
   folder gets measured against -- see README.md. */

#define SCREEN_W 320
#define SCREEN_H 200

/* VIC bank 1 ($4000-$7FFF): screen matrix at bank+$0000, bitmap at
   bank+$2000. */
extern unsigned char * const SCREEN_MATRIX;
extern unsigned char * const BITMAP;

void hires_on(void);
void draw_line(int x0, int y0, int x1, int y1);

#endif
