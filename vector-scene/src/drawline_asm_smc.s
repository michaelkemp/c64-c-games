; Trick #2b: same as drawline_asm.s, but the pixel toggle uses
; self-modified absolute addressing instead of a zero-page pointer with
; indirect-Y. This is the actual Elite technique the ChatGPT conversation
; (see README.md) described: patch the operand bytes of a fixed
; `LDA $nnnn` / `STA $nnnn` pair whenever the touched address changes,
; so the hot per-pixel access is `LDA abs` (4 cycles) + `STA abs`
; (4 cycles) instead of `LDA (zp),Y` (5) + `STA (zp),Y` (6) -- 5 cycles
; cheaper per pixel, at the cost of a slightly larger patch (4 bytes:
; both instructions' operands) whenever the address actually changes,
; versus updating one zero-page pointer (2 bytes) in drawline_asm.s.
; Since address changes only happen on x-byte-crossings and y-cell-row
; wraps (not every pixel), this should be a net win -- see README.md for
; the measured result.
;
; Imports dla_x0/y0/x1/y1/dla_bitmap_base from drawline_asm.s rather
; than redefining them: both modules share the same C-facing calling
; convention (same restriction: x1 >= x0), so a caller that wants to
; compare them side by side sets the same four globals either way.
; Everything else (dx/dy/err/e2/mask/...) is a fresh, this-module-only
; copy -- ca65/ld65 only resolve symbols that are exported/imported, so
; reusing the exact same internal names as drawline_asm.s in this
; separate module doesn't collide.

.export _draw_line_smc
.import _dla_x0
.import _dla_y0
.import _dla_x1
.import _dla_y1
.import _dla_bitmap_base

.segment "ZEROPAGE"

dla_dx:      .res 2
dla_dy:      .res 2
dla_sy:      .res 1
dla_sy_hi:   .res 1

dla_err:     .res 2
dla_e2:      .res 2

dla_mask:    .res 1

dla_ystep:   .res 2
dla_ycross:  .res 2
dla_ywrap:   .res 1

dla_tmp:     .res 2
dla_scratch: .res 2

.rodata

dla_row_offset:
    .word 0*320,  1*320,  2*320,  3*320,  4*320
    .word 5*320,  6*320,  7*320,  8*320,  9*320
    .word 10*320, 11*320, 12*320, 13*320, 14*320
    .word 15*320, 16*320, 17*320, 18*320, 19*320
    .word 20*320, 21*320, 22*320, 23*320, 24*320

dla_mask_table:
    .byte $80, $40, $20, $10, $08, $04, $02, $01

.code

.proc _draw_line_smc: near

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

    ; ---- addr = dla_bitmap_base + ROW_OFFSET[y0>>3] + (x0 & ~7) + (y0 & 7) ----
    ; Computed into dla_tmp first (a plain zp scratch pair), then
    ; copied into the self-modified LDA/STA operands once, below --
    ; simpler than patching after every partial add.
    lda _dla_y0
    lsr a
    lsr a
    lsr a
    asl a                   ; *2 for word-table index
    tax
    lda dla_row_offset,x
    clc
    adc _dla_bitmap_base
    sta dla_tmp
    lda dla_row_offset+1,x
    adc _dla_bitmap_base+1
    sta dla_tmp+1

    lda _dla_x0
    and #$F8
    clc
    adc dla_tmp
    sta dla_tmp
    lda _dla_x0+1
    adc dla_tmp+1
    sta dla_tmp+1

    lda _dla_y0
    and #7
    clc
    adc dla_tmp
    sta dla_tmp
    lda #0
    adc dla_tmp+1
    sta dla_tmp+1

    ; ---- patch both instructions' operands to the initial address ----
    lda dla_tmp
    sta toggle_lda+1
    sta toggle_sta+1
    lda dla_tmp+1
    sta toggle_lda+2
    sta toggle_sta+2

    ; ---- mask = mask_table[x0 & 7] ----
    lda _dla_x0
    and #7
    tax
    lda dla_mask_table,x
    sta dla_mask

pixel_loop:
toggle_lda:
    lda a:$0000       ; operand patched below; `a:` forces 3-byte absolute encoding
    eor dla_mask
toggle_sta:
    sta a:$0000       ; operand patched below

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

    ; if (e2 >= dy) { err += dy; mask >>= 1 (wrap: mask=$80, addr+=8); x0++ }
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
    lda toggle_lda+1
    adc #8
    sta toggle_lda+1
    sta toggle_sta+1
    bcc xstep_nowrap
    inc toggle_lda+2
    inc toggle_sta+2
xstep_nowrap:
    inc _dla_x0
    bne skip_xstep
    inc _dla_x0+1

skip_xstep:
    ; if (e2 <= dx) { err += dx; addr += (y0&7==ywrap)?ycross:ystep; y0 += sy }
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
    lda toggle_lda+1
    adc dla_ycross
    sta toggle_lda+1
    sta toggle_sta+1
    lda toggle_lda+2
    adc dla_ycross+1
    sta toggle_lda+2
    sta toggle_sta+2
    jmp y_ptr_done

y_no_wrap:
    clc
    lda toggle_lda+1
    adc dla_ystep
    sta toggle_lda+1
    sta toggle_sta+1
    lda toggle_lda+2
    adc dla_ystep+1
    sta toggle_lda+2
    sta toggle_sta+2

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
