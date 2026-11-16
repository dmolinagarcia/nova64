; vfs.s — what the VFS owns (sheet Y1): path resolution, the open-file objects
; and the descriptor table.
;
; Resolution happens once, here, iteratively (Y1.11, Y1.12); the driver below
; sees one name in one directory and never a "/" (Y1.8). "." and ".." are
; resolved lexically against the current directory's path, which is what NVFS
; asks of its caller since it stores no parent (Y2.13). Symbolic links are
; followed here, at most eight per call.
;
; There is one mount, the root, and no vnode cache: no layer keeps a copy of
; an inode across calls (see bcache.s), so a file's identity is its inode
; number and file_t holds exactly that. file_t is still separate from the
; descriptor, so that two opens have two offsets and a dup shares one (Y1.5).

NFILE = 24                      ; open file objects
NFD   = 16                      ; descriptors
F_FREE   = 0
F_VNODE  = 1                    ; a regular file on the volume
F_DEVICE = 2                    ; an entry of the device table (CN1.15)
F_DIR    = 3                    ; a directory opened for readdir; the cookie is in the offset
SYMLINK_MAX = 8

.segment "BSS"
fd_tab:   .res NFD*2            ; descriptor -> file object index, or $FFFF
f_kind:   .res NFILE*2
f_ref:    .res NFILE*2
f_flags:  .res NFILE*2
f_ino_lo: .res NFILE*2          ; inode number, or the device number
f_ino_hi: .res NFILE*2
f_off_lo: .res NFILE*2
f_off_hi: .res NFILE*2
cwd_path: .res PATH_MAX         ; one path string and one inode number (CN1.17)
cwd_ino:  .res 4
pbuf:     .res PATH_MAX         ; a normalised absolute path
pbuf2:    .res PATH_MAX
lbuf:     .res PATH_MAX         ; a symbolic link's target
tbuf:     .res PATH_MAX*3       ; a path being rebuilt around one
w_base:   .res 2
w_mode:   .res 2
w_i:      .res 2
w_last:   .res 2
w_links:  .res 2
w_cur:    .res 4
w_type:   .res 2
vo_ino:   .res 4
.segment "CODE"

.proc vfs_init
        ldx #NFD*2-2
        lda #$FFFF
@fd:    sta fd_tab,x
        dex
        dex
        bpl @fd
        ldx #NFILE*2-2
@file:  stz f_kind,x
        dex
        dex
        bpl @file
        lda #'/'                ; "/" and a terminator
        sta cwd_path
        lda #1
        sta cwd_ino
        stz cwd_ino+2
        stz nv_mounted
        lda #$FFFF
        sta nv_badbit
        rts
.endproc

; X = a path as the caller wrote it, Y = a PATH_MAX buffer -> the absolute
; path with ".", ".." and repeated slashes resolved. "/" is the root.
.proc path_norm
        sty p3
        ldy #0
        A8
        lda a:0,x
        bne @some
        A16
        lda #E_NOENT
        rts
        .a8
@some:  cmp #'/'
        beq @comp
        lda cwd_path+1          ; relative: start from the current directory
        beq @comp
@cwd:   lda cwd_path,y
        beq @comp
        sta (p3),y
        iny
        bra @cwd
@comp:  lda a:0,x
        cmp #'/'
        bne @first
        inx
        bra @comp
@first: cmp #0
        beq @end
        stx r1                  ; where the component starts
@scan:  lda a:0,x
        beq @got
        cmp #'/'
        beq @got
        inx
        bra @scan
@got:   A16
        stx r2                  ; and where it ends
        txa
        sec
        sbc r1
        sta r0                  ; its length
        ldx r1
        cmp #1
        bne @two
        lda a:0,x
        and #$00FF
        cmp #'.'
        beq @skip
        bra @name
@two:   cmp #2
        bne @name
        lda a:0,x
        cmp #$2E2E              ; ".."
        bne @name
@pop:   cpy #0                  ; drop the last component; at the root, stay there
        beq @skip
        dey
        lda (p3),y
        and #$00FF
        cmp #'/'
        bne @pop
        bra @skip
@name:  lda r0
        cmp #NAME_MAX+1
        bcs @long
        tya
        clc
        adc r0
        cmp #PATH_MAX-2
        bcs @long
        A8
        lda #'/'
        sta (p3),y
        iny
@copy:  lda a:0,x
        sta (p3),y
        inx
        iny
        cpx r2
        bne @copy
        A16
@skip:  ldx r2
        A8
        jmp @comp
@end:   cpy #0
        bne @term
        lda #'/'
        sta (p3),y
        iny
@term:  lda #0
        sta (p3),y
        A16
        lda #E_OK
        rts
@long:  lda #E_NAMETOOLONG
        rts
.endproc

; X = a normalised path, A = 0 to resolve all of it or 1 to stop at its parent.
;   A = 0 -> w_cur, w_type: the inode and its directory-entry type
;   A = 1 -> w_cur: the parent directory; p1, nm_len: the last component
.proc vfs_walk
        stx w_base
        sta w_mode
        stz w_links
@restart:
        lda #1
        sta w_cur
        stz w_cur+2
        sta w_i
        lda #4
        sta w_type
        ldx w_base
        lda a:1,x
        and #$00FF
        bne @comp
        lda w_mode              ; the path is "/": it has no parent and no name
        beq @ok
        lda #E_INVAL
        rts
@ok:    lda #E_OK
        rts
@comp:  lda w_base
        clc
        adc w_i
        sta p1
        tax
        stz nm_len
@len:   lda a:0,x
        and #$00FF
        beq @end
        cmp #'/'
        beq @more
        inx
        inc nm_len
        bra @len
@end:   lda #1
        bra @set
@more:  lda #0
@set:   sta w_last
        beq @look
        lda w_mode
        bne @ok                 ; the parent is resolved; the name is the caller's business
@look:  MOV32 d_dir, w_cur
        TRY dir_lookup          ; E_NOTDIR if what we stand on is not a directory
        cmp #0
        bne @found
        lda #E_NOENT
        rts
@found: lda de_type
        cmp #10
        jeq @link
        MOV32 w_cur, de_ino
        lda de_type
        sta w_type
        lda w_last
        bne @ok
        lda w_i
        sec                     ; + 1 for the slash
        adc nm_len
        sta w_i
        jmp @comp

        ; a symbolic link: splice its target into the path and start again
@link:  inc w_links
        lda w_links
        cmp #SYMLINK_MAX+1
        bcc @read
        lda #E_LOOP
        rts
@read:  MOV32 nv_ino, de_ino
        lda #.loword(lbuf)
        sta io_buf
        lda #PATH_MAX-1
        sta io_cnt
        TRY nv_readlink
        tax
        A8
        stz lbuf,x
        ldy #0
        lda lbuf
        cmp #'/'
        beq @target             ; an absolute target replaces everything before it
        ldx w_base
@head:  cpx p1                  ; a relative one replaces the link's own name
        beq @target
        lda a:0,x
        sta tbuf,y
        inx
        iny
        bra @head
@target:
        ldx #0
@tgt:   lda lbuf,x
        beq @rest
        sta tbuf,y
        inx
        iny
        bra @tgt
@rest:  A16
        lda p1
        clc
        adc nm_len
        tax
        A8
@tail:  lda a:0,x               ; and whatever followed the link
        sta tbuf,y
        beq @built
        inx
        iny
        bra @tail
@built: A16
        ldx #.loword(tbuf)
        ldy w_base
        TRY path_norm
        jmp @restart
.endproc

; X, Y = normalised paths -> carry set if X is Y or lies beneath it
.proc path_under
        stx p0
        sty p1
        ldy #1
        lda (p1),y
        and #$00FF
        beq @yes                ; everything is under "/"
        ldy #0
        A8
@next:  lda (p1),y
        beq @tail
        cmp (p0),y
        bne @no
        iny
        bra @next
@tail:  lda (p0),y
        beq @same
        cmp #'/'
        beq @same
@no:    A16
        clc
        rts
        .a8
@same:  A16
@yes:   sec
        rts
.endproc

; vo_ino -> carry set if an open file object refers to that inode
.proc vfs_is_open
        MOV32 vo_ino, ibuf_ino
        ; fall through
.endproc
.proc vfs_ino_open
        ldx #0
@next:  lda f_kind,x
        beq @skip
        cmp #F_DEVICE
        beq @skip
        lda f_ino_lo,x
        cmp vo_ino
        bne @skip
        lda f_ino_hi,x
        cmp vo_ino+2
        bne @skip
        sec
        rts
@skip:  inx
        inx
        cpx #NFILE*2
        bne @next
        clc
        rts
.endproc

; A = descriptor -> X = its file object, or E_BADF
.proc fd_get
        cmp #NFD
        bcs @bad
        asl a
        tax
        lda fd_tab,x
        bmi @bad
        tax
        lda #E_OK
        rts
@bad:   lda #E_BADF
        rts
.endproc

; -> X = a free file object, zeroed, or E_MFILE
.proc file_alloc
        ldx #0
@next:  lda f_kind,x
        beq @got
        inx
        inx
        cpx #NFILE*2
        bne @next
        lda #E_MFILE
        rts
@got:   stz f_off_lo,x
        stz f_off_hi,x
        stz f_ino_hi,x
        lda #1
        sta f_ref,x
        lda #E_OK
        rts
.endproc

; bind file object X to the lowest free descriptor -> A = the descriptor, or
; E_MFILE. X is preserved.
.proc fd_bind
        ldy #0
@next:  lda fd_tab,y
        bmi @got
        iny
        iny
        cpy #NFD*2
        bne @next
        lda #E_MFILE
        rts
@got:   txa
        sta fd_tab,y
        tya
        lsr a
        rts
.endproc

; the same for an object just made: without a descriptor it is released
.proc fd_bind_new
        jsr fd_bind
        cmp #$0000
        bpl @ok
        stz f_kind,x
@ok:    rts
.endproc

; close out an operation that ran inside a transaction: A = its status.
; Success commits and checkpoints; failure discards every change it made, so
; nothing of a failed operation reaches the card. Returns the status.
.proc txn_end
        cmp #$0000
        bmi @abort
        pha
        jsr txn_commit
        cmp #E_OK
        bne @failed
        pla
        rts
@failed:
        tax
        pla
        txa
        ldx #1                  ; a commit that failed leaves the card as it is: stop writing
        stx nv_ro
@abort: pha
        jsr txn_abort
        pla
        rts
.endproc
