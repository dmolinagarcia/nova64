; con.s — the console's output backend: Mode 0 on Neon (CN1.9, CN1.10, T1.55).
;
; Neon is a cell engine with a cursor and a scroll; every control character is
; interpreted here (D105). The mechanism is the BIOS's -- the same stores to
; the same addresses -- and the authoritative cursor is kept in software.
;
; The backend also holds its own copy of the ring in system memory and can
; repaint from it, because a graphics mode may reclaim the text buffer's block
; RAM (J.4, Q45). Nothing switches modes yet; the copy is kept from the start
; because it cannot be retrofitted.

SHADOW = (KFAR_BANK << 16) | $C000      ; 4096 cells x 2 bytes

.segment "BSS"
con_cols:  .res 2
con_rows:  .res 2
con_row:   .res 2
con_col:   .res 2
con_start: .res 2               ; ring index shown at the top-left corner
con_cell:  .res 2               ; byte offset of the cursor's cell in the ring
.segment "CODE"

.proc con_init
        lda f:SIB+SIB_COLS      ; geometry from the information block, never constants (CN1.4)
        beq @defc
        cmp #TEXT_COLS+1
        bcc @cols
@defc:  lda #TEXT_COLS
@cols:  sta con_cols
        lda f:SIB+SIB_ROWS
        beq @defr
        cmp #TEXT_ROWS+1
        bcc @rows
@defr:  lda #TEXT_ROWS
@rows:  sta con_rows

        A8                      ; carry on where the BIOS left the cursor
        lda f:NEON_TEXT_START
        sta con_start
        lda f:NEON_TEXT_START+1
        sta con_start+1
        lda f:NEON_CURSOR_POS
        sta r0
        lda f:NEON_CURSOR_POS+1
        sta r0+1
        lda #$10                ; a visible cursor
        sta f:NEON_CURSOR_CTL
        A16
        lda r0
        sec
        sbc con_start
        and #$0FFF
        pha
        and #TEXT_COLS-1
        sta con_col
        pla
        asl a
        xba
        and #$001F
        sta con_row

        ldx #0                  ; the scrollback copy starts as what is on the screen
@copy:  lda f:NEON_TEXT,x
        sta f:SHADOW,x
        inx
        inx
        cpx #TEXT_CELLS*2
        bne @copy

        lda con_row
        cmp con_rows
        bcs @clear
        lda con_col
        cmp con_cols
        bcc con_locate
@clear: jmp con_clear
.endproc

; recompute con_cell from row and column
.proc con_locate
        lda con_row
        xba
        lsr a                   ; row * 128: the ring's stride, whatever the visible width
        clc
        adc con_col
        clc
        adc con_start
        and #$0FFF
        asl a
        sta con_cell
        rts
.endproc

; move the hardware cursor to where the software one is
.proc con_cursor
        lda con_cell
        lsr a
        A8
        sta f:NEON_CURSOR_POS
        xba
        sta f:NEON_CURSOR_POS+1 ; latches on the high byte (T1.54)
        A16
        rts
.endproc

; A = character. The printable path is the short one (CN1.10).
.proc con_putc
        and #$00FF
        cmp #$0020
        bcc @ctrl
        ora #TEXT_ATTR << 8
        ldx con_cell
        sta f:NEON_TEXT,x
        sta f:SHADOW,x
        inx
        inx
        txa
        and #$1FFF
        sta con_cell
        inc con_col
        lda con_col
        cmp con_cols
        bcs con_newline         ; output wraps at the last column
        rts
@ctrl:  cmp #$000A
        beq con_newline
        cmp #$000D
        beq @cr
        cmp #$0008
        beq @bs
        cmp #$0009
        beq @tab
        rts                     ; BEL, ESC and the rest: ignored
@cr:    stz con_col
        jmp con_locate
@bs:    lda con_col             ; back one column, without erasing
        beq @bsrow
        dec con_col
        jmp con_locate
@bsrow: lda con_row
        beq @bsend
        dec con_row
        lda con_cols
        dec a
        sta con_col
        jmp con_locate
@bsend: rts
@tab:   lda con_col             ; the next multiple of 8
        clc
        adc #8
        and #$FFF8
        sta con_col
        cmp con_cols
        bcs con_newline
        jmp con_locate
.endproc

; column 0 of the next row; scroll at the bottom row
.proc con_newline
        stz con_col
        inc con_row
        lda con_row
        cmp con_rows
        bcs @scroll
        jmp con_locate
@scroll:
        dec con_row
        lda con_start           ; scrolling is a pointer move (T1.27)
        clc
        adc #TEXT_COLS
        and #$0FFF
        sta con_start
        A8
        sta f:NEON_TEXT_START
        xba
        sta f:NEON_TEXT_START+1
        A16
        jsr con_locate
        ldx con_cell            ; clear the row newly exposed at the bottom
        ldy #TEXT_COLS
@clr:   lda #(TEXT_ATTR << 8) | $20
        sta f:NEON_TEXT,x
        sta f:SHADOW,x
        inx
        inx
        txa
        and #$1FFF
        tax
        dey
        bne @clr
        rts
.endproc

; clear the screen and home the cursor (TTY_CLEAR)
.proc con_clear
        ldx #0
        lda #(TEXT_ATTR << 8) | $20
@fill:  sta f:NEON_TEXT,x
        sta f:SHADOW,x
        inx
        inx
        cpx #TEXT_CELLS*2
        bne @fill
        stz con_start
        stz con_row
        stz con_col
        A8
        lda #0
        sta f:NEON_TEXT_START
        sta f:NEON_TEXT_START+1
        A16
        jsr con_locate
        jmp con_cursor
.endproc

; put the whole ring back from the scrollback copy, after something else has
; had the text buffer's memory
.proc con_repaint
        ldx #0
@copy:  lda f:SHADOW,x
        sta f:NEON_TEXT,x
        inx
        inx
        cpx #TEXT_CELLS*2
        bne @copy
        A8
        lda con_start
        sta f:NEON_TEXT_START
        lda con_start+1
        sta f:NEON_TEXT_START+1
        A16
        jmp con_cursor
.endproc

; ---- kernel printing: works from con_init onward ------------------------------
.proc kputc
        jsr con_putc
        jmp con_cursor
.endproc

.proc kputs                     ; X = string
@next:  lda a:0,x
        and #$00FF
        beq @done
        phx
        jsr con_putc
        plx
        inx
        bra @next
@done:  jmp con_cursor
.endproc

.proc kputu                     ; A = unsigned 16-bit, decimal
        sta r0
        stz r1
        jsr u32toa
        jmp kputs
.endproc

.proc kputx                     ; A = 16 bits, four hex digits
        sta r3
        ldy #4
@nib:   lda r3
        xba
        lsr a
        lsr a
        lsr a
        lsr a
        and #$000F
        tax
        lda hexdig,x
        phy
        jsr con_putc
        ply
        asl r3
        asl r3
        asl r3
        asl r3
        dey
        bne @nib
        jmp con_cursor
.endproc
hexdig: .byte "0123456789ABCDEF"

; X = message. Print on Neon, disable interrupts, halt (CN1.8). The post-mortem
; is the Debug Agent's; the kernel carries no debugger.
.proc panic
        sei
        phx
        lda con_col
        beq @line
        lda #$0A
        jsr con_putc
@line:  ldx #.loword(@lead)
        jsr kputs
        plx
        jsr kputs
        lda #$0A
        jsr kputc
        stp
@lead:  .byte "panic: ", 0
.endproc
