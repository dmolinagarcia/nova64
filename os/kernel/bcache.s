; bcache.s — one block cache below the filesystem (Y1.14): uniform 2048-byte
; buffers, each aligned to 2048 so that none straddles a bank and the DMA
; destination is a shift away (Y2.23).
;
; A buffer is the single in-memory copy of its block. Nothing above keeps a
; second copy of an inode or a directory block across calls, which is the
; property Y1.6 wants from a vnode cache: two open files on one inode cannot
; hold diverging copies, because neither holds a copy at all.
;
; BC_DIRTY marks a metadata block that belongs to the open transaction. It
; stays in the cache, unevictable, until the journal has committed it. File
; data is written through at once and never carries the flag.
;
; A far pointer into a buffer is valid until the next bc_get: callers fetch
; again rather than hold one.

NBUF     = 24                   ; 48 KB of bank $FC
BC_VALID = $01
BC_DIRTY = $02
BC_CRC   = $04                  ; carries a block checksum at $7FC, sealed at commit
BC_OK    = $08                  ; structure and checksum verified since it was read

.segment "BSS"
bc_arg:    .res 4               ; the block a call is about
bc_cur:    .res 2               ; slot of the last bc_get, as an index into the tables
bc_tick:   .res 2
bc_blk_lo: .res NBUF*2
bc_blk_hi: .res NBUF*2
bc_flags:  .res NBUF*2
bc_age:    .res NBUF*2
.segment "CODE"

.proc bc_reset
        ldx #NBUF*2-2
@next:  stz bc_flags,x
        dex
        dex
        bpl @next
        rts
.endproc

; bc_arg -> carry set and X = slot if the block is cached
.proc bc_find
        ldx #0
@next:  lda bc_flags,x
        and #BC_VALID
        beq @skip
        lda bc_blk_lo,x
        cmp bc_arg
        bne @skip
        lda bc_blk_hi,x
        cmp bc_arg+2
        bne @skip
        sec
        rts
@skip:  inx
        inx
        cpx #NBUF*2
        bne @next
        clc
        rts
.endproc

; X = slot -> bp
.proc bc_ptr
        txa
        xba
        asl a
        asl a                   ; slot index * 1024 = slot * 2048
        sta bp
        lda #KFAR_BANK
        sta bp+2
        rts
.endproc

; make slot X the current one
.proc bc_use
        stx bc_cur
        inc bc_tick
        lda bc_tick
        sta bc_age,x
        jmp bc_ptr
.endproc

; -> X = a slot to reuse: a free one, else the least recently used clean one
.proc bc_victim
        ldx #0
@free:  lda bc_flags,x
        and #BC_VALID
        beq @got
        inx
        inx
        cpx #NBUF*2
        bne @free
        lda #$FFFF
        sta r0                  ; oldest age so far
        sta r1                  ; its slot
        ldx #0
@lru:   lda bc_flags,x
        and #BC_DIRTY
        bne @skip
        lda bc_tick
        sec
        sbc bc_age,x            ; how long ago, modulo 65536
        eor #$FFFF
        cmp r0
        bcs @skip
        sta r0
        stx r1
@skip:  inx
        inx
        cpx #NBUF*2
        bne @lru
        ldx r1
        bpl @got
        ldx #.loword(@full)
        jmp panic
@got:   rts
@full:  .byte "block cache: every buffer is held by the transaction", 0
.endproc

; bc_arg -> bp, reading the block from the card unless it is cached
.proc bc_get
        jsr bc_find
        bcs @hit
        jsr bc_victim
        stz bc_flags,x
        lda bc_arg
        sta bc_blk_lo,x
        sta bio_blk
        lda bc_arg+2
        sta bc_blk_hi,x
        sta bio_blk+2
        jsr bc_use
        lda bp
        sta bio_addr
        jsr bio_read
        cmp #E_OK
        bne @fail
        ldx bc_cur
        lda #BC_VALID
        sta bc_flags,x
        lda #E_OK
@fail:  rts
@hit:   jsr bc_use
        lda #E_OK
        rts
.endproc

; bc_arg -> bp, the buffer zeroed and nothing read: a block about to be
; written in full, or one that has just been allocated
.proc bc_get_zero
        jsr bc_find
        bcs @have
        jsr bc_victim
        stz bc_flags,x
        lda bc_arg
        sta bc_blk_lo,x
        lda bc_arg+2
        sta bc_blk_hi,x
@have:  lda bc_flags,x
        and #BC_DIRTY
        ora #BC_VALID
        sta bc_flags,x
        jsr bc_use
        ldy #0
        lda #0
@zero:  sta [bp],y
        iny
        iny
        cpy #2048
        bne @zero
        lda #E_OK
        rts
.endproc

; A = flags to set on the current buffer
.proc bc_mark
        ldx bc_cur
        ora bc_flags,x
        sta bc_flags,x
        rts
.endproc

; write the current buffer to its block now: the path file data takes
.proc bc_write_cur
        ldx bc_cur
        lda bc_blk_lo,x
        sta bio_blk
        lda bc_blk_hi,x
        sta bio_blk+2
        jsr bc_ptr
        lda bp
        sta bio_addr
        jmp bio_write
.endproc

; drop bc_arg from the cache, written or not: the block has been freed
.proc bc_forget
        jsr bc_find
        bcc @done
        stz bc_flags,x
@done:  rts
.endproc

; abandon the open transaction: what the cache holds for it never happened
.proc bc_drop_dirty
        ldx #NBUF*2-2
@next:  lda bc_flags,x
        and #BC_DIRTY
        beq @skip
        stz bc_flags,x
@skip:  dex
        dex
        bpl @next
        rts
.endproc
