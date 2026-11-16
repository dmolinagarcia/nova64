; sdcard.s — Helium's SD block, driven through the DMA path (G.7-G.11).
;
; Included by both the BIOS and the kernel: the same source assembled twice,
; as the MBR scan is (CN1.5, CN1.16). The including file provides the storage:
;
;       sd_lba   .res 4         sector address
;       sd_addr  .res 4         24-bit physical address of the buffer, 512-aligned
;       sd_hc    .res 2         non-zero once the card answered as SDHC
;       sd_t0    .res 2
;       sd_tmp   .res 2
;
; Card initialisation is software (G.11): CMD0, CMD8, ACMD41 until ready, CMD58.
; Every wait is bounded by the fixed-frequency counter, never by a cycle count
; (L.15, D92). Returns are E_OK or E_IO in A.

SD_TIMEOUT_MS = 2000

.proc sd_cmd                    ; A = command index, sd_tmp/Y = argument low/high -> A = response
        pha
        lda sd_tmp
        sta f:SD_ARG_L
        tya
        sta f:SD_ARG_H
        pla
        sta f:SD_CMD
        lda f:SD_RESP
        rts
.endproc

.proc sd_init
        stz sd_hc
        lda f:SD_CARD
        and #$0001
        bne @present
@fail:  lda #E_IO
        rts
@present:
        lda #$0004              ; clear errors, initialisation clock
        sta f:SD_CTRL
        stz sd_tmp
        ldy #0
        lda #0                  ; CMD0: go idle
        jsr sd_cmd
        and #$00FF
        cmp #$0001
        jne @fail
        lda #$01AA              ; CMD8: 2.7-3.6 V, check pattern
        sta sd_tmp
        ldy #0
        lda #8
        jsr sd_cmd
        and #$00FF
        cmp #$0001
        jne @fail
        lda f:TMR_MS_L
        sta sd_t0
@acmd:  stz sd_tmp
        ldy #0
        lda #55
        jsr sd_cmd
        stz sd_tmp
        ldy #$4000              ; HCS
        lda #41
        jsr sd_cmd
        and #$00FF
        beq @ready
        lda f:TMR_MS_L
        sec
        sbc sd_t0
        cmp #SD_TIMEOUT_MS
        bcc @acmd
        jmp @fail
@ready: stz sd_tmp
        ldy #0
        lda #58                 ; CMD58: read OCR
        jsr sd_cmd
        lda #$0008              ; full-speed clock
        sta f:SD_CTRL
        lda f:SD_CARD
        and #$0002
        jeq @fail
        lda f:SD_CARD
        and #$0004
        sta sd_hc
        lda #E_OK
        rts
.endproc

; wait for BUSY to clear, then report the error bits
.proc sd_wait
        lda f:TMR_MS_L
        sta sd_t0
@poll:  lda f:SD_STATUS
        and #SD_ST_BUSY
        beq @idle
        lda f:TMR_MS_L
        sec
        sbc sd_t0
        cmp #SD_TIMEOUT_MS
        bcc @poll
        bra @fail
@idle:  lda f:SD_STATUS
        and #SD_ST_ERRORS
        beq @ok
        lda #$000C              ; clear the error bits, keep the fast clock
        sta f:SD_CTRL
@fail:  lda #E_IO
        rts
@ok:    lda #E_OK
        rts
.endproc

; load LBA and destination; leaves the frame number in sd_tmp
.proc sd_setup
        lda sd_hc
        bne @block
        lda sd_lba              ; SDSC takes a byte address: sector << 9
        sta r6
        lda sd_lba+2
        sta r7
        ldy #9
@shift: asl r6
        rol r7
        dey
        bne @shift
        lda r6
        sta f:SD_LBA_L
        lda r7
        sta f:SD_LBA_H
        bra @dest
@block: lda sd_lba
        sta f:SD_LBA_L
        lda sd_lba+2
        sta f:SD_LBA_H
@dest:  lda sd_addr+2           ; frame = address >> 11
        and #$00FF
        asl a
        asl a
        asl a
        asl a
        asl a
        sta sd_tmp
        lda sd_addr
        xba
        and #$00FF
        lsr a
        lsr a
        lsr a
        ora sd_tmp
        sta sd_tmp
        sta f:SD_DST_FRAME
        lda sd_addr
        and #$07FF
        sta f:SD_DST_OFF
        rts
.endproc

; A = sector count. Card -> memory: invalidate the frame, then read (G.9).
.proc sd_read
        pha
        jsr sd_setup
        lda sd_tmp
        sta f:HE_CACHE_INVAL_FRAME
        pla
        sta f:SD_DMA_READ
        jmp sd_wait
.endproc

; A = sector count. Memory -> card: flush the frame, then write (L.16).
.proc sd_write
        pha
        jsr sd_setup
        lda sd_tmp
        sta f:HE_CACHE_FLUSH_FRAME
@flush: lda f:HE_MMU_STATUS     ; asynchronous: poll BUSY (M.6)
        and #$0001
        bne @flush
        pla
        sta f:SD_DMA_WRITE
        jmp sd_wait
.endproc
