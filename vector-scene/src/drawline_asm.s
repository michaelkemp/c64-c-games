; Trick #2: hand-written 6502, not cc65-compiled C.
;
; Same Bresenham algorithm as hires.c's draw_line(), restricted to
; x1 >= x0 (the caller swaps endpoints first if needed -- a one-time,
; not-per-pixel cost). Parameters are passed through plain C-visible
; globals (dla_x0/y0/x1/y1) instead of cc65's normal software-stack
; calling convention, and all hot state (ptr/mask/err/e2/dx/dy/...)
; lives in zero page, addressed directly -- no cc65 runtime helper calls
; for 16-bit compares/moves anywhere in the inner loop. See README.md
; for why this exists: cc65-compiled draw_line() measured at ~730
; cycles/pixel, and the hypothesis (backed by the ChatGPT conversation
; saved as a PDF) is that nearly all of that is cc65's generic C-level
; overhead for 16-bit arithmetic/comparisons, not the algorithm itself.
;
; The pixel toggle itself uses a zero-page pointer with indirect-Y
; (Y always 0), not self-modified absolute addressing -- one cycle
; slower per access (5 vs 4 cycles) but far simpler/safer to get right
; first. Upgrading the toggle to self-modified absolute is a small,
; isolated follow-up once this is verified correct and benchmarked.

.export _draw_line_asm
.export _dla_x0
.export _dla_y0
.export _dla_x1
.export _dla_y1
.export _dla_bitmap_base

.segment "ZEROPAGE"

_dla_x0:     .res 2
_dla_y0:     .res 2
_dla_x1:     .res 2
_dla_y1:     .res 2

; Base address of the bitmap to draw into -- $6000 for the single
; buffer everything before double-buffering used, but the whole point
; of a second VIC bank is that the CPU-visible bitmap address changes
; too. Callers using only one buffer set this once, before the first
; call; the double-buffered demo sets it per frame to whichever buffer
; is currently the invisible one. Low byte first (matches how a plain
; C `unsigned char *` is stored).
_dla_bitmap_base: .res 2

dla_dx:      .res 2
dla_dy:      .res 2
dla_sy:      .res 1
dla_sy_hi:   .res 1

dla_err:     .res 2
dla_e2:      .res 2

dla_ptr:     .res 2
dla_mask:    .res 1

dla_ystep:   .res 2
dla_ycross:  .res 2
dla_ywrap:   .res 1

dla_tmp:     .res 2
dla_scratch: .res 2

.rodata

; Same row*320 table as hires.c/dirtylist.c's ROW_OFFSET, duplicated
; here for the same reason dirtylist.c duplicates it: this module isn't
; a thin wrapper, it's a full alternate implementation being measured
; against the others.
dla_row_offset:
    .word 0*320,  1*320,  2*320,  3*320,  4*320
    .word 5*320,  6*320,  7*320,  8*320,  9*320
    .word 10*320, 11*320, 12*320, 13*320, 14*320
    .word 15*320, 16*320, 17*320, 18*320, 19*320
    .word 20*320, 21*320, 22*320, 23*320, 24*320

dla_mask_table:
    .byte $80, $40, $20, $10, $08, $04, $02, $01

.code

.proc _draw_line_asm: near

    ; ---- dx = x1 - x0 (non-negative: caller guarantees x1 >= x0) ----
    sec
    lda _dla_x1
    sbc _dla_x0
    sta dla_dx
    lda _dla_x1+1
    sbc _dla_x0+1
    sta dla_dx+1

    ; ---- diff = y1 - y0 (16-bit, may be negative) ----
    sec
    lda _dla_y1
    sbc _dla_y0
    sta dla_tmp
    lda _dla_y1+1
    sbc _dla_y0+1
    sta dla_tmp+1
    bmi diff_neg

    ; diff >= 0: sy = +1, dy = -diff
    lda #1
    sta dla_sy
    lda #0
    sta dla_sy_hi
    sec
    lda #0
    sbc dla_tmp
    sta dla_dy
    lda #0
    sbc dla_tmp+1
    sta dla_dy+1
    jmp diff_done

diff_neg:
    lda #$FF
    sta dla_sy
    sta dla_sy_hi
    lda dla_tmp
    sta dla_dy
    lda dla_tmp+1
    sta dla_dy+1

diff_done:

    ; ---- err = dx + dy ----
    clc
    lda dla_dx
    adc dla_dy
    sta dla_err
    lda dla_dx+1
    adc dla_dy+1
    sta dla_err+1

    ; ---- y-step constants, depending on sign of sy ----
    lda dla_sy
    bmi sy_neg

    lda #1
    sta dla_ystep
    lda #0
    sta dla_ystep+1
    lda #<313
    sta dla_ycross
    lda #>313
    sta dla_ycross+1
    lda #7
    sta dla_ywrap
    jmp sy_setup_done

sy_neg:
    lda #<-1
    sta dla_ystep
    lda #>-1
    sta dla_ystep+1
    lda #<-313
    sta dla_ycross
    lda #>-313
    sta dla_ycross+1
    lda #0
    sta dla_ywrap

sy_setup_done:

    ; ---- ptr = dla_bitmap_base + ROW_OFFSET[y0>>3] + (x0 & ~7) + (y0 & 7) ----
    lda _dla_y0
    lsr a
    lsr a
    lsr a
    asl a                   ; *2 for word-table index
    tax
    lda dla_row_offset,x
    clc
    adc _dla_bitmap_base
    sta dla_ptr
    lda dla_row_offset+1,x
    adc _dla_bitmap_base+1
    sta dla_ptr+1

    lda _dla_x0
    and #$F8
    clc
    adc dla_ptr
    sta dla_ptr
    lda _dla_x0+1
    adc dla_ptr+1
    sta dla_ptr+1

    lda _dla_y0
    and #7
    clc
    adc dla_ptr
    sta dla_ptr
    lda #0
    adc dla_ptr+1
    sta dla_ptr+1

    ; ---- mask = mask_table[x0 & 7] ----
    lda _dla_x0
    and #7
    tax
    lda dla_mask_table,x
    sta dla_mask

pixel_loop:
    ldy #0
    lda (dla_ptr),y
    eor dla_mask
    sta (dla_ptr),y

    ; if (x0 == x1 && y0 == y1) return
    lda _dla_x0
    cmp _dla_x1
    bne not_done
    lda _dla_x0+1
    cmp _dla_x1+1
    bne not_done
    lda _dla_y0
    cmp _dla_y1
    bne not_done
    lda _dla_y0+1
    cmp _dla_y1+1
    bne not_done
    jmp done

not_done:
    ; e2 = 2 * err
    lda dla_err
    asl a
    sta dla_e2
    lda dla_err+1
    rol a
    sta dla_e2+1

    ; if (e2 >= dy) { err += dy; mask >>= 1 (wrap: mask=$80, ptr+=8); x0++ }
    sec
    lda dla_e2
    sbc dla_dy
    sta dla_scratch
    lda dla_e2+1
    sbc dla_dy+1
    bmi skip_xstep

    clc
    lda dla_err
    adc dla_dy
    sta dla_err
    lda dla_err+1
    adc dla_dy+1
    sta dla_err+1

    lsr dla_mask
    bne xstep_nowrap
    lda #$80
    sta dla_mask
    clc
    lda dla_ptr
    adc #8
    sta dla_ptr
    bcc xstep_nowrap
    inc dla_ptr+1
xstep_nowrap:
    inc _dla_x0
    bne skip_xstep
    inc _dla_x0+1

skip_xstep:
    ; if (e2 <= dx) { err += dx; ptr += (y0&7==ywrap)?ycross:ystep; y0 += sy }
    sec
    lda dla_e2
    sbc dla_dx
    sta dla_scratch
    lda dla_e2+1
    sbc dla_dx+1
    sta dla_scratch+1
    bmi do_ystep
    ora dla_scratch
    bne pixel_loop_continue
    ; falls through when e2 - dx == 0

do_ystep:
    clc
    lda dla_err
    adc dla_dx
    sta dla_err
    lda dla_err+1
    adc dla_dx+1
    sta dla_err+1

    lda _dla_y0
    and #7
    cmp dla_ywrap
    bne y_no_wrap

    clc
    lda dla_ptr
    adc dla_ycross
    sta dla_ptr
    lda dla_ptr+1
    adc dla_ycross+1
    sta dla_ptr+1
    jmp y_ptr_done

y_no_wrap:
    clc
    lda dla_ptr
    adc dla_ystep
    sta dla_ptr
    lda dla_ptr+1
    adc dla_ystep+1
    sta dla_ptr+1

y_ptr_done:
    clc
    lda _dla_y0
    adc dla_sy
    sta _dla_y0
    lda _dla_y0+1
    adc dla_sy_hi
    sta _dla_y0+1

pixel_loop_continue:
    jmp pixel_loop

done:
    rts

.endproc
