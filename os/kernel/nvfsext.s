; nvfsext.s — NVFS extents and file data (Y2.10, Y2.11).
;
; The root of a file's extent tree is the six entries in its inode; a node is
; a block of 169. A logical block covered by no extent is a hole and reads as
; zeros. Every routine here works on the inode in ibuf.

; point ep at the inode's own entries, x_cnt and x_depth from its header
.proc ext_root
        lda #.loword(ibuf+I_EXT)
        sta ep
        lda #KBANK
        sta ep+2
        lda ibuf+I_EXTCNT
        and #$00FF
        sta x_cnt
        lda ibuf+I_EXTCNT
        xba
        and #$00FF
        sta x_depth
        cmp #4
        bcs @bad
        lda x_cnt
        cmp #7
        bcs @bad
        lda #E_OK
        rts
@bad:   lda #E_CORRUPT
        rts
.endproc

; the last of x_cnt entries at ep whose first logical block is <= m_lblk
; -> carry set and Y = its offset, or carry clear
.proc ext_find
        stz r7
        ldx x_cnt
        beq @none
        ldy #0
@entry: iny
        iny
        lda [ep],y
        dey
        dey
        cmp m_lblk+2
        bcc @le
        bne @stop
        lda [ep],y
        cmp m_lblk
        beq @le
        bcs @stop
@le:    sty r6
        inc r7
        tya
        clc
        adc #EXT_SIZE
        tay
        dex
        bne @entry
@stop:  lda r7
        beq @none
        ldy r6
        sec
        rts
@none:  clc
        rts
.endproc

; the extent node at bp must be what its parent says it is. The depth check is
; mandatory: it is what bounds the walk (Y2.10).
.proc xn_check
        LDF 0
        cmp #MAGIC_NV
        bne @bad
        LDF 2
        cmp #MAGIC_XN
        bne @bad
        ldx bc_cur
        lda bc_flags,x
        and #BC_OK
        bne @known
        TRY crc_check
        lda #BC_OK
        jsr bc_mark
@known: LDF 6
        cmp x_depth
        bne @bad
        LDF 8
        cmp ibuf_ino
        bne @bad
        LDF $0A
        cmp ibuf_ino+2
        bne @bad
        LDF 4
        beq @bad
        cmp #XN_MAX+1
        bcs @bad
        sta x_cnt
        lda bp
        clc
        adc #$10
        sta ep
        lda bp+2
        sta ep+2
        lda #E_OK
        rts
@bad:   lda #E_CORRUPT
        rts
.endproc

; m_lblk -> A = 1 with m_pblk and m_flags, or 0 for a hole
.proc ext_map
        TRY ext_root
@level: jsr ext_find
        bcc @hole
        lda x_depth
        bne @index
        sec                     ; leaf: is the block inside this extent?
        lda m_lblk
        sbc [ep],y
        sta r0
        iny
        iny
        lda m_lblk+2
        sbc [ep],y
        bne @hole
        tya
        clc
        adc #6
        tay                     ; +8: length
        lda r0
        cmp [ep],y
        bcs @hole
        iny
        iny                     ; +10: flags
        lda [ep],y
        sta m_flags
        tya
        sec
        sbc #6
        tay                     ; +4: physical block
        clc
        lda [ep],y
        adc r0
        sta m_pblk
        iny
        iny
        lda [ep],y
        adc #0
        sta m_pblk+2
        lda #1
        rts
@index: tya
        clc
        adc #4
        tay
        lda [ep],y
        sta bc_arg
        iny
        iny
        lda [ep],y
        sta bc_arg+2
        TRY bc_get
        dec x_depth
        TRY xn_check
        bra @level
@hole:  lda #0
        rts
.endproc

; insert the single block m_lblk -> m_pblk into the x_cnt entries at ep, which
; has room for x_cap. -> A = 0 merged into its predecessor, 1 inserted (x_ins
; is where), 2 no room
.proc arr_insert
        stz x_ins
        jsr ext_find
        bcc @shift
        sty r6
        tya
        clc
        adc #EXT_SIZE
        sta x_ins
        ; merging is required, or sequential writes make one extent per block:
        ; contiguous in both spaces, same flags, and room in the length
        tya
        clc
        adc #8
        tay
        lda [ep],y
        sta r5                  ; length
        cmp #$FFFF
        beq @shift
        iny
        iny
        lda [ep],y
        bne @shift              ; flags differ from a plain extent
        ldy r6
        clc
        lda [ep],y
        adc r5
        sta r0
        iny
        iny
        lda [ep],y
        adc #0
        sta r1
        CMP32 r0, m_lblk
        bne @shift
        iny
        iny
        clc
        lda [ep],y
        adc r5
        sta r0
        iny
        iny
        lda [ep],y
        adc #0
        sta r1
        CMP32 r0, m_pblk
        bne @shift
        iny
        iny
        lda r5
        inc a
        sta [ep],y
        lda #0
        rts

@shift: lda x_cnt
        cmp x_cap
        bcc @room
        lda #2
        rts
@room:  asl a                   ; count * 12
        asl a
        sta x_end
        asl a
        clc
        adc x_end
        sta x_end
        lda ep                  ; fq = ep + 12, so one index moves a word up a slot
        clc
        adc #EXT_SIZE
        sta fq
        lda ep+2
        sta fq+2
        ldy x_end
@move:  cpy x_ins
        beq @write
        dey
        dey
        lda [ep],y
        sta [fq],y
        bra @move
@write: lda m_lblk
        sta [ep],y
        iny
        iny
        lda m_lblk+2
        sta [ep],y
        iny
        iny
        lda m_pblk
        sta [ep],y
        iny
        iny
        lda m_pblk+2
        sta [ep],y
        iny
        iny
        lda #1
        sta [ep],y
        iny
        iny
        lda #0
        sta [ep],y
        lda #1
        rts
.endproc

; give the new node at bp its header: A = entries, owner ibuf_ino, depth 0
.proc xn_header
        pha
        lda #MAGIC_NV
        STF 0
        lda #MAGIC_XN
        STF 2
        pla
        STF 4
        lda #0
        STF 6
        lda ibuf_ino
        STF 8
        lda ibuf_ino+2
        STF $0A
        lda #BC_DIRTY | BC_CRC | BC_OK
        jmp bc_mark
.endproc

; map the hole at m_lblk to the block m_pblk. Changes ibuf; the caller stores it.
.proc ext_insert
        lda ibuf+I_DEPTH
        and #$00FF
        jne @deep
        jsr ext_root
        lda #6
        sta x_cap
        jsr arr_insert
        cmp #1
        jeq @rooted
        jcc @done               ; merged
        ; the inode is full: the tree grows a level. The six entries move into
        ; a new node and the inode keeps one index entry for it (10.6).
        MOV32 x_left, m_pblk    ; blk_alloc leaves m_* alone, but the cache may not
        stz nv_goal
        stz nv_goal+2
        TRY blk_alloc
        MOV32 x_node, nv_newblk
        MOV32 bc_arg, x_node
        TRY bc_get_zero
        ldx #0
        ldy #$10
@down:  lda ibuf+I_EXT,x
        sta [bp],y
        stz ibuf+I_EXT,x
        iny
        iny
        inx
        inx
        cpx #6*EXT_SIZE
        bne @down
        lda #6
        jsr xn_header
        LDF $10                 ; the node's first logical block heads the index entry
        sta ibuf+I_EXT
        LDF $12
        sta ibuf+I_EXT+2
        MOV32 ibuf+I_EXT+4, x_node
        lda #$0101              ; one entry, depth one
        sta ibuf+I_EXTCNT
        INC32 ibuf+I_BLOCKS
        bra @deep
@rooted:
        A8
        inc ibuf+I_EXTCNT
        A16
@done:  lda #E_OK
        rts

@deep:  lda ibuf+I_DEPTH
        and #$00FF
        cmp #1
        beq @one
        lda #E_FBIG             ; depth 2 and 3 are read and freed, not grown
        rts
@one:   jsr ext_root
        jsr ext_find
        bcs @have
        ldy #0                  ; before every node: it goes at the front of the first
@have:  sty x_roff
        lda ibuf+I_EXT+4,y
        sta bc_arg
        sta x_left
        lda ibuf+I_EXT+6,y
        sta bc_arg+2
        sta x_left+2
        TRY bc_get
        stz x_depth
        TRY xn_check
        lda #XN_MAX
        sta x_cap
        jsr arr_insert
        cmp #1
        beq @leafed
        bcs @split
@marked:
        lda #BC_DIRTY | BC_CRC
        jsr bc_mark
        lda #E_OK
        rts
@leafed:
        ldy #4
        lda [bp],y
        inc a
        sta [bp],y
        lda x_ins               ; a new first entry changes what the index says of this node
        bne @marked
        ldy x_roff
        lda m_lblk
        sta ibuf+I_EXT,y
        lda m_lblk+2
        sta ibuf+I_EXT+2,y
        bra @marked

        ; the node is full: split it at the midpoint and index the new half
@split: lda ibuf+I_EXTCNT
        and #$00FF
        cmp #6
        bcc @halve
        lda #E_FBIG
        rts
@halve: lda #SCRATCH_B          ; the upper 84 entries leave through scratch B
        sta fq
        lda #KFAR_BANK
        sta fq+2
        ldy #$10 + 85*EXT_SIZE
        ldx #0
@out:   lda [bp],y
        sta r0
        lda #0
        sta [bp],y
        phy
        txy
        lda r0
        sta [fq],y
        ply
        iny
        iny
        inx
        inx
        cpx #84*EXT_SIZE
        bne @out
        lda #85
        STF 4
        lda #BC_DIRTY | BC_CRC
        jsr bc_mark
        stz nv_goal
        stz nv_goal+2
        TRY blk_alloc
        MOV32 x_node, nv_newblk
        MOV32 bc_arg, x_node
        TRY bc_get_zero
        lda #SCRATCH_B
        sta fq
        lda #KFAR_BANK
        sta fq+2
        ldy #0
@in:    lda [fq],y
        pha
        tya
        clc
        adc #$10
        tay
        pla
        sta [bp],y
        tya
        sec
        sbc #$10 - 2
        tay
        cpy #84*EXT_SIZE
        bne @in
        lda #84
        jsr xn_header
        ; index entry for the new node, after the one that was split
        lda ibuf+I_EXTCNT
        and #$00FF
        asl a
        asl a
        sta r0
        asl a
        clc
        adc r0
        tax                     ; X = end of the root entries
        lda x_roff
        clc
        adc #EXT_SIZE
        sta r1                  ; where the new entry goes
@up:    cpx r1
        beq @index
        dex
        dex
        lda ibuf+I_EXT,x
        sta ibuf+I_EXT+EXT_SIZE,x
        bra @up
@index: LDF $10
        sta ibuf+I_EXT,x
        LDF $12
        sta ibuf+I_EXT+2,x
        lda x_node
        sta ibuf+I_EXT+4,x
        lda x_node+2
        sta ibuf+I_EXT+6,x
        stz ibuf+I_EXT+8,x
        stz ibuf+I_EXT+10,x
        A8
        inc ibuf+I_EXTCNT
        A16
        INC32 ibuf+I_BLOCKS
        jmp @one                ; and try again: one of the halves has room
.endproc

; free the run of xe_n blocks at xe_p
.proc free_run
@next:  lda xe_n
        beq @done
        MOV32 nv_fblk, xe_p
        TRY blk_free
        INC32 xe_p
        dec xe_n
        bra @next
@done:  lda #E_OK
        rts
.endproc

; free the extent node xe_p of depth A and everything under it. The walk keeps
; its place in three small per-level slots rather than on the stack (Y1.12).
.proc free_node
        asl a
        sta xf_lvl
        sta xf_top
        tax
        lda xe_p
        sta xf_blk_lo,x
        lda xe_p+2
        sta xf_blk_hi,x
        stz xf_idx,x
@loop:  ldx xf_lvl
        lda xf_blk_lo,x
        sta bc_arg
        lda xf_blk_hi,x
        sta bc_arg+2
        TRY bc_get
        LDF 0
        cmp #MAGIC_NV
        jne @bad
        LDF 2
        cmp #MAGIC_XN
        jne @bad
        LDF 6
        asl a
        cmp xf_lvl
        jne @bad
        LDF 4
        cmp #XN_MAX+1
        jcs @bad
        ldx xf_lvl
        cmp xf_idx,x
        beq @empty
        lda xf_idx,x            ; next entry of this node
        inc xf_idx,x
        asl a
        asl a
        sta r0
        asl a
        clc
        adc r0
        clc
        adc #$10 + 4
        tay
        lda [bp],y
        sta xe_p
        iny
        iny
        lda [bp],y
        sta xe_p+2
        lda xf_lvl
        bne @child
        iny
        iny
        lda [bp],y
        sta xe_n
        TRY free_run
        jmp @loop
@child: dec xf_lvl
        dec xf_lvl
        ldx xf_lvl
        lda xe_p
        sta xf_blk_lo,x
        lda xe_p+2
        sta xf_blk_hi,x
        stz xf_idx,x
        jmp @loop
@empty: lda xf_blk_lo,x         ; every entry is gone: the node itself
        sta nv_fblk
        lda xf_blk_hi,x
        sta nv_fblk+2
        TRY blk_free
        lda xf_lvl
        cmp xf_top
        beq @done
        inc xf_lvl
        inc xf_lvl
        jmp @loop
@done:  lda #E_OK
        rts
@bad:   lda #E_CORRUPT
        rts
.endproc

; release every block the inode in ibuf holds, to any depth. Leaves it with no
; extents; size and the rest are the caller's.
.proc ext_free_all
        lda ibuf+I_MODE
        and #S_IFMT
        cmp #S_IFREG
        beq @tree
        cmp #S_IFDIR
        beq @tree
        cmp #S_IFLNK
        bne @clear              ; devices and FIFOs hold nothing
        lda ibuf+I_SIZE+2
        bne @tree
        lda ibuf+I_SIZE
        cmp #73
        bcc @clear              ; an inline link: the extent area is its target (Y2.11)
@tree:  TRY ext_root
        stz xf_i
@next:  lda xf_i
        cmp x_cnt
        beq @clear
        asl a
        asl a
        sta r0
        asl a
        clc
        adc r0
        tax
        lda ibuf+I_EXT+4,x
        sta xe_p
        lda ibuf+I_EXT+6,x
        sta xe_p+2
        lda ibuf+I_DEPTH
        and #$00FF
        bne @node
        lda ibuf+I_EXT+8,x
        sta xe_n
        TRY free_run
        bra @step
@node:  dec a
        TRY free_node
        lda ibuf+I_EXTCNT       ; free_node used the scratch counters: restore ours
        and #$00FF
        sta x_cnt
@step:  inc xf_i
        bra @next
@clear: ldx #6*EXT_SIZE-2
@zero:  stz ibuf+I_EXT,x
        dex
        dex
        bpl @zero
        stz ibuf+I_EXTCNT       ; count and depth
        stz ibuf+I_BLOCKS
        stz ibuf+I_BLOCKS+2
        lda #E_OK
        rts
.endproc

; ==============================================================================
; file data
; ==============================================================================

; split io_off and io_cnt: m_lblk, io_boff, and io_n = what fits this block
.proc io_split
        lda io_off
        and #$07FF
        sta io_boff
        lda io_off+1            ; (offset >> 8) >> 3 = offset >> 11
        sta m_lblk
        lda io_off+3
        and #$00FF
        sta m_lblk+2
        ldx #3
@shift: lsr m_lblk+2
        ror m_lblk
        dex
        bne @shift
        lda #2048
        sec
        sbc io_boff
        cmp io_cnt
        bcc @n
        lda io_cnt
@n:     sta io_n
        rts
.endproc

.proc io_advance
        clc
        lda io_off
        adc io_n
        sta io_off
        bcc @buf
        inc io_off+2
@buf:   clc
        lda io_buf
        adc io_n
        sta io_buf
        clc
        lda io_done
        adc io_n
        sta io_done
        sec
        lda io_cnt
        sbc io_n
        sta io_cnt
        rts
.endproc

; nv_ino, io_off, io_buf, io_cnt -> A = bytes read. Short past the end; holes
; and unwritten extents read as zeros; i_atime is not touched (Y2.9).
.proc nv_read
        stz io_done
        TRY ino_load
        CMP32 io_off, ibuf+I_SIZE
        jcs @end
        sec                     ; clamp to what the file has left
        lda ibuf+I_SIZE
        sbc io_off
        sta r0
        lda ibuf+I_SIZE+2
        sbc io_off+2
        bne @loop
        lda r0
        cmp io_cnt
        bcs @loop
        sta io_cnt
@loop:  lda io_cnt
        jeq @end
        jsr io_split
        TRY ext_map
        cmp #0
        beq @zeros
        lda m_flags
        and #$0001
        bne @zeros
        MOV32 bc_arg, m_pblk
        TRY bc_get
        lda bp                  ; cache -> the caller's buffer
        clc
        adc io_boff
        tax
        ldy io_buf
        lda io_n
        dec a
        mvn KFAR_BANK << 16, KBANK << 16    ; leaves DBR on the kernel's bank
        bra @adv
@zeros: lda io_buf
        sta p0
        ldy #0
        A8
        lda #0
@zero:  sta (p0),y
        iny
        cpy io_n
        bne @zero
        A16
@adv:   jsr io_advance
        jmp @loop
@end:   lda io_done
        rts
.endproc

; nv_ino, io_off, io_buf, io_cnt -> A = bytes written. The data goes to the
; card at once; the metadata that makes it part of the file joins the open
; transaction, so the commit that follows finds the data durable (Y2.18).
.proc nv_write
        stz io_done
        TRY ino_load
        lda ibuf+I_MODE
        and #S_IFMT
        cmp #S_IFREG
        beq @loop
        lda #E_INVAL
        rts
@loop:  lda io_cnt
        jeq @size
        jsr io_split
        TRY ext_map
        cmp #0
        beq @hole
        lda m_flags
        and #$0001
        beq @mapped
        lda #E_NOTSUP
        rts
@mapped:
        MOV32 bc_arg, m_pblk
        lda io_n
        cmp #2048
        beq @whole
        TRY bc_get              ; part of a block: read, modify, write
        jmp @copy
@whole: TRY bc_get_zero
        jmp @copy

@hole:  stz nv_goal             ; allocate, next to the previous block if that is free
        stz nv_goal+2
        lda m_lblk
        ora m_lblk+2
        beq @alloc
        lda m_lblk
        pha
        lda m_lblk+2
        pha
        DEC32 m_lblk
        jsr ext_map
        tax
        pla
        sta m_lblk+2
        pla
        sta m_lblk
        cpx #1
        bne @alloc
        clc
        lda m_pblk
        adc #1
        sta nv_goal
        lda m_pblk+2
        adc #0
        sta nv_goal+2
@alloc: TRY blk_alloc
        MOV32 m_pblk, nv_newblk
        lda m_pblk
        pha
        lda m_pblk+2
        pha
        jsr ext_insert
        tax
        pla
        sta bc_arg+2
        pla
        sta bc_arg
        txa
        bpl @added
        rts
@added: INC32 ibuf+I_BLOCKS
        TRY bc_get_zero         ; a new block starts as zeros, whatever it held before

@copy:  lda bp                  ; the caller's buffer -> cache
        clc
        adc io_boff
        tay
        ldx io_buf
        lda io_n
        dec a
        phb
        mvn KBANK << 16, KFAR_BANK << 16
        plb
        TRY bc_write_cur
        jsr io_advance
        jmp @loop

@size:  CMP32 io_off, ibuf+I_SIZE
        bcc @store
        MOV32 ibuf+I_SIZE, io_off
@store: TRY ino_store
        lda io_done
        rts
.endproc
