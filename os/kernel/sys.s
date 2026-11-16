; sys.s — the system-call layer: the sys_* handlers and the numbered service
; table (J.3, D90, LB1.10).
;
; In this build the shell reaches these by a direct call; in the user-mode
; build a COP stub does, with the service number in Y. The handlers are the
; same either way, which is why they take nothing in Y:
;
;       first argument   A          second   X          third   r0 (direct page)
;       a 32-bit argument travels in r0:r1; a 32-bit result returns in A (low), X (high)
;
; The result is zero or positive, or a negative code from the one error space
; (Y1.15). Pointers are 16 bits, in the caller's data bank, which in this
; build is the kernel's. The kernel carries no message strings for these.

.segment "BSS"
s_path:  .res 2
s_flags: .res 2
s_buf:   .res 2
s_n:     .res 2
s_file:  .res 2
s_fd:    .res 2
s_ino:   .res 4
s_odir:  .res 4
s_oname: .res 2
s_olen:  .res 2
.segment "CODE"

; run fn as one transaction; a failure returns from the caller
.macro TXN fn
        jsr fn
        jsr txn_end
        cmp #$0000
        bpl :+
        rts
:
.endmacro

str_dev: .byte "/dev/", 0

; pbuf under /dev/? -> carry set and X = the device name
.proc is_dev
        ldx #0
        A8
@next:  lda str_dev,x
        beq @yes
        cmp pbuf,x
        bne @no
        inx
        bra @next
@yes:   A16
        ldx #.loword(pbuf+5)
        sec
        rts
        .a8
@no:    A16
        clc
        rts
.endproc

.proc need_root                 ; E_NOTMOUNTED without a root filesystem (CN1.7)
        lda nv_mounted
        bne @ok
        lda #E_NOTMOUNTED
        rts
@ok:    lda #E_OK
        rts
.endproc

.proc need_rw
        TRY need_root
        lda nv_ro
        beq @ok
        lda #E_ROFS
        rts
@ok:    lda #E_OK
        rts
.endproc

; ---- 32 open(path, flags) -------------------------------------------------------
.proc sys_open
        sta s_path
        stx s_flags
        tax
        ldy #.loword(pbuf)
        TRY path_norm
        jsr is_dev              ; /dev/<name> goes to the device table, not the VFS (D108)
        bcc @file
        TRY dev_find
        sta s_ino
        TRY file_alloc
        lda #F_DEVICE
        sta f_kind,x
        lda s_ino
        sta f_ino_lo,x
        lda s_flags
        sta f_flags,x
        jmp fd_bind_new

@file:  TRY need_root
        ldx #.loword(pbuf)
        lda #0
        jsr vfs_walk
        cmp #E_NOENT
        jeq @create
        cmp #0
        bpl @exists
        rts
@exists:
        lda s_flags
        and #NV_O_CREAT | NV_O_EXCL
        cmp #NV_O_CREAT | NV_O_EXCL
        bne @type
        lda #E_EXIST
        rts
@type:  lda w_type
        cmp #8
        beq @regular
        cmp #4
        bne @special
        lda #E_ISDIR
        rts
@special:
        lda #E_NOTSUP           ; device nodes and FIFOs are stored, not opened (Y2 12.2)
        rts
@regular:
        MOV32 s_ino, w_cur
        lda s_flags
        and #NV_O_ACCMODE
        beq @open
        TRY need_rw
        lda s_flags
        and #NV_O_TRUNC
        beq @open
        MOV32 nv_ino, s_ino
        TXN nv_trunc0
        bra @open

@create:
        lda s_flags
        and #NV_O_CREAT
        bne @make
        lda #E_NOENT
        rts
@make:  TRY need_rw
        ldx #.loword(pbuf)
        lda #1
        TRY vfs_walk
        MOV32 op_dir, w_cur
        stz op_isdir
        TXN nv_create
        MOV32 s_ino, nv_ino

@open:  TRY file_alloc
        lda #F_VNODE
        sta f_kind,x
        lda s_ino
        sta f_ino_lo,x
        lda s_ino+2
        sta f_ino_hi,x
        lda s_flags
        sta f_flags,x
        jmp fd_bind_new
.endproc

; ---- 33 close(fd) ---------------------------------------------------------------
.proc sys_close
        sta s_fd
        TRY fd_get
        stx s_file
        lda s_fd
        asl a
        tay
        lda #$FFFF
        sta fd_tab,y
        dec f_ref,x
        bne @done
        lda f_kind,x
        stz f_kind,x
        cmp #F_VNODE
        bne @done
        lda f_ino_lo,x
        sta vo_ino
        lda f_ino_hi,x
        sta vo_ino+2
        jsr vfs_ino_open        ; another descriptor still has the file
        bcs @done
        lda nv_ro
        bne @done
        MOV32 nv_ino, vo_ino    ; the last close of an unlinked file frees it (Y1.18)
        jsr nv_release
        cmp #0
        beq @done
        jsr txn_end
@done:  lda #E_OK
        rts
.endproc

; clamp a byte count so that the result cannot look like an error
.proc clamp_n
        lda s_n
        bpl @ok
        lda #$7FFF
        sta s_n
@ok:    rts
.endproc

; ---- 34 read(fd, buf, n) --------------------------------------------------------
.proc sys_read
        stx s_buf
        ldx r0
        stx s_n
        TRY fd_get
        stx s_file
        jsr clamp_n
        lda f_kind,x
        cmp #F_DEVICE
        bne @file
        lda f_ino_lo,x
        sta r6
        lda s_buf
        ldx s_n
        ldy #DEV_READ
        jmp dev_call
@file:  cmp #F_VNODE
        beq @vnode
        lda #E_ISDIR
        rts
@vnode: lda f_flags,x
        and #NV_O_ACCMODE
        cmp #NV_O_WRONLY
        bne @go
        lda #E_BADF
        rts
@go:    jsr io_setup
        TRY nv_read
        jmp io_finish
.endproc

; ---- 35 write(fd, buf, n) -------------------------------------------------------
.proc sys_write
        stx s_buf
        ldx r0
        stx s_n
        TRY fd_get
        stx s_file
        jsr clamp_n
        lda f_kind,x
        cmp #F_DEVICE
        bne @file
        lda f_ino_lo,x
        sta r6
        lda s_buf
        ldx s_n
        ldy #DEV_WRITE
        jmp dev_call
@file:  cmp #F_VNODE
        beq @vnode
        lda #E_ISDIR
        rts
@vnode: lda f_flags,x
        and #NV_O_ACCMODE
        bne @mode
        lda #E_BADF
        rts
@mode:  TRY need_rw
        ldx s_file
        jsr io_setup
        ldx s_file
        lda f_flags,x
        and #NV_O_APPEND
        beq @go
        TRY ino_load            ; every append starts at the end of the file
        MOV32 io_off, ibuf+I_SIZE
@go:    lda s_n
        bne @txn
        rts
@txn:   TXN nv_write
        jmp io_finish
.endproc

.proc io_setup                  ; file object X -> the arguments of nv_read / nv_write
        lda f_ino_lo,x
        sta nv_ino
        lda f_ino_hi,x
        sta nv_ino+2
        lda f_off_lo,x
        sta io_off
        lda f_off_hi,x
        sta io_off+2
        lda s_buf
        sta io_buf
        lda s_n
        sta io_cnt
        rts
.endproc

.proc io_finish                 ; A = bytes moved: the offset follows
        ldx s_file
        pha
        lda io_off
        sta f_off_lo,x
        lda io_off+2
        sta f_off_hi,x
        pla
        rts
.endproc

; ---- 36 lseek(fd, off, whence): fd in A, whence in X, off in r0:r1 --------------
.proc sys_lseek
        stx s_flags
        ldx r0
        stx s_ino
        ldx r1
        stx s_ino+2
        jsr fd_get
        cmp #0
        bmi @err
        stx s_file
        lda f_kind,x
        cmp #F_VNODE
        bne @inval
        lda s_flags
        beq @set
        cmp #NV_SEEK_CUR
        beq @cur
        cmp #NV_SEEK_END
        bne @inval
        lda f_ino_lo,x
        sta nv_ino
        lda f_ino_hi,x
        sta nv_ino+2
        jsr ino_load
        cmp #0
        bmi @err
        ADD32 s_ino, ibuf+I_SIZE
        bra @set
@cur:   clc
        lda s_ino
        adc f_off_lo,x
        sta s_ino
        lda s_ino+2
        adc f_off_hi,x
        sta s_ino+2
@set:   lda s_ino+2
        bmi @inval
        ldx s_file
        sta f_off_hi,x
        lda s_ino
        sta f_off_lo,x
        ldx s_ino+2
        rts
@inval: lda #E_INVAL
@err:   ldx #$FFFF
        rts
.endproc

; ---- 37 ioctl(fd, op, arg) ------------------------------------------------------
.proc sys_ioctl
        stx s_flags
        ldx r0
        stx s_buf
        TRY fd_get
        lda f_kind,x
        cmp #F_DEVICE
        beq @dev
        lda #E_NOTTY
        rts
@dev:   lda f_ino_lo,x
        sta r6
        lda s_flags
        ldx s_buf
        ldy #DEV_IOCTL
        jmp dev_call
.endproc

; ---- 38 dup(fd) · 39 dup2(fd, fd2) ----------------------------------------------
.proc sys_dup
        TRY fd_get
        inc f_ref,x
        jsr fd_bind
        cmp #$0000
        bpl @ok
        dec f_ref,x
@ok:    rts
.endproc

.proc sys_dup2
        stx s_fd
        cpx #NFD
        bcc @in
        lda #E_BADF
        rts
@in:    TRY fd_get
        stx s_file
        lda s_fd
        asl a
        tay
        txa
        cmp fd_tab,y
        beq @same
        phy
        lda fd_tab,y
        bmi @free
        lda s_fd
        pha
        lda s_file
        pha
        lda s_fd
        jsr sys_close
        pla
        sta s_file
        pla
        sta s_fd
@free:  ply
        ldx s_file
        txa
        sta fd_tab,y
        inc f_ref,x
@same:  lda s_fd
        rts
.endproc

; ---- 40 stat(path, st) · 41 fstat(fd, st) ---------------------------------------
.proc stat_dev                  ; a device has no inode: a character device, 0666
        ldy #ST_SIZEOF-2
        lda #0
@zero:  sta (p2),y
        dey
        dey
        bpl @zero
        lda #S_IFCHR | $01B6
        sta (p2)
        ldy #ST_NLINK
        lda #1
        sta (p2),y
        lda #E_OK
        rts
.endproc

.proc sys_stat
        stx s_buf
        tax
        ldy #.loword(pbuf)
        TRY path_norm
        lda s_buf
        sta p2
        jsr is_dev
        bcc @file
        TRY dev_find
        jmp stat_dev
@file:  TRY need_root
        ldx #.loword(pbuf)
        lda #0
        TRY vfs_walk
        lda s_buf
        sta p2
        MOV32 nv_ino, w_cur
        jmp nv_stat
.endproc

.proc sys_fstat
        stx p2
        TRY fd_get
        lda f_kind,x
        cmp #F_DEVICE
        jeq stat_dev
        lda f_ino_lo,x
        sta nv_ino
        lda f_ino_hi,x
        sta nv_ino+2
        jmp nv_stat
.endproc

; resolve the parent of the path in A for an operation that changes it
.proc parent_of
        tax
        ldy #.loword(pbuf)
        TRY path_norm
        TRY need_rw
        ldx #.loword(pbuf)
        lda #1
        TRY vfs_walk
        MOV32 op_dir, w_cur
        lda #E_OK
        rts
.endproc

; ---- 42 unlink(path) ------------------------------------------------------------
.proc sys_unlink
        TRY parent_of
        TXN nv_unlink
        lda #E_OK
        rts
.endproc

; ---- 44 mkdir(path) -------------------------------------------------------------
.proc sys_mkdir
        TRY parent_of
        lda #1
        sta op_isdir
        TXN nv_create
        lda #E_OK
        rts
.endproc

; ---- 45 rmdir(path) -------------------------------------------------------------
.proc sys_rmdir
        tax
        ldy #.loword(pbuf)
        TRY path_norm
        TRY need_rw
        ldx #.loword(cwd_path)  ; not the directory we are standing in, nor one above it
        ldy #.loword(pbuf)
        jsr path_under
        bcc @go
        lda #E_BUSY
        rts
@go:    ldx #.loword(pbuf)
        lda #1
        TRY vfs_walk
        MOV32 op_dir, w_cur
        TXN nv_rmdir
        lda #E_OK
        rts
.endproc

; ---- 43 rename(from, to) --------------------------------------------------------
.proc sys_rename
        stx s_path
        tax
        ldy #.loword(pbuf2)
        TRY path_norm
        ldx s_path
        ldy #.loword(pbuf)
        TRY path_norm
        TRY need_rw
        ldx #.loword(pbuf)      ; a directory cannot move beneath itself: the walk that
        ldy #.loword(pbuf2)     ; proves it is the VFS's, since it holds the paths (15.5)
        jsr path_under
        bcc @cwd
        ldx #.loword(pbuf)
        ldy #.loword(pbuf2)
        jsr strcmp
        beq @cwd                ; onto itself: nv_rename answers that
        lda #E_INVAL
        rts
@cwd:   ldx #.loword(cwd_path)
        ldy #.loword(pbuf2)
        jsr path_under
        bcc @go
        lda #E_BUSY
        rts
@go:    ldx #.loword(pbuf2)
        lda #1
        TRY vfs_walk
        MOV32 s_odir, w_cur
        lda p1
        sta s_oname
        lda nm_len
        sta s_olen
        ldx #.loword(pbuf)
        lda #1
        TRY vfs_walk
        MOV32 rn_ndir, w_cur
        lda p1
        sta rn_name
        lda nm_len
        sta rn_len
        MOV32 op_dir, s_odir
        lda s_oname
        sta p1
        lda s_olen
        sta nm_len
        TXN nv_rename
        lda #E_OK
        rts
.endproc

; ---- 46 chdir(path) · 47 getcwd(buf, size) --------------------------------------
.proc sys_chdir
        tax
        ldy #.loword(pbuf)
        TRY path_norm
        TRY need_root
        ldx #.loword(pbuf)
        lda #0
        TRY vfs_walk
        lda w_type
        cmp #4
        beq @dir
        lda #E_NOTDIR
        rts
@dir:   ldx #.loword(pbuf)
        ldy #.loword(cwd_path)
        jsr strcpy
        MOV32 cwd_ino, w_cur
        lda #E_OK
        rts
.endproc

.proc sys_getcwd
        sta s_buf
        stx s_n
        ldx #.loword(cwd_path)
        jsr strlen
        cmp s_n
        bcc @fits
        lda #E_INVAL
        rts
@fits:  pha
        ldx #.loword(cwd_path)
        ldy s_buf
        jsr strcpy
        pla
        rts
.endproc

; ---- 48 sync() ------------------------------------------------------------------
.proc sys_sync
        lda nv_mounted
        beq @done
        lda nv_ro
        bne @done
        jsr nv_sync
        jsr txn_end
        cmp #0
        bmi @out
@done:  lda #E_OK
@out:   rts
.endproc

; ---- 49 opendir(path) · 50 readdir(fd, ent) -------------------------------------
.proc sys_opendir
        tax
        ldy #.loword(pbuf)
        TRY path_norm
        TRY need_root
        ldx #.loword(pbuf)
        lda #0
        TRY vfs_walk
        lda w_type
        cmp #4
        beq @dir
        lda #E_NOTDIR
        rts
@dir:   TRY file_alloc
        lda #F_DIR
        sta f_kind,x
        lda w_cur
        sta f_ino_lo,x
        lda w_cur+2
        sta f_ino_hi,x
        stz f_flags,x
        jmp fd_bind_new
.endproc

.proc sys_readdir
        stx p2
        stx s_buf
        TRY fd_get
        stx s_file
        lda f_kind,x
        cmp #F_DIR
        beq @dir
        lda #E_NOTDIR
        rts
@dir:   lda f_ino_lo,x
        sta d_dir
        lda f_ino_hi,x
        sta d_dir+2
        lda f_off_lo,x          ; the cookie: slot in the low word, block in the high
        sta rd_slot
        lda f_off_hi,x
        sta rd_lb
        jsr dir_next
        ldx s_file
        pha
        lda rd_slot
        sta f_off_lo,x
        lda rd_lb
        sta f_off_hi,x
        pla
        rts
.endproc

; ---- 64 time() · 65 uptime() ----------------------------------------------------
.proc sys_time                  ; no part of this board holds a wall clock (Q88)
        lda #0
        tax
        rts
.endproc

.proc sys_uptime                ; milliseconds since the counter started (L.15)
        jsr clock_read
        lda now
        ldx now+2
        rts
.endproc

.proc sys_nosys
        lda #E_NOSYS
        rts
.endproc

; ---- the numbered service table (D90, LB1.10) -----------------------------------
; Kernel services are reached through this table and through nothing else.
; The COP dispatcher of the user-mode build indexes it with the number in Y;
; this build calls the handlers directly and carries the table so that the
; numbering has exactly one home.
.macro SVC number, handler
        .assert (* - svc_table) / 2 <= number, error, "service table out of order"
        .repeat number - (* - svc_table) / 2
        .word sys_nosys
        .endrepeat
        .word handler
.endmacro

svc_table:
        SVC 0,  sys_nosys       ; reserved: a zero number is a stub bug
        SVC 1,  sys_nosys       ; exit      -- CON-04
        SVC 2,  sys_nosys       ; spawn     -- CON-04
        SVC 3,  sys_nosys       ; wait      -- CON-04
        SVC 4,  sys_nosys       ; getpid    -- CON-04
        SVC 32, sys_open
        SVC 33, sys_close
        SVC 34, sys_read
        SVC 35, sys_write
        SVC 36, sys_lseek
        SVC 37, sys_ioctl
        SVC 38, sys_dup
        SVC 39, sys_dup2
        SVC 40, sys_stat
        SVC 41, sys_fstat
        SVC 42, sys_unlink
        SVC 43, sys_rename
        SVC 44, sys_mkdir
        SVC 45, sys_rmdir
        SVC 46, sys_chdir
        SVC 47, sys_getcwd
        SVC 48, sys_sync
        SVC 49, sys_opendir
        SVC 50, sys_readdir
        SVC 64, sys_time
        SVC 65, sys_uptime
        SVC 127, sys_nosys
svc_table_end:
