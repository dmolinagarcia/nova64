; nvfsdir.s — NVFS directories (Y2.12-Y2.14).
;
; A directory is a file of directory blocks, each with thirty fixed 64-byte
; slots. A free slot is one whose inode number is zero. Neither "." nor ".."
; occupies a slot: both are synthesised by the layer above, which knows the
; parent because that is how it got here (Y2.13). Lookup is a linear scan, and
; the 16-bit name hash rejects a slot before its name is compared.

DB_USED   = $08
DB_SLOTS  = $40
DB_END    = $7C0                ; 30 slots of 64 bytes
SLOT_SIZE = 64
DE_HASH   = 4
DE_LEN    = 6                   ; byte; the type byte follows it
DE_STR    = 8

.segment "BSS"
d_dir:    .res 4                ; the directory being worked on
d_lb:     .res 2                ; logical block
d_nblk:   .res 2
d_pblk:   .res 4
d_off:    .res 2
d_newblk: .res 4
nm_len:   .res 2                ; the name at p1
nm_hash:  .res 2
de_ino:   .res 4                ; what a lookup found, or what an add will store
de_type:  .res 2
de_blk:   .res 4
de_off:   .res 2
df_found: .res 2                ; the first free slot a lookup passed
df_blk:   .res 4
df_off:   .res 2
rd_lb:    .res 2                ; readdir position
rd_slot:  .res 2
.segment "CODE"

; load the directory d_dir into ibuf and count its blocks
.proc dir_open
        MOV32 nv_ino, d_dir
        TRY ino_load
        lda ibuf+I_MODE
        and #S_IFMT
        cmp #S_IFDIR
        beq @dir
        lda #E_NOTDIR
        rts
@dir:   lda ibuf+I_SIZE+1       ; size >> 11; a directory has no holes and no odd size
        lsr a
        lsr a
        lsr a
        sta d_nblk
        lda #E_OK
        rts
.endproc

; logical block d_lb of the directory in ibuf -> bp, checked
.proc dir_block
        lda d_lb
        sta m_lblk
        stz m_lblk+2
        TRY ext_map
        cmp #0
        beq @bad                ; a hole in a directory is corruption (Y2.11)
        MOV32 d_pblk, m_pblk
        MOV32 bc_arg, m_pblk
        TRY bc_get
        LDF 0
        cmp #MAGIC_NV
        bne @bad
        LDF 2
        cmp #MAGIC_DB
        bne @bad
        ldx bc_cur
        lda bc_flags,x
        and #BC_OK
        bne @known
        TRY crc_check
        lda #BC_OK
        jsr bc_mark
@known: LDF 4                   ; the back-reference
        cmp ibuf_ino
        bne @bad
        LDF 6
        cmp ibuf_ino+2
        bne @bad
        lda #E_OK
        rts
@bad:   lda #E_CORRUPT
        rts
.endproc

; the hash of the name at p1: the low 16 bits of its CRC-32
.proc name_hash
        jsr crc_begin
        lda p1
        sta fp
        lda #KBANK
        sta fp+2
        lda nm_len
        jsr crc_update
        jsr crc_end
        lda crc
        sta nm_hash
        rts
.endproc

; fq -> the name bytes of the slot at d_off in the block at bp
.proc slot_name
        lda bp
        clc
        adc d_off
        clc
        adc #DE_STR
        sta fq
        lda bp+2
        sta fq+2
        rts
.endproc

; d_dir, name at p1 / nm_len -> A = 1 and de_* if found, 0 if not. Either way
; df_* remembers the first free slot seen, for an add that follows directly.
.proc dir_lookup
        TRY dir_open
        jsr name_hash
        stz df_found
        stz d_lb
@blk:   lda d_lb
        cmp d_nblk
        bcc @scan
        lda #0
        rts
@scan:  TRY dir_block
        lda #DB_SLOTS
        sta d_off
@slot:  ldy d_off
        lda [bp],y
        iny
        iny
        ora [bp],y
        bne @used
        lda df_found
        bne @next
        inc df_found
        MOV32 df_blk, d_pblk
        lda d_off
        sta df_off
        bra @next
@used:  iny
        iny                     ; +4: hash, two bytes compared before up to 56
        lda [bp],y
        cmp nm_hash
        bne @next
        iny
        iny
        lda [bp],y
        and #$00FF
        cmp nm_len
        bne @next
        jsr slot_name
        ldy #0
        A8
@cmp:   lda [fq],y
        cmp (p1),y
        bne @differ
        iny
        cpy nm_len
        bne @cmp
        A16
        ldy d_off               ; the first match wins
        lda [bp],y
        sta de_ino
        iny
        iny
        lda [bp],y
        sta de_ino+2
        lda d_off
        clc
        adc #DE_LEN+1
        tay
        lda [bp],y
        and #$00FF
        sta de_type
        MOV32 de_blk, d_pblk
        lda d_off
        sta de_off
        lda #1
        rts
@differ:
        A16
@next:  lda d_off
        clc
        adc #SLOT_SIZE
        sta d_off
        cmp #DB_END
        jcc @slot
        inc d_lb
        jmp @blk
.endproc

; write the slot at d_off of the block at bp: de_ino, de_type, the name at p1
.proc slot_fill
        ldy d_off
        ldx #SLOT_SIZE/2
        lda #0
@zero:  sta [bp],y
        iny
        iny
        dex
        bne @zero
        ldy d_off
        lda de_ino
        sta [bp],y
        iny
        iny
        lda de_ino+2
        sta [bp],y
        iny
        iny
        lda nm_hash
        sta [bp],y
        iny
        iny
        lda de_type
        xba
        ora nm_len
        sta [bp],y
        jsr slot_name
        ldy #0
        A8
@name:  lda (p1),y
        sta [fq],y
        iny
        cpy nm_len
        bne @name
        A16
        ldy #DB_USED
        lda [bp],y
        inc a
        sta [bp],y
        rts
.endproc

; add de_ino / de_type under the name at p1 to d_dir. Must follow a dir_lookup
; of the same name that found nothing: it uses the free slot that scan saw.
.proc dir_add
        lda df_found
        beq @grow
        MOV32 bc_arg, df_blk
        TRY bc_get
        lda df_off
        sta d_off
        jsr slot_fill
        lda #BC_DIRTY | BC_CRC
        jsr bc_mark
        lda #E_OK
        rts

        ; every block is full: the directory grows by one, in this transaction
@grow:  TRY dir_open
        stz nv_goal
        stz nv_goal+2
        lda d_nblk
        beq @alloc
        dec a
        sta m_lblk
        stz m_lblk+2
        TRY ext_map
        cmp #1
        bne @alloc
        clc
        lda m_pblk
        adc #1
        sta nv_goal
        lda m_pblk+2
        adc #0
        sta nv_goal+2
@alloc: TRY blk_alloc
        MOV32 d_newblk, nv_newblk
        lda d_nblk
        sta m_lblk
        stz m_lblk+2
        MOV32 m_pblk, d_newblk
        TRY ext_insert
        clc
        lda ibuf+I_SIZE
        adc #2048
        sta ibuf+I_SIZE
        bcc @blocks
        inc ibuf+I_SIZE+2
@blocks:
        INC32 ibuf+I_BLOCKS
        TRY ino_store
        MOV32 bc_arg, d_newblk
        TRY bc_get_zero
        jsr db_header
        lda #DB_SLOTS
        sta d_off
        jsr slot_fill
        lda #E_OK
        rts
.endproc

; make the zeroed block at bp an empty directory block of d_dir
.proc db_header
        lda #MAGIC_NV
        STF 0
        lda #MAGIC_DB
        STF 2
        lda d_dir
        STF 4
        lda d_dir+2
        STF 6
        lda #BC_DIRTY | BC_CRC | BC_OK
        jmp bc_mark
.endproc

; remove the slot a lookup found: one field cleared, the rest zeroed. The
; block is never freed and the directory never shrinks (Y2.14).
.proc dir_del
        MOV32 bc_arg, de_blk
        TRY bc_get
        ldy de_off
        ldx #SLOT_SIZE/2
        lda #0
@zero:  sta [bp],y
        iny
        iny
        dex
        bne @zero
        ldy #DB_USED
        lda [bp],y
        dec a
        sta [bp],y
        lda #BC_DIRTY | BC_CRC
        jsr bc_mark
        lda #E_OK
        rts
.endproc

; d_dir -> A = 1 if no slot in any block is occupied
.proc dir_empty
        TRY dir_open
        stz d_lb
@blk:   lda d_lb
        cmp d_nblk
        bcc @look
        lda #1
        rts
@look:  TRY dir_block
        LDF DB_USED
        bne @no
        inc d_lb
        bra @blk
@no:    lda #0
        rts
.endproc

; the next entry of d_dir at or after rd_lb / rd_slot -> the nv_dirent at p2.
; A = 1, or 0 at the end.
detype:  .byte 0, 6, VCHR, 0, VDIR, 0, 5, 0, VREG, 0, VLNK    ; DT_* -> vtype_t
.proc dir_next
        TRY dir_open
@blk:   lda rd_lb
        cmp d_nblk
        bcc @look
        lda #0
        rts
@look:  sta d_lb
        TRY dir_block
@slot:  lda rd_slot
        cmp #30
        bcc @this
        stz rd_slot
        inc rd_lb
        bra @blk
@this:  xba                     ; slot * 64
        lsr a
        lsr a
        clc
        adc #DB_SLOTS
        sta d_off
        inc rd_slot
        tay
        lda [bp],y
        sta r0
        iny
        iny
        lda [bp],y
        sta r1
        ora r0
        beq @slot
        lda r0
        sta (p2)
        ldy #DE_FILE_ID+2
        lda r1
        sta (p2),y
        lda d_off
        clc
        adc #DE_LEN
        tay
        lda [bp],y              ; length, type
        pha
        and #$00FF
        sta r2
        pla
        xba
        and #$00FF
        cmp #11
        bcc @type
        lda #0
@type:  tax
        lda detype,x
        and #$00FF
        sta r3
        lda r2                  ; type in the low byte, length in the high
        xba
        ora r3
        ldy #DE_TYPE
        sta (p2),y
        jsr slot_name
        lda p2
        clc
        adc #DE_NAME
        sta p0
        ldy #0
        A8
@name:  cpy r2
        beq @term
        lda [fq],y
        sta (p0),y
        iny
        bra @name
@term:  lda #0
        sta (p0),y
        A16
        lda #1
        rts
.endproc
