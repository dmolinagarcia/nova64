; clock.s — the fixed-frequency counter every timeout uses (L.15).
; Helium's counter runs on its own clock; nothing here counts CPU cycles for
; time (D92). Reads have no side effects, so a consistent 32-bit value is read
; high, low, high again.

.segment "BSS"
now:    .res 4
.segment "CODE"

.proc clock_read                ; -> now
@again: lda f:TMR_MS_H
        sta now+2
        lda f:TMR_MS_L
        sta now
        lda f:TMR_MS_H
        cmp now+2
        bne @again
        rts
.endproc

; the counter must be seen to advance; the bound is on attempts, not on time
.proc clock_init
        lda f:TMR_MS_L
        sta r0
        ldx #0
        ldy #8
@wait:  lda f:TMR_MS_L
        cmp r0
        bne @ok
        dex
        bne @wait
        dey
        bne @wait
        ldx #.loword(@dead)
        jmp panic
@ok:    rts
@dead:  .byte "clock: Helium's counter is not running", 0
.endproc
