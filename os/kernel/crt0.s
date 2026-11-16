; crt0.s — the kernel's entry (CN1 "Kernel initialisation", step 1).
; The BIOS jumps here with nothing in any register worth keeping.

.import __BSS_RUN__, __BSS_SIZE__

.proc kernel_entry
        sei
        clc
        xce
        rep #$38                ; native, 16-bit A/X/Y, decimal off
        ldx #STACK_TOP
        txs
        lda #$0000
        tcd
        phk
        plb                     ; DBR = the kernel's bank

        ldx #.loword(__BSS_RUN__)
        ldy #.loword(__BSS_SIZE__)
        beq @vec
        A8
@bss:   stz a:0,x
        inx
        dey
        bne @bss
        A16

@vec:   ; every trap the monolithic build can take is a panic (CN1.8)
        ldy #.loword(trap_cop)
        ldx #RAMVEC_COP
        jsr @set
        ldy #.loword(trap_brk)
        ldx #RAMVEC_BRK
        jsr @set
        ldy #.loword(trap_abort)
        ldx #RAMVEC_ABORT
        jsr @set
        ldy #.loword(trap_nmi)
        ldx #RAMVEC_NMI
        jsr @set
        ldy #.loword(trap_irq)
        ldx #RAMVEC_IRQ
        jsr @set
        jmp kmain

@set:   tya                     ; a far pointer into the kernel's bank, at $00:X
        sta f:$000000,x
        lda #KBANK
        sta f:$000002,x
        rts
.endproc

; A trap arrives from the BIOS vector stub in whatever state the CPU was in.
.macro TRAP name, text
.proc name
        rep #$38
        .a16
        .i16
        lda #$0000
        tcd
        phk
        plb
        ldx #.loword(@msg)
        jmp panic
@msg:   .byte text, 0
.endproc
.endmacro

TRAP trap_cop,   "unexpected COP: the monolithic build has no system-call gate"
TRAP trap_brk,   "BRK"
TRAP trap_abort, "ABORT with the MMU off"
TRAP trap_nmi,   "NMI"
TRAP trap_irq,   "IRQ: the monolithic build enables none"
