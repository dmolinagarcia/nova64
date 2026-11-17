; crc.s — CRC-32, the one checksum of NVFS (Y2.4): reflected, polynomial
; $EDB88320, initial value and final XOR $FFFFFFFF. Table-driven; the table is
; built at boot rather than carried in the image.

.segment "BSS"
crc:        .res 4
crc_n:      .res 2
crc_tab_lo: .res 512
crc_tab_hi: .res 512
.segment "CODE"

.proc crc_init
        ldx #0
@entry: txa
        lsr a
        sta r0
        stz r1
        ldy #8
@bit:   lsr r1
        ror r0
        bcc @next
        lda r0
        eor #$8320
        sta r0
        lda r1
        eor #$EDB8
        sta r1
@next:  dey
        bne @bit
        lda r0
        sta crc_tab_lo,x
        lda r1
        sta crc_tab_hi,x
        inx
        inx
        cpx #512
        bne @entry
        rts
.endproc

.proc crc_begin
        lda #$FFFF
        sta crc
        sta crc+2
        rts
.endproc

; fp = data, A = length in bytes
.proc crc_update
        sta crc_n
        cmp #0
        beq @done
        ldy #0
@byte:  lda [fp],y
        eor crc
        and #$00FF
        asl a
        tax
        lda crc+1               ; crc >> 8, low word
        eor crc_tab_lo,x
        sta r0
        lda crc+2               ; crc >> 8, high word
        xba
        and #$00FF
        eor crc_tab_hi,x
        sta crc+2
        lda r0
        sta crc
        iny
        cpy crc_n
        bne @byte
@done:  rts
.endproc

.proc crc_end
        lda crc
        eor #$FFFF
        sta crc
        lda crc+2
        eor #$FFFF
        sta crc+2
        rts
.endproc

; the checksum of the 2048-byte block at bp, its last four bytes read as zero
.proc crc_block
        jsr crc_begin
        lda bp
        sta fp
        lda bp+2
        sta fp+2
        lda #$07FC
        jsr crc_update
        lda #.loword(crc_zero)
        sta fp
        lda #KBANK
        sta fp+2
        lda #4
        jsr crc_update
        jmp crc_end
.endproc
crc_zero: .byte 0, 0, 0, 0

; store the block checksum at offset $7FC of the block at bp
.proc crc_seal
        jsr crc_block
        ldy #$07FC
        lda crc
        sta [bp],y
        iny
        iny
        lda crc+2
        sta [bp],y
        rts
.endproc

; -> E_OK if the block at bp carries its own checksum, else E_CORRUPT
.proc crc_check
        jsr crc_block
        ldy #$07FC
        lda [bp],y
        cmp crc
        bne @bad
        iny
        iny
        lda [bp],y
        cmp crc+2
        bne @bad
        lda #E_OK
        rts
@bad:   lda #E_CORRUPT
        rts
.endproc
