; dev.s — the device table (J.4, CN1.15).
;
; A driver is five functions -- init, read, write, ioctl, irq -- reached
; through this table by device number. open() resolves /dev/<name> against it
; before the path reaches the VFS (D108). Only the console is registered: the
; raw card is not exposed, and there is no devfs, so /dev cannot be listed.

DEV_NAME  = 0                   ; near pointer to the name
DEV_INIT  = 2
DEV_READ  = 4                   ; A = buffer, X = count
DEV_WRITE = 6
DEV_IOCTL = 8                   ; A = operation, X = argument
DEV_IRQ   = 10
DEV_SIZEOF = 12

devtab:
        .word devname_tty, tty_init, tty_read, tty_write, tty_ioctl, tty_irq
devtab_end:
NDEV = (devtab_end - devtab) / DEV_SIZEOF
devname_tty: .byte "tty", 0

; X = name -> A = device number, or E_NOENT
.proc dev_find
        stx r5
        stz r6
@next:  lda r6
        cmp #NDEV
        bcs @none
        jsr dev_slot
        ldy a:devtab+DEV_NAME,x
        ldx r5
        jsr strcmp
        beq @found
        inc r6
        bra @next
@found: lda r6
        rts
@none:  lda #E_NOENT
        rts
.endproc

; A = device number -> X = offset of its entry
.proc dev_slot
        asl a
        asl a
        sta r7
        asl a
        clc
        adc r7                  ; * 12
        tax
        rts
.endproc

; call function Y (a DEV_* offset) of device r6 with A and X as its arguments
.proc dev_call
        pha
        tya
        clc
        adc #.loword(devtab)
        sta r7
        lda r6
        asl a
        asl a
        clc
        adc r7
        sta r7
        lda r6
        asl a
        asl a
        asl a
        clc
        adc r7
        sta r7                  ; devtab + 12 * device + function
        lda (r7)
        sta jv
        pla
        jmp (jv)                ; the vector is in the direct page, which is where JMP (abs) looks
.endproc
