; lib.s — string and number helpers with no kernel state in them. The shell
; uses the same routines: they are the part of libnova this build needs.

.segment "BSS"
numbuf:  .res 12
.segment "CODE"

; X = string -> A = length. X preserved.
.proc strlen
        phx
        ldy #0
        A8
@next:  lda a:0,x
        beq @done
        inx
        iny
        bra @next
@done:  A16
        tya
        plx
        rts
.endproc

; X, Y = strings -> Z set if equal
.proc strcmp
        stx p0
        sty p1
        ldy #0
        A8
@next:  lda (p0),y
        cmp (p1),y
        bne @out
        cmp #0
        beq @out
        iny
        bra @next
@out:   A16
        rts
.endproc

; copy the string at X to Y, terminator included
.proc strcpy
        stx p0
        sty p1
        ldy #0
        A8
@next:  lda (p0),y
        sta (p1),y
        beq @done
        iny
        bra @next
@done:  A16
        rts
.endproc

; r0:r1 / 10 -> quotient in r0:r1, remainder in A
.proc div10
        stz r2
        ldx #32
@bit:   asl r0
        rol r1
        rol r2
        lda r2
        cmp #10
        bcc @next
        sbc #10
        sta r2
        inc r0
@next:  dex
        bne @bit
        lda r2
        rts
.endproc

; r0:r1 -> X = decimal string in numbuf
.proc u32toa
        ldy #11
        A8
        lda #0
        sta numbuf,y
        A16
@digit: phy
        jsr div10
        ply
        dey
        A8
        ora #'0'
        sta numbuf,y
        A16
        lda r0
        ora r1
        bne @digit
        tya
        clc
        adc #.loword(numbuf)
        tax
        rts
.endproc
