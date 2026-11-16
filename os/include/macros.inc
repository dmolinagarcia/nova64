; macros.inc — the conventions every source file in this tree shares.
;
; The CPU is in native mode with A, X and Y 16 bits wide at every routine
; boundary. A routine that needs an 8-bit accumulator brackets the stretch
; with A8 / A16; the index registers never narrow.

.p816
.smart -
.macpack longbranch
.a16
.i16

.macro A8
        sep #$20
        .a8
.endmacro
.macro A16
        rep #$20
        .a16
.endmacro

; ---- direct page, bank $00 ---------------------------------------------------
r0 = $00                ; scratch words: caller-saved, clobbered by any call
r1 = $02
r2 = $04
r3 = $06
r4 = $08
r5 = $0A
r6 = $0C
r7 = $0E
bp = $10                ; far pointer to the current block-cache buffer
ep = $14                ; far pointer to an extent or index array
p0 = $18                ; near pointers, read through the data bank
p1 = $1A
p2 = $1C
p3 = $1E
fp = $20                ; general far pointers
fq = $24
jv = $28                ; a jump vector: JMP (abs) reads its pointer from bank $00

; Macros branch over unnamed labels only: a named label, however local, would
; end the scope of the @labels around the place a macro is used.

; ---- 32-bit helpers on absolute operands -------------------------------------
.macro MOV32 dst, src
        lda src
        sta dst
        lda src+2
        sta dst+2
.endmacro
.macro MOVI32 dst, value
        lda #.loword(value)
        sta dst
        lda #.hiword(value)
        sta dst+2
.endmacro
.macro ZERO32 dst
        stz dst
        stz dst+2
.endmacro
.macro ADD32 dst, src
        clc
        lda dst
        adc src
        sta dst
        lda dst+2
        adc src+2
        sta dst+2
.endmacro
.macro SUB32 dst, src
        sec
        lda dst
        sbc src
        sta dst
        lda dst+2
        sbc src+2
        sta dst+2
.endmacro
.macro INC32 dst
        inc dst
        bne :+
        inc dst+2
:
.endmacro
.macro DEC32 dst
        lda dst
        bne :+
        dec dst+2
:       dec dst
.endmacro
; unsigned compare: carry set if lhs >= rhs, Z set if equal
.macro CMP32 lhs, rhs
        lda lhs+2
        cmp rhs+2
        bne :+
        lda lhs
        cmp rhs
:
.endmacro
.macro SHL32 dst
        asl dst
        rol dst+2
.endmacro
.macro SHR32 dst
        lsr dst+2
        ror dst
.endmacro
; Z set if the 32-bit value is zero
.macro TST32 val
        lda val
        ora val+2
.endmacro

; load / store a field of the block the far pointer bp addresses
.macro LDF off
        ldy #off
        lda [bp],y
.endmacro
.macro STF off
        ldy #off
        sta [bp],y
.endmacro
