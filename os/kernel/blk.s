; blk.s — the block driver: the card through Helium's SD block (CN1.14).
;
; Below the block cache there is one request shape: an NVFS block, which is
; four consecutive sectors into a 2048-byte-aligned buffer, issued as a single
; multi-block transfer (Y2.24, G.11). The driver follows J.4's five-function
; shape from this build onward (D111) but is not registered in the device
; table: the raw card is not exposed (CN1.15).

SCRATCH_A = $E000               ; two 2 KB work buffers in the kernel's far bank,
SCRATCH_B = $E800               ; for the MBR, the journal's own blocks and splits

.segment "BSS"
sd_lba:    .res 4
sd_addr:   .res 4
sd_hc:     .res 2
sd_t0:     .res 2
sd_tmp:    .res 2
part_lba:  .res 4
part_secs: .res 4
part_num:  .res 2
mbr_type:  .res 2
bio_blk:   .res 4               ; NVFS block number
bio_addr:  .res 2               ; buffer, in the far bank
.segment "CODE"

.include "common/sdcard.s"
.include "common/mbr.s"

.proc blk_init
        jmp sd_init
.endproc

; A = MBR type byte -> part_lba, part_secs, part_num
.proc blk_find_part
        pha
        lda #SCRATCH_A
        sta fp
        sta sd_addr
        lda #KFAR_BANK
        sta fp+2
        sta sd_addr+2
        pla
        jmp mbr_find
.endproc

; lba = partition + (block << 2): a shift, never a multiply (Y2.3)
.proc bio_setup
        lda bio_blk
        sta sd_lba
        lda bio_blk+2
        sta sd_lba+2
        asl sd_lba
        rol sd_lba+2
        asl sd_lba
        rol sd_lba+2
        ADD32 sd_lba, part_lba
        lda bio_addr
        sta sd_addr
        lda #KFAR_BANK
        sta sd_addr+2
        rts
.endproc

.proc bio_read                  ; bio_blk -> the buffer at bio_addr
        jsr bio_setup
        lda #4
        jmp sd_read
.endproc

.proc bio_write
        jsr bio_setup
        lda #4
        jmp sd_write
.endproc

.proc blk_ioctl
        lda #E_NOTTY
        rts
.endproc

.proc blk_irq                   ; the completion interrupt: unused while the driver polls BUSY
        rts
.endproc
