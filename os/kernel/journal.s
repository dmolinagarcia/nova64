; journal.s — the NVFS journal: physical, redo-only, metadata-only, circular
; (Y2.15-Y2.18, DN-FS-NVFS-003 section 13).
;
; A transaction is the set of BC_DIRTY buffers in the block cache. Commit logs
; them, writes the descriptor and then the commit block, and checkpoints at
; once: every transaction is home, and the checkpoint advanced, before the
; next begins. That is the simplest correct reading of 13.6 -- steps 8-10 may
; be deferred, not must be -- and it makes the reuse rule of Y2.17 hold by
; construction, since no freed block can still be the destination of a
; transaction that has not been checkpointed.
;
; File data never passes through here. It is written through the cache before
; commit, which is the one ordering rule of Y2.18: data=ordered.

MAGIC_NV   = $564E              ; 'N' 'V', the first word of every metadata magic
MAGIC_JS   = $534A              ; 'J' 'S'
MAGIC_JD   = $444A              ; 'J' 'D'
MAGIC_JC   = $434A              ; 'J' 'C'
J_MAX      = 507                ; blocks per transaction, by format

.segment "BSS"
j_start:     .res 4             ; block of the journal superblock
j_end:       .res 4             ; first block past the journal region
j_ring:      .res 2             ; ring length in blocks
j_ckpt:      .res 2             ; ring index where the next transaction goes
j_seq:       .res 4
j_n:         .res 2
j_i:         .res 2
j_pcrc:      .res 4
j_check:     .res 2             ; recovery may look but not write
nv_replayed: .res 2
.segment "CODE"

.proc use_ja                    ; bp, bio_addr -> scratch A
        lda #SCRATCH_A
        bra use_scratch
.endproc
.proc use_jb
        lda #SCRATCH_B
        ; fall through
.endproc
.proc use_scratch
        sta bp
        sta bio_addr
        lda #KFAR_BANK
        sta bp+2
        rts
.endproc

.proc zero_bp
        ldy #0
        lda #0
@next:  sta [bp],y
        iny
        iny
        cpy #2048
        bne @next
        rts
.endproc

; A = ring index, possibly once past the end -> bio_blk
.proc j_ringblk
@mod:   cmp j_ring
        bcc @ok
        sbc j_ring
        bra @mod
@ok:    sec                     ; + 1: the ring follows the journal superblock
        adc j_start
        sta bio_blk
        lda j_start+2
        adc #0
        sta bio_blk+2
        rts
.endproc

; write the journal superblock with the current checkpoint and sequence
.proc j_write_sb
        jsr use_ja
        jsr zero_bp
        lda #MAGIC_NV
        STF 0
        lda #MAGIC_JS
        STF 2
        lda j_ring
        STF 4
        lda j_ckpt
        STF 8
        lda j_seq
        STF $0C
        lda j_seq+2
        STF $0E
        jsr crc_seal
        jsr use_ja
        MOV32 bio_blk, j_start
        jmp bio_write
.endproc

; commit the open transaction, if there is one, and checkpoint it
.proc txn_commit
        stz j_n
        ldx #0
@count: lda bc_flags,x
        and #BC_DIRTY
        beq @cnext
        inc j_n
        lda bc_flags,x
        and #BC_CRC
        beq @cnext
        phx
        jsr bc_ptr
        jsr crc_seal
        plx
@cnext: inx
        inx
        cpx #NBUF*2
        bne @count
        lda j_n
        bne @log
        lda #E_OK
        rts

        ; descriptor: the home of every block logged, in the order logged
@log:   jsr use_ja
        jsr zero_bp
        lda #MAGIC_NV
        STF 0
        lda #MAGIC_JD
        STF 2
        lda j_seq
        STF 4
        lda j_seq+2
        STF 6
        lda j_n
        STF 8
        ldy #$10
        ldx #0
@dest:  lda bc_flags,x
        and #BC_DIRTY
        beq @dnext
        lda bc_blk_lo,x
        sta [bp],y
        iny
        iny
        lda bc_blk_hi,x
        sta [bp],y
        iny
        iny
@dnext: inx
        inx
        cpx #NBUF*2
        bne @dest
        jsr crc_seal
        jsr crc_begin
        jsr @feed

        ; 13.6 step 2: the blocks into the ring
        stz j_i
        ldx #0
@put:   lda bc_flags,x
        and #BC_DIRTY
        beq @pnext
        phx
        jsr bc_ptr
        jsr @feed
        lda bp
        sta bio_addr
        lda j_ckpt
        sec
        adc j_i                 ; + 1 + i
        jsr j_ringblk
        jsr bio_write
        plx
        cmp #E_OK
        beq @pok
        rts
@pok:   inc j_i
@pnext: inx
        inx
        cpx #NBUF*2
        bne @put
        jsr crc_end
        MOV32 j_pcrc, crc

        ; step 4: the descriptor. Every data block this transaction wrote is
        ; already on the card (step 3).
        jsr use_ja
        lda j_ckpt
        jsr j_ringblk
        TRY bio_write

        ; step 6: the commit block. Past this write the transaction is durable.
        jsr use_jb
        jsr zero_bp
        lda #MAGIC_NV
        STF 0
        lda #MAGIC_JC
        STF 2
        lda j_seq
        STF 4
        lda j_seq+2
        STF 6
        lda j_pcrc
        STF 8
        lda j_pcrc+2
        STF $0A
        jsr crc_seal
        jsr use_jb
        lda j_ckpt
        sec
        adc j_n
        jsr j_ringblk
        TRY bio_write

        ; steps 8-10: checkpoint. Each block to its home, then the journal
        ; superblock past this transaction -- in that order, never the other.
        ldx #0
@home:  lda bc_flags,x
        and #BC_DIRTY
        beq @hnext
        lda bc_blk_lo,x
        sta bio_blk
        lda bc_blk_hi,x
        sta bio_blk+2
        phx
        jsr bc_ptr
        lda bp
        sta bio_addr
        jsr bio_write
        plx
        cmp #E_OK
        beq @hok
        rts
@hok:   lda bc_flags,x
        and #$FFFF - BC_DIRTY
        sta bc_flags,x
@hnext: inx
        inx
        cpx #NBUF*2
        bne @home
        lda j_ckpt
        clc
        adc j_n
        clc
        adc #2
@wrap:  cmp j_ring
        bcc @adv
        sbc j_ring
        bra @wrap
@adv:   sta j_ckpt
        INC32 j_seq
        jmp j_write_sb

@feed:  lda bp                  ; the block at bp into the payload checksum
        sta fp
        lda bp+2
        sta fp+2
        lda #2048
        jmp crc_update
.endproc

; an operation failed part-way: nothing of it reaches the card
.proc txn_abort
        jmp bc_drop_dirty
.endproc

; ---- recovery (13.7) -----------------------------------------------------------
; Stops at the first thing that does not check out. With j_check set it writes
; nothing and answers E_ROFS if a committed transaction is waiting, which is
; what a read-only mounter must do (Y2.20).

.proc j_read_a                  ; A = ring index -> scratch A
        pha
        jsr use_ja
        pla
        jsr j_ringblk
        jmp bio_read
.endproc
.proc j_read_b
        pha
        jsr use_jb
        pla
        jsr j_ringblk
        jmp bio_read
.endproc

.proc j_recover
        stz nv_replayed
        lda #SCRATCH_A          ; fq reads the descriptor while bp is elsewhere
        sta fq
        lda #KFAR_BANK
        sta fq+2
        jsr use_ja
        MOV32 bio_blk, j_start
        TRY bio_read
        LDF 0
        cmp #MAGIC_NV
        jne @corrupt
        LDF 2
        cmp #MAGIC_JS
        jne @corrupt
        TRY crc_check
        LDF 6                   ; a ring this driver can index in 16 bits
        jne @corrupt
        LDF 4
        cmp j_ring
        jne @corrupt
        LDF $0A
        jne @corrupt
        LDF 8
        cmp j_ring
        jcs @corrupt
        sta j_ckpt
        LDF $0C
        sta j_seq
        LDF $0E
        sta j_seq+2

@txn:   lda j_ckpt
        TRY j_read_a
        LDF 0
        cmp #MAGIC_NV
        jne @done
        LDF 2
        cmp #MAGIC_JD
        jne @done
        jsr crc_check
        cmp #E_OK
        jne @done
        jsr use_ja
        LDF 4
        cmp j_seq
        jne @done
        LDF 6
        cmp j_seq+2
        jne @done
        LDF 8                   ; validated before it is used (Y2.15)
        jeq @done
        cmp #J_MAX+1
        jcs @done
        sta j_n
        clc
        adc #2
        cmp j_ring
        beq @fits
        jcs @done
@fits:  lda j_ckpt
        sec
        adc j_n
        TRY j_read_b
        LDF 0
        cmp #MAGIC_NV
        jne @done
        LDF 2
        cmp #MAGIC_JC
        jne @done
        jsr crc_check
        cmp #E_OK
        jne @done
        jsr use_jb
        LDF 4
        cmp j_seq
        jne @done
        LDF 6
        cmp j_seq+2
        jne @done
        LDF 8
        sta j_pcrc
        LDF $0A
        sta j_pcrc+2

        jsr crc_begin           ; the payload: descriptor, then every logged block as stored
        lda #SCRATCH_A
        jsr @feed
        stz j_i
@sum:   lda j_ckpt
        sec
        adc j_i
        TRY j_read_b
        lda #SCRATCH_B
        jsr @feed
        inc j_i
        lda j_i
        cmp j_n
        bne @sum
        jsr crc_end
        lda crc
        cmp j_pcrc
        jne @done
        lda crc+2
        cmp j_pcrc+2
        jne @done

        lda j_check
        beq @valid
        lda #E_ROFS
        rts

@valid: ldy #$10                ; every destination in range and outside the journal
        ldx j_n
@dchk:  lda [fq],y
        sta r4
        iny
        iny
        lda [fq],y
        sta r5
        iny
        iny
        CMP32 r4, nv_total
        jcs @corrupt
        CMP32 r4, j_start
        bcc @dok
        CMP32 r4, j_end
        jcc @corrupt
@dok:   dex
        bne @dchk

        stz j_i                 ; apply in ascending order: the later copy of a block wins
@apply: lda j_ckpt
        sec
        adc j_i
        TRY j_read_b
        lda j_i
        asl a
        asl a
        clc
        adc #$10
        tay
        lda [fq],y
        sta bio_blk
        iny
        iny
        lda [fq],y
        sta bio_blk+2
        TRY bio_write
        inc j_i
        lda j_i
        cmp j_n
        bne @apply

        lda j_ckpt
        clc
        adc j_n
        clc
        adc #2
@wrap:  cmp j_ring
        bcc @adv
        sbc j_ring
        bra @wrap
@adv:   sta j_ckpt
        INC32 j_seq
        inc nv_replayed
        jmp @txn

@done:  lda nv_replayed         ; the replayed blocks are on the card: only now the checkpoint
        beq @ok
        TRY j_write_sb
@ok:    lda #E_OK
        rts
@corrupt:
        lda #E_CORRUPT
        rts

@feed:  sta fp
        lda #KFAR_BANK
        sta fp+2
        lda #2048
        jmp crc_update
.endproc
