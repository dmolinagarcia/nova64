; nvfs.s — NVFS, the native volume (sheet Y2, DN-FS-NVFS-003 draft 3):
; mount, inodes, the two bitmaps, extents and file data.
;
; Every index conversion is a shift or a mask; there is no division in the
; data path (Y2.3). All fields are little-endian and nothing is ever swapped.
;
; What this driver does not do, each a refusal rather than a wrong answer:
;   - DIRHASH directories: the incompat bit refuses the mount (Y2.14, Y2.20)
;   - extent trees deeper than one level on the write path: a file that
;     needs depth 2 gets E_FBIG. Reads and frees handle every depth to 3.
;   - writing into an UNWRITTEN extent: E_NOTSUP. Reading one gives zeros.

; ---- superblock ----------------------------------------------------------------
SB_VMAJ       = $04
SB_BSIZE      = $08
SB_TOTAL      = $0C
SB_FREE       = $10
SB_TINODES    = $14
SB_FINODES    = $18
SB_FIRST      = $1C
SB_BM_START   = $20
SB_BM_BLOCKS  = $24
SB_SUM_START  = $28
SB_SUM_BLOCKS = $2C
SB_IBM_START  = $30
SB_IBM_BLOCKS = $34
SB_IT_START   = $38
SB_IT_BLOCKS  = $3C
SB_J_START    = $40
SB_J_BLOCKS   = $44
SB_ROOT       = $48
SB_BACKUP     = $4C
SB_INCOMPAT   = $54
SB_ROCOMPAT   = $58
SB_MOUNTS     = $98
SB_STATE      = $A0
SB_NEXTGEN    = $A8
SB_HINT       = $AC
SB_ORPHAN     = $B0
MAGIC_FS      = $5346           ; 'F' 'S'
MAGIC_XN      = $4E58           ; 'X' 'N'
MAGIC_DB      = $4244           ; 'D' 'B'

; ---- inode ---------------------------------------------------------------------
I_MODE   = $00
I_LINKS  = $02
I_SIZE   = $04
I_BLOCKS = $08
I_GEN    = $18
I_EXTCNT = $1E                  ; byte
I_DEPTH  = $1F                  ; byte
I_EXT    = $20                  ; six 12-byte entries; also the inline symlink
I_ORPHAN = $70
I_CRC    = $7C
EXT_SIZE = 12
XN_MAX   = 169

.segment "BSS"
nv_mounted:    .res 2
nv_ro:         .res 2
nv_badbit:     .res 2           ; the feature bit that refused or restricted the mount, or $FFFF
nv_total:      .res 4
nv_tinodes:    .res 4
nv_first:      .res 4
nv_bm_start:   .res 4
nv_bm_blocks:  .res 4
nv_sum_start:  .res 4
nv_sum_blocks: .res 4
nv_ibm_start:  .res 4
nv_ibm_blocks: .res 4
nv_it_start:   .res 4
nv_it_blocks:  .res 4
nv_jblocks:    .res 4
nv_t:          .res 4
nv_last:       .res 4           ; total - 1: the backup superblock
nv_ino:        .res 4           ; the inode a call is about
nv_ioff:       .res 2
nv_gen:        .res 4
nv_goal:       .res 4
nv_newblk:     .res 4
nv_fblk:       .res 4
ibuf:          .res 128         ; the inode working copy, valid within one operation
ibuf_ino:      .res 4
bm_n:          .res 4
bm_base:       .res 4
bm_byte:       .res 2
bm_mask:       .res 2
bm_i:          .res 2
m_lblk:        .res 4
m_pblk:        .res 4
m_flags:       .res 2
x_cnt:         .res 2
x_cap:         .res 2
x_depth:       .res 2
x_ins:         .res 2
x_end:         .res 2
x_roff:        .res 2
x_node:        .res 4
x_left:        .res 4
xe_p:          .res 4
xe_n:          .res 2
xf_i:          .res 2
xf_lvl:        .res 2
xf_top:        .res 2
xf_blk_lo:     .res 6
xf_blk_hi:     .res 6
xf_idx:        .res 6
io_off:        .res 4
io_buf:        .res 2
io_cnt:        .res 2
io_done:       .res 2
io_n:          .res 2
io_boff:       .res 2
.segment "CODE"

; ==============================================================================
; mount (Y2.21): eight steps, and the order is the safety
; ==============================================================================

sb_fields:                      ; superblock offset -> where the layout constant is cached
        .word SB_TOTAL, nv_total, SB_TINODES, nv_tinodes, SB_FIRST, nv_first
        .word SB_BM_START, nv_bm_start, SB_BM_BLOCKS, nv_bm_blocks
        .word SB_SUM_START, nv_sum_start, SB_SUM_BLOCKS, nv_sum_blocks
        .word SB_IBM_START, nv_ibm_start, SB_IBM_BLOCKS, nv_ibm_blocks
        .word SB_IT_START, nv_it_start, SB_IT_BLOCKS, nv_it_blocks
        .word SB_J_START, j_start, SB_J_BLOCKS, nv_jblocks
        .word $FFFF

.proc sb_get                    ; the primary superblock -> bp
        lda #1
        sta bc_arg
        stz bc_arg+2
        jmp bc_get
.endproc
.proc sb_dirty
        lda #BC_DIRTY | BC_CRC
        jmp bc_mark
.endproc

; r0:r1, not zero -> A = number of its lowest set bit
.proc lowbit
        ldx #0
@next:  lsr r1
        ror r0
        bcs @got
        inx
        bra @next
@got:   txa
        rts
.endproc

.proc nv_mount
        stz nv_mounted
        stz nv_ro
        lda #$FFFF
        sta nv_badbit
        stz nv_replayed
        jsr bc_reset
        lda f:SD_CARD           ; a write-protected card can only be mounted read-only
        and #$0008
        beq @sb
        inc nv_ro

@sb:    TRY sb_get              ; 2. block 1: magic and checksum. No fallback to the backup (Y2.6)
        LDF 0
        cmp #MAGIC_NV
        jne @inval
        LDF 2
        cmp #MAGIC_FS
        jne @inval
        TRY crc_check
        LDF SB_VMAJ             ; 3. version and block size
        cmp #3
        jne @feature
        LDF SB_BSIZE
        cmp #2048
        jne @feature
        LDF SB_BSIZE+2
        jne @feature

        LDF SB_INCOMPAT         ; 4. the feature policy, before any other validation.
        sta r0                  ;    DIRHASH is defined and not supported here.
        LDF SB_INCOMPAT+2
        sta r1
        ora r0
        beq @rocompat
        jsr lowbit
        sta nv_badbit
        jmp @feature
@rocompat:
        LDF SB_ROCOMPAT
        and #$FFFC              ; METACRC and JOURNAL are the two this driver knows
        sta r0
        LDF SB_ROCOMPAT+2
        sta r1
        ora r0
        beq @layout
        jsr lowbit
        sta nv_badbit
        lda #1
        sta nv_ro

@layout:
        ldx #0                  ; cache the layout constants, not the block (Y2.23)
@field: lda sb_fields,x
        bmi @sane
        tay
        lda sb_fields+2,x
        sta p0
        lda [bp],y
        sta (p0)
        iny
        iny
        lda [bp],y
        ldy #2
        sta (p0),y
        inx
        inx
        inx
        inx
        bra @field

@sane:  ; 5. every region in range, in the order of Y2.5, none overlapping
        lda j_start+2
        jne @corrupt
        lda j_start
        cmp #8
        jcc @corrupt
        MOV32 j_end, j_start
        ADD32 j_end, nv_jblocks
        CMP32 nv_bm_start, j_end
        jcc @corrupt
        MOV32 nv_t, nv_bm_start
        ADD32 nv_t, nv_bm_blocks
        CMP32 nv_sum_start, nv_t
        jcc @corrupt
        MOV32 nv_t, nv_sum_start
        ADD32 nv_t, nv_sum_blocks
        CMP32 nv_ibm_start, nv_t
        jcc @corrupt
        MOV32 nv_t, nv_ibm_start
        ADD32 nv_t, nv_ibm_blocks
        CMP32 nv_it_start, nv_t
        jcc @corrupt
        MOV32 nv_t, nv_it_start
        ADD32 nv_t, nv_it_blocks
        CMP32 nv_first, nv_t
        jcc @corrupt
        MOV32 nv_last, nv_total
        DEC32 nv_last
        CMP32 nv_first, nv_last
        jcs @corrupt
        TRY sb_get
        LDF SB_BACKUP
        cmp nv_last
        jne @corrupt
        LDF SB_BACKUP+2
        cmp nv_last+2
        jne @corrupt
        LDF SB_ROOT
        cmp #1
        jne @corrupt
        MOV32 nv_t, nv_total    ; the volume must fit the partition it is in
        SHL32 nv_t
        SHL32 nv_t
        CMP32 part_secs, nv_t
        jcc @corrupt
        lda nv_jblocks+2        ; a ring of 2 to 65535 blocks
        jne @corrupt
        lda nv_jblocks
        cmp #3
        jcc @corrupt
        dec a
        sta j_ring

        LDF SB_STATE
        sta r4
        lda nv_ro               ; a mounter that cannot write must refuse a dirty volume (Y2.20)
        beq @recover
        lda r4
        beq @recover
        lda #E_ROFS
        rts

@recover:
        lda nv_ro               ; 6. the journal
        sta j_check
        TRY j_recover
        lda nv_replayed
        beq @state
        jsr bc_reset            ; recovery may have replaced anything, the superblock included

@state: TRY sb_get
        LDF SB_STATE
        cmp #2                  ; the error state mounts read-only: nvfsck is required
        bne @rw
        lda #1
        sta nv_ro
@rw:    lda nv_ro
        bne @root
        TRY nv_orphans          ; 7a. free what a crash left unlinked but open
        TRY sb_get
        lda #1                  ; 7b. mark the volume mounted, count the mount
        STF SB_STATE
        ldy #SB_MOUNTS
        lda [bp],y
        inc a
        sta [bp],y
        bne @cnt
        iny
        iny
        lda [bp],y
        inc a
        sta [bp],y
@cnt:   jsr sb_dirty
        TRY txn_commit

@root:  lda #1                  ; 8. the root inode: a directory with at least two links
        sta nv_ino
        stz nv_ino+2
        TRY ino_load
        lda ibuf+I_MODE
        and #S_IFMT
        cmp #S_IFDIR
        jne @corrupt
        lda ibuf+I_LINKS
        cmp #2
        jcc @corrupt
        lda #1
        sta nv_mounted
        lda #E_OK
        rts
@inval: lda #E_INVAL
        rts
@feature:
        lda #E_FEATURE
        rts
@corrupt:
        lda #E_CORRUPT
        rts
.endproc

; ==============================================================================
; inodes: 128 bytes, 16 to a block, each with its own checksum (Y2.8, Y2.9)
; ==============================================================================

; nv_ino -> bc_arg = its table block, nv_ioff = its offset there
.proc ino_locate
        lda nv_ino
        ora nv_ino+2
        beq @bad
        CMP32 nv_tinodes, nv_ino
        bcc @bad
        lda nv_ino
        sec
        sbc #1
        sta r0
        lda nv_ino+2
        sbc #0
        sta r1
        lda r0
        and #$000F
        xba
        lsr a                   ; slot << 7
        sta nv_ioff
        ldx #4
@shift: lsr r1
        ror r0
        dex
        bne @shift
        clc
        lda r0
        adc nv_it_start
        sta bc_arg
        lda r1
        adc nv_it_start+2
        sta bc_arg+2
        lda #E_OK
        rts
@bad:   lda #E_CORRUPT
        rts
.endproc

.proc ibuf_crc                  ; -> crc over bytes $00-$7B of the working copy
        jsr crc_begin
        lda #.loword(ibuf)
        sta fp
        lda #KBANK
        sta fp+2
        lda #I_CRC
        jsr crc_update
        jmp crc_end
.endproc

; nv_ino -> ibuf
.proc ino_load
        TRY ino_locate
        TRY bc_get
        ldy nv_ioff
        ldx #0
@copy:  lda [bp],y
        sta ibuf,x
        iny
        iny
        inx
        inx
        cpx #128
        bne @copy
        MOV32 ibuf_ino, nv_ino
        jsr ibuf_crc
        lda crc
        cmp ibuf+I_CRC
        bne @bad
        lda crc+2
        cmp ibuf+I_CRC+2
        bne @bad
        lda #E_OK
        rts
@bad:   lda #E_CORRUPT
        rts
.endproc

; ibuf -> its table block, as part of the open transaction
.proc ino_store
        jsr ibuf_crc
        MOV32 ibuf+I_CRC, crc
        MOV32 nv_ino, ibuf_ino
        TRY ino_locate
        TRY bc_get
        ldy nv_ioff
        ldx #0
@copy:  lda ibuf,x
        sta [bp],y
        iny
        iny
        inx
        inx
        cpx #128
        bne @copy
        lda #BC_DIRTY
        jsr bc_mark
        lda #E_OK
        rts
.endproc

.proc ibuf_zero
        ldx #126
@next:  stz ibuf,x
        dex
        dex
        bpl @next
        rts
.endproc

; ==============================================================================
; the two bitmaps: flat, one bit per block or inode, 1 = allocated (Y2.7)
; ==============================================================================
bitmask: .byte 1, 2, 4, 8, 16, 32, 64, 128

; bm_n, bm_base -> bc_arg, bm_byte, bm_mask
.proc bm_locate
        lda bm_n
        sta r0
        lda bm_n+2
        sta r1
        asl r0
        rol r1
        asl r0
        rol r1                  ; r1 = n >> 14
        clc
        lda r1
        adc bm_base
        sta bc_arg
        lda bm_base+2
        adc #0
        sta bc_arg+2
        lda bm_n
        lsr a
        lsr a
        lsr a
        and #$07FF
        sta bm_byte
        lda bm_n
        and #$0007
        tax
        lda bitmask,x
        and #$00FF
        sta bm_mask
        rts
.endproc

; -> A = the bit, 0 or not 0. The bitmap block is the current buffer.
.proc bm_test
        jsr bm_locate
        TRY bc_get
        ldy bm_byte
        lda [bp],y
        and bm_mask
        rts
.endproc

.proc bm_set
        ldy bm_byte
        A8
        lda [bp],y
        ora bm_mask
        sta [bp],y
        A16
        lda #BC_DIRTY
        jmp bc_mark
.endproc

.proc bm_clear
        ldy bm_byte
        A8
        lda bm_mask
        eor #$FF
        and [bp],y
        sta [bp],y
        A16
        lda #BC_DIRTY
        jmp bc_mark
.endproc

; the block at bp -> carry set and r0 = its first clear bit
.proc bm_scan
        ldy #0
@word:  lda [bp],y
        cmp #$FFFF
        bne @hit
        iny
        iny
        cpy #2048
        bcc @word
        clc
        rts
@hit:   ldx #0
@bit:   lsr a
        bcc @got
        inx
        bra @bit
@got:   stx r0
        tya
        asl a
        asl a
        asl a
        clc
        adc r0
        sta r0
        sec
        rts
.endproc

; the summary entry for bitmap block bm_i -> bp, Y
.proc sum_get
        lda bm_i
        xba
        and #$00FF
        lsr a
        lsr a                   ; i >> 10
        clc
        adc nv_sum_start
        sta bc_arg
        lda nv_sum_start+2
        adc #0
        sta bc_arg+2
        TRY bc_get
        lda bm_i
        and #$03FF
        asl a
        tay
        lda #E_OK
        rts
.endproc

; nv_goal (0 for none) -> nv_newblk. One group: bitmap, summary, counters (Y2 atomicity table).
.proc blk_alloc
        lda nv_goal
        ora nv_goal+2
        beq @scan
        CMP32 nv_goal, nv_first
        bcc @scan
        CMP32 nv_goal, nv_last
        bcs @scan
        MOV32 bm_n, nv_goal     ; the block after the file's last: this is what makes
        MOV32 bm_base, nv_bm_start      ; a sequential file one extent
        TRY bm_test
        cmp #0
        jeq @take
@scan:  stz bm_i
@blk:   lda bm_i
        cmp nv_bm_blocks
        bcc @sum
        lda #E_NOSPC
        rts
@sum:   TRY sum_get             ; a full bitmap block is skipped without being read
        lda [bp],y
        bne @look
        inc bm_i
        bra @blk
@look:  clc
        lda bm_i
        adc nv_bm_start
        sta bc_arg
        lda nv_bm_start+2
        adc #0
        sta bc_arg+2
        TRY bc_get
        jsr bm_scan
        bcs @found
        lda #E_CORRUPT          ; the summary promised a free block here
        rts
@found: lda bm_i                ; block = (i << 14) + bit
        lsr a
        lsr a
        sta bm_n+2
        lda bm_i
        and #$0003
        xba
        asl a
        asl a
        asl a
        asl a
        asl a
        asl a
        clc
        adc r0
        sta bm_n
        MOV32 bm_base, nv_bm_start
        TRY bm_test
@take:  jsr bm_set
        lda bm_n                ; the summary entry: one fewer free
        sta r0
        lda bm_n+2
        sta r1
        asl r0
        rol r1
        asl r0
        rol r1
        lda r1
        sta bm_i
        TRY sum_get
        lda [bp],y
        jeq @corrupt
        cmp #$FFFF              ; $FFFF stands for 16384 (Y2.7)
        bne @dec
        lda #16384
@dec:   dec a
        sta [bp],y
        lda #BC_DIRTY
        jsr bc_mark
        TRY sb_get
        ldy #SB_FREE
        lda [bp],y
        bne @lo
        iny
        iny
        lda [bp],y
        dec a
        sta [bp],y
        dey
        dey
        lda [bp],y
@lo:    dec a
        sta [bp],y
        clc                     ; the hint is advisory and need not be consistent
        lda bm_n
        adc #1
        STF SB_HINT
        lda bm_n+2
        adc #0
        STF SB_HINT+2
        jsr sb_dirty
        MOV32 nv_newblk, bm_n
        lda #E_OK
        rts
@corrupt:
        lda #E_CORRUPT
        rts
.endproc

; nv_fblk back to the pool
.proc blk_free
        CMP32 nv_fblk, nv_first
        jcc @corrupt
        CMP32 nv_fblk, nv_last
        jcs @corrupt
        MOV32 bm_n, nv_fblk
        MOV32 bm_base, nv_bm_start
        TRY bm_test
        cmp #0
        jeq @corrupt            ; freeing a free block
        jsr bm_clear
        lda bm_n
        sta r0
        lda bm_n+2
        sta r1
        asl r0
        rol r1
        asl r0
        rol r1
        lda r1
        sta bm_i
        TRY sum_get
        lda [bp],y
        cmp #$FFFF
        jeq @corrupt
        inc a
        cmp #16384
        bne @inc
        lda #$FFFF
@inc:   sta [bp],y
        lda #BC_DIRTY
        jsr bc_mark
        TRY sb_get
        ldy #SB_FREE
        lda [bp],y
        inc a
        sta [bp],y
        bne @done
        iny
        iny
        lda [bp],y
        inc a
        sta [bp],y
@done:  jsr sb_dirty
        MOV32 bc_arg, nv_fblk   ; whatever the cache held of it is stale now
        jsr bc_forget
        lda #E_OK
        rts
@corrupt:
        lda #E_CORRUPT
        rts
.endproc

; a fresh inode -> nv_ino, ibuf zeroed and carrying its generation (Y2.8)
.proc ino_alloc
        stz bm_i
@blk:   lda bm_i
        cmp nv_ibm_blocks
        bcc @look
        lda #E_NOFILE
        rts
@look:  clc
        lda bm_i
        adc nv_ibm_start
        sta bc_arg
        lda nv_ibm_start+2
        adc #0
        sta bc_arg+2
        TRY bc_get
        jsr bm_scan
        bcs @found
        inc bm_i
        bra @blk
@found: lda bm_i
        lsr a
        lsr a
        sta bm_n+2
        lda bm_i
        and #$0003
        xba
        asl a
        asl a
        asl a
        asl a
        asl a
        asl a
        clc
        adc r0
        sta bm_n
        MOV32 bm_base, nv_ibm_start
        TRY bm_test
        jsr bm_set
        clc                     ; bit n is inode n + 1
        lda bm_n
        adc #1
        sta nv_ino
        lda bm_n+2
        adc #0
        sta nv_ino+2
        TRY sb_get
        ldy #SB_FINODES
        lda [bp],y
        bne @lo
        iny
        iny
        lda [bp],y
        dec a
        sta [bp],y
        dey
        dey
        lda [bp],y
@lo:    dec a
        sta [bp],y
        ldy #SB_NEXTGEN
        lda [bp],y
        sta nv_gen
        inc a
        sta [bp],y
        iny
        iny
        lda [bp],y
        sta nv_gen+2
        lda nv_gen
        cmp #$FFFF
        bne @gen
        lda [bp],y
        inc a
        sta [bp],y
@gen:   jsr sb_dirty
        jsr ibuf_zero
        MOV32 ibuf_ino, nv_ino
        MOV32 ibuf+I_GEN, nv_gen
        lda #E_OK
        rts
.endproc

; the inode in ibuf back to the pool: bit clear, mode and links zero, and a
; valid checksum so that free is distinguishable from corrupt (Y2.8)
.proc ino_free
        sec
        lda ibuf_ino
        sbc #1
        sta bm_n
        lda ibuf_ino+2
        sbc #0
        sta bm_n+2
        MOV32 bm_base, nv_ibm_start
        TRY bm_test
        cmp #0
        bne @clear
        lda #E_CORRUPT
        rts
@clear: jsr bm_clear
        TRY sb_get
        ldy #SB_FINODES
        lda [bp],y
        inc a
        sta [bp],y
        bne @done
        iny
        iny
        lda [bp],y
        inc a
        sta [bp],y
@done:  jsr sb_dirty
        jsr ibuf_zero
        jmp ino_store
.endproc
