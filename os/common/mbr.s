; mbr.s — the MBR scan, shared by the BIOS and the kernel (CN1.5, CN1.16).
;
; The including file provides:
;       part_lba  .res 4        first sector of the partition found
;       part_secs .res 4        its length in sectors
;       part_num  .res 2        its number, 1-4
;       mbr_type  .res 2
; and sets sd_addr to a 512-byte buffer that fp also points at.
;
; mbr_find: A = type byte. Returns E_OK, E_IO, E_INVAL (no signature) or
; E_NOENT (no partition of that type). MBR only; GPT is not supported (CN1.25).

.proc mbr_find
        and #$00FF
        sta mbr_type
        stz sd_lba
        stz sd_lba+2
        lda #1
        jsr sd_read
        cmp #E_OK
        beq @got
        rts
@got:   ldy #510
        lda [fp],y
        cmp #$AA55
        beq @sig
        lda #E_INVAL
        rts
@sig:   lda #1
        sta part_num
        ldy #446
@entry: iny
        iny
        iny
        iny                     ; +4: the type byte
        lda [fp],y
        and #$00FF
        cmp mbr_type
        beq @found
        tya
        clc
        adc #12
        tay
        inc part_num
        lda part_num
        cmp #5
        bcc @entry
        lda #E_NOENT
        rts
@found: iny
        iny
        iny
        iny                     ; +8: first LBA
        lda [fp],y
        sta part_lba
        iny
        iny
        lda [fp],y
        sta part_lba+2
        iny
        iny                     ; +12: sector count
        lda [fp],y
        sta part_secs
        iny
        iny
        lda [fp],y
        sta part_secs+2
        lda #E_OK
        rts
.endproc
