; nvfsops.s — the NVFS operations the VFS calls: one name in one directory at
; a time, never a path (Y1.8). Each runs inside the transaction its caller
; opened, and every group the atomicity table of sheet Y2 lists commits
; together because all of it is in that one transaction.

.segment "BSS"
op_dir:   .res 4
op_isdir: .res 2
op_new:   .res 4
rm_ino:   .res 4
rm_blk:   .res 4
rm_off:   .res 2
rn_ino:   .res 4                ; rename: the inode that moves
rn_type:  .res 2
rn_blk:   .res 4
rn_off:   .res 2
rn_ndir:  .res 4
rn_name:  .res 2
rn_len:   .res 2
or_next:  .res 4
or_steps: .res 4
.segment "CODE"

; create the name at p1 / nm_len in op_dir -> nv_ino. A file, or with op_isdir
; a directory with one empty block and exactly two links (Y2.13): the parent's
; link count does not move.
.proc nv_create
        MOV32 d_dir, op_dir
        TRY dir_lookup
        cmp #0
        beq @new
        lda #E_EXIST
        rts
@new:   TRY ino_alloc
        MOV32 op_new, nv_ino
        lda op_isdir
        bne @dir
        lda #S_IFREG | $01A4    ; 0644 (Y1.16)
        sta ibuf+I_MODE
        lda #1
        sta ibuf+I_LINKS
        TRY ino_store
        lda #8
        jmp @link
@dir:   stz nv_goal
        stz nv_goal+2
        TRY blk_alloc
        MOV32 d_newblk, nv_newblk
        lda #S_IFDIR | $01ED    ; 0755
        sta ibuf+I_MODE
        lda #2
        sta ibuf+I_LINKS
        lda #2048
        sta ibuf+I_SIZE
        lda #1
        sta ibuf+I_BLOCKS
        MOV32 ibuf+I_EXT+4, d_newblk
        lda #1
        sta ibuf+I_EXT+8
        sta ibuf+I_EXTCNT       ; one extent, depth 0
        TRY ino_store
        MOV32 bc_arg, d_newblk
        TRY bc_get_zero
        MOV32 d_dir, op_new
        jsr db_header
        MOV32 d_dir, op_dir
        lda #4
@link:  sta de_type
        MOV32 de_ino, op_new
        TRY dir_add
        MOV32 nv_ino, op_new
        lda #E_OK
        rts
.endproc

; the inode in ibuf loses a link. At zero it is freed with all its blocks --
; unless a descriptor still holds it, in which case it joins the orphan list
; in this same transaction and is freed when the last one closes (Y2.19).
.proc nv_drop
        dec ibuf+I_LINKS
        beq @last
        jmp ino_store
@last:  jsr vfs_is_open         ; ibuf_ino -> carry set if some file_t refers to it
        bcc @free
        TRY sb_get
        LDF SB_ORPHAN
        sta ibuf+I_ORPHAN
        LDF SB_ORPHAN+2
        sta ibuf+I_ORPHAN+2
        lda ibuf_ino
        STF SB_ORPHAN
        lda ibuf_ino+2
        STF SB_ORPHAN+2
        jsr sb_dirty
        jmp ino_store
@free:  TRY ext_free_all
        jmp ino_free
.endproc

; remove the file named p1 / nm_len from op_dir
.proc nv_unlink
        MOV32 d_dir, op_dir
        TRY dir_lookup
        cmp #0
        bne @found
        lda #E_NOENT
        rts
@found: lda de_type
        cmp #4
        bne @file
        lda #E_ISDIR
        rts
@file:  MOV32 rm_ino, de_ino
        TRY dir_del
        MOV32 nv_ino, rm_ino
        TRY ino_load
        jmp nv_drop
.endproc

; remove the directory named p1 / nm_len from op_dir, if nothing is in it
.proc nv_rmdir
        MOV32 d_dir, op_dir
        TRY dir_lookup
        cmp #0
        bne @found
        lda #E_NOENT
        rts
@found: lda de_type
        cmp #4
        beq @dir
        lda #E_NOTDIR
        rts
@dir:   MOV32 rm_ino, de_ino
        MOV32 rm_blk, de_blk
        lda de_off
        sta rm_off
        MOV32 d_dir, rm_ino
        TRY dir_empty
        cmp #0
        bne @empty
        lda #E_NOTEMPTY
        rts
@empty: MOV32 nv_ino, rm_ino
        TRY ino_load
        TRY ext_free_all
        TRY ino_free
        MOV32 de_blk, rm_blk
        lda rm_off
        sta de_off
        jmp dir_del
.endproc

; move the name at p1 / nm_len in op_dir to the name at rn_name / rn_len in
; rn_ndir, replacing what is there if it is compatible. One transaction, so
; there is no moment at which the file has no name (15.5). The caller has
; checked that a directory is not moving into itself: NVFS stores no parent
; pointers and cannot (Y2.13).
.proc nv_rename
        MOV32 d_dir, op_dir
        TRY dir_lookup
        cmp #0
        bne @have
        lda #E_NOENT
        rts
@have:  MOV32 rn_ino, de_ino
        lda de_type
        sta rn_type
        MOV32 rn_blk, de_blk
        lda de_off
        sta rn_off

        lda rn_name
        sta p1
        lda rn_len
        sta nm_len
        MOV32 d_dir, rn_ndir
        TRY dir_lookup
        cmp #0
        jeq @fresh

        CMP32 de_ino, rn_ino    ; the same file under both names: nothing to do
        bne @replace
        lda #E_OK
        rts
@replace:
        lda rn_type
        cmp #4
        bne @overfile
        lda de_type             ; a directory may only replace an empty directory
        cmp #4
        beq @overdir
        lda #E_NOTDIR
        rts
@overdir:
        MOV32 rm_ino, de_ino
        MOV32 rm_blk, de_blk
        lda de_off
        sta rm_off
        MOV32 d_dir, rm_ino
        TRY dir_empty
        cmp #0
        bne @retarget
        lda #E_NOTEMPTY
        rts
@overfile:
        lda de_type
        cmp #4
        bne @keep
        lda #E_ISDIR
        rts
@keep:  MOV32 rm_ino, de_ino
        MOV32 rm_blk, de_blk
        lda de_off
        sta rm_off
@retarget:
        MOV32 bc_arg, rm_blk    ; the existing slot now names the moved inode
        TRY bc_get
        ldy rm_off
        lda rn_ino
        sta [bp],y
        iny
        iny
        lda rn_ino+2
        sta [bp],y
        lda rm_off
        clc
        adc #DE_LEN
        tay
        lda [bp],y
        and #$00FF
        sta r0
        lda rn_type
        xba
        ora r0
        sta [bp],y
        lda #BC_DIRTY | BC_CRC
        jsr bc_mark
        MOV32 nv_ino, rm_ino    ; and what it used to name loses that link
        TRY ino_load
        lda ibuf+I_MODE
        and #S_IFMT
        cmp #S_IFDIR
        bne @drop
        TRY ext_free_all
        TRY ino_free
        bra @unlink
@drop:  TRY nv_drop
        bra @unlink

@fresh: MOV32 de_ino, rn_ino
        lda rn_type
        sta de_type
        TRY dir_add
@unlink:
        MOV32 de_blk, rn_blk    ; last, the old name
        lda rn_off
        sta de_off
        jmp dir_del
.endproc

; empty the file nv_ino: every block released, size zero
.proc nv_trunc0
        TRY ino_load
        TRY ext_free_all
        stz ibuf+I_SIZE
        stz ibuf+I_SIZE+2
        jmp ino_store
.endproc

; the last descriptor on nv_ino has closed. If the file was unlinked while it
; was open, it is on the orphan list: take it off and free it, in one
; transaction. -> A = 1 if a transaction is now open, 0 if nothing was done.
.proc nv_release
        MOV32 rm_ino, nv_ino
        TRY ino_load
        lda ibuf+I_LINKS
        beq @orphan
        lda #0
        rts
@orphan:
        MOV32 or_next, ibuf+I_ORPHAN
        TRY sb_get
        LDF SB_ORPHAN
        cmp rm_ino
        bne @walk
        LDF SB_ORPHAN+2
        cmp rm_ino+2
        bne @walk
        lda or_next             ; it heads the list
        STF SB_ORPHAN
        lda or_next+2
        STF SB_ORPHAN+2
        jsr sb_dirty
        bra @free
@walk:  LDF SB_ORPHAN
        sta nv_ino
        LDF SB_ORPHAN+2
        sta nv_ino+2
        MOV32 or_steps, nv_tinodes
@link:  lda nv_ino
        ora nv_ino+2
        beq @bad                ; not on the list at all
        TRY ino_load
        CMP32 ibuf+I_ORPHAN, rm_ino
        beq @splice
        MOV32 nv_ino, ibuf+I_ORPHAN
        DEC32 or_steps
        TST32 or_steps
        bne @link
@bad:   lda #E_CORRUPT          ; a cycle, or a list that does not hold it
        rts
@splice:
        MOV32 ibuf+I_ORPHAN, or_next
        TRY ino_store
@free:  MOV32 nv_ino, rm_ino
        TRY ino_load
        TRY ext_free_all
        TRY ino_free
        lda #1
        rts
.endproc

; at mount, after recovery: every inode on the orphan list is freed, one
; transaction each. The walk bounds itself and a bad link fails the mount
; rather than looping (Y2.19).
.proc nv_orphans
        MOV32 or_steps, nv_tinodes
@next:  TRY sb_get
        LDF SB_ORPHAN
        sta nv_ino
        LDF SB_ORPHAN+2
        sta nv_ino+2
        ora nv_ino
        bne @one
        lda #E_OK
        rts
@one:   lda nv_ino+2
        bne @range
        lda nv_ino
        cmp #8
        bcc @bad
@range: CMP32 nv_tinodes, nv_ino
        bcc @bad
        TST32 or_steps
        beq @bad
        DEC32 or_steps
        TRY ino_load
        lda ibuf+I_LINKS
        bne @bad
        MOV32 or_next, ibuf+I_ORPHAN
        TRY ext_free_all
        TRY ino_free
        TRY sb_get
        lda or_next
        STF SB_ORPHAN
        lda or_next+2
        STF SB_ORPHAN+2
        jsr sb_dirty
        TRY txn_commit
        jmp @next
@bad:   jsr txn_abort
        lda #E_CORRUPT
        rts
.endproc

; nv_ino -> the nv_stat at p2. mode is already POSIX and passes through (Y2.9).
.proc nv_stat
        TRY ino_load
        lda ibuf+I_MODE
        sta (p2)
        ldy #ST_NLINK
        lda ibuf+I_LINKS
        sta (p2),y
        ldx #0
        ldy #ST_SIZE
@size:  lda ibuf+I_SIZE,x
        sta (p2),y
        iny
        iny
        inx
        inx
        cpx #4
        bne @size
        lda ibuf+I_BLOCKS       ; NVFS counts its own blocks; vattr_t counts 512-byte units
        sta r0
        lda ibuf+I_BLOCKS+2
        sta r1
        asl r0
        rol r1
        asl r0
        rol r1
        ldy #ST_BLOCKS
        lda r0
        sta (p2),y
        iny
        iny
        lda r1
        sta (p2),y
        ldy #ST_ATIME           ; inode order is mtime, ctime, atime
        lda ibuf+$14
        sta (p2),y
        iny
        iny
        lda ibuf+$16
        sta (p2),y
        ldx #0
        ldy #ST_MTIME
@time:  lda ibuf+$0C,x
        sta (p2),y
        iny
        iny
        inx
        inx
        cpx #8
        bne @time
        ldy #ST_FILE_ID
        lda ibuf_ino
        sta (p2),y
        iny
        iny
        lda ibuf_ino+2
        sta (p2),y
        ldy #ST_UID
        lda ibuf+$68
        sta (p2),y
        ldy #ST_GID
        lda ibuf+$6A
        sta (p2),y
        lda #E_OK
        rts
.endproc

; the target of the symbolic link nv_ino -> the buffer at io_buf, at most
; io_cnt bytes. A = its length. 72 bytes or fewer live in the inode (Y2.11).
.proc nv_readlink
        TRY ino_load
        lda ibuf+I_SIZE+2
        bne @data
        lda ibuf+I_SIZE
        cmp #73
        bcs @data
        cmp io_cnt
        bcc @n
        lda io_cnt
@n:     sta io_done
        lda io_buf
        sta p0
        ldy #0
        A8
@copy:  cpy io_done
        beq @end
        lda ibuf+I_EXT,y
        sta (p0),y
        iny
        bra @copy
@end:   A16
        lda io_done
        rts
@data:  stz io_off
        stz io_off+2
        jmp nv_read
.endproc

; everything is already on the card: each transaction is committed and
; checkpointed as its operation ends. sync has nothing left to flush.
.proc nv_sync
        jmp txn_commit
.endproc
