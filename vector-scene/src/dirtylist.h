#ifndef DIRTYLIST_H
#define DIRTYLIST_H

/* Trick #1: dirty-list erase. draw_line_dirty() does exactly what
   draw_line() does (same Bresenham walk, same XOR toggle) but also
   records each touched byte's address and bit mask. erase_dirty() then
   undoes it by replaying that list -- a flat loop with no error-term
   arithmetic and no mask/address-stepping decisions -- instead of
   re-running the whole branchy Bresenham walk a second time with
   identical endpoints, which is what hires-bounce's "draw the same line
   twice" erase trick actually does today. See README.md for the
   measured win.

   320 entries covers the longest possible on-screen line: a line's
   pixel count is max(dx,|dy|)+1, and the largest either can be on a
   320x200 bitmap is 319. */

#define MAX_DIRTY 320

typedef struct {
    unsigned char *addr[MAX_DIRTY];
    unsigned char mask[MAX_DIRTY];
    unsigned int count;
} DirtyList;

void draw_line_dirty(int x0, int y0, int x1, int y1, DirtyList *dirty);
void erase_dirty(const DirtyList *dirty);

#endif
