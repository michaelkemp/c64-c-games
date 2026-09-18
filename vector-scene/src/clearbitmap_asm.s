; Trick #4: fast unrolled full-bitmap clear -- the "list of STA calls"
; idea this whole folder started from, and the classic C64 technique
; for it: STA absolute,X is 5 cycles regardless of which page it
; targets, so one 256-iteration X loop with every page's STA unrolled
; inside it clears many pages at once far faster than a per-byte or
; per-page loop would. See README.md's "Trick #4" section for the
; measured result against erasing only the touched pixels.
;
; BITMAP is exactly 8000 bytes (320/8 * 200) = 31 full 256-byte pages
; (7936 bytes) plus a 64-byte remainder -- handled as a second, shorter
; unrolled loop, same technique. Two hardcoded copies (buffer 0 at
; $6000, buffer 1 at $A000) rather than one parameterized-by-runtime-
; patched-page-byte routine: with only two fixed double-buffer targets
; that never change, hardcoding avoids any patching cost at all, not
; just a cheap one.

.export _clear_bitmap_buf0
.export _clear_bitmap_buf1

.code

.proc _clear_bitmap_buf0: near
    lda #$00
    ldx #$00
loop0:
    sta $6000,x
    sta $6100,x
    sta $6200,x
    sta $6300,x
    sta $6400,x
    sta $6500,x
    sta $6600,x
    sta $6700,x
    sta $6800,x
    sta $6900,x
    sta $6A00,x
    sta $6B00,x
    sta $6C00,x
    sta $6D00,x
    sta $6E00,x
    sta $6F00,x
    sta $7000,x
    sta $7100,x
    sta $7200,x
    sta $7300,x
    sta $7400,x
    sta $7500,x
    sta $7600,x
    sta $7700,x
    sta $7800,x
    sta $7900,x
    sta $7A00,x
    sta $7B00,x
    sta $7C00,x
    sta $7D00,x
    sta $7E00,x
    inx
    bne loop0

    ldx #$00
rem0:
    sta $7F00,x
    inx
    cpx #$40
    bne rem0

    rts
.endproc

.proc _clear_bitmap_buf1: near
    lda #$00
    ldx #$00
loop1:
    sta $A000,x
    sta $A100,x
    sta $A200,x
    sta $A300,x
    sta $A400,x
    sta $A500,x
    sta $A600,x
    sta $A700,x
    sta $A800,x
    sta $A900,x
    sta $AA00,x
    sta $AB00,x
    sta $AC00,x
    sta $AD00,x
    sta $AE00,x
    sta $AF00,x
    sta $B000,x
    sta $B100,x
    sta $B200,x
    sta $B300,x
    sta $B400,x
    sta $B500,x
    sta $B600,x
    sta $B700,x
    sta $B800,x
    sta $B900,x
    sta $BA00,x
    sta $BB00,x
    sta $BC00,x
    sta $BD00,x
    sta $BE00,x
    inx
    bne loop1

    ldx #$00
rem1:
    sta $BF00,x
    inx
    cpx #$40
    bne rem1

    rts
.endproc
