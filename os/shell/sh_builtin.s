; sh_builtin.s — the built-in commands (CN1.21).
;
; A built-in is entered with A = argc and X = argv, writes to the descriptor
; sh_out, and returns 0 or an error code with sh_earg naming the argument it
; is about; the shell prints "name: arg: message".

.segment "BSS"
bi_i:    .res 2
bi_fd:   .res 2
bi_fd2:  .res 2
bi_long: .res 2
bi_n:    .res 2
bi_src:  .res 2
bi_dst:  .res 2
.segment "CODE"

bi_table:
        .word n_help, bi_help,   n_echo, bi_echo,   n_clear, bi_clear,   n_sysinfo, bi_sysinfo
        .word n_pwd, bi_pwd,     n_cd, bi_cd,       n_ls, bi_ls,         n_cat, bi_cat
        .word n_stat, bi_stat,   n_mkdir, bi_mkdir, n_rmdir, bi_rmdir,   n_rm, bi_rm
        .word n_mv, bi_mv,       n_cp, bi_cp,       n_sync, bi_sync
        .word 0
n_help:    .byte "help", 0
n_echo:    .byte "echo", 0
n_clear:   .byte "clear", 0
n_sysinfo: .byte "sysinfo", 0
n_pwd:     .byte "pwd", 0
n_cd:      .byte "cd", 0
n_ls:      .byte "ls", 0
n_cat:     .byte "cat", 0
n_stat:    .byte "stat", 0
n_mkdir:   .byte "mkdir", 0
n_rmdir:   .byte "rmdir", 0
n_rm:      .byte "rm", 0
n_mv:      .byte "mv", 0
n_cp:      .byte "cp", 0
n_sync:    .byte "sync", 0

.proc bi_usage
        stz sh_earg
        lda #SH_E_USAGE
        rts
.endproc

; A = argument number -> X = the argument, also left in sh_earg
.proc bi_arg
        asl a
        tax
        lda sh_argv,x
        sta sh_earg
        tax
        rts
.endproc

; ---- CON-01: no files needed ----------------------------------------------------
.proc bi_help
        ldx #.loword(@text)
        jsr sh_puts
        lda #0
        rts
@text:  .byte "help  echo  clear  sysinfo", $0A
        .byte "pwd  cd [dir]  ls [-l] [path]  cat file...  stat path", $0A
        .byte "mkdir dir...  rmdir dir...  rm file...  mv from to  cp from to  sync", $0A
        .byte "output: > file   >> file", $0A, 0
.endproc

.proc bi_echo
        lda #1
        sta bi_i
@next:  lda bi_i
        cmp sh_argc
        bcs @end
        jsr bi_arg
        jsr sh_puts
        inc bi_i
        lda bi_i
        cmp sh_argc
        bcs @end
        lda #' '
        jsr sh_putc
        bra @next
@end:   jsr sh_nl
        stz sh_earg
        lda #0
        rts
.endproc

.proc bi_clear                  ; TTY_CLEAR, while there are no escape sequences
        stz r0
        lda #0
        ldx #TTY_CLEAR
        jmp ioctl
.endproc

; the system information block: CPU, platform, register map, text geometry
.proc bi_sysinfo
        ldx #.loword(@cpu)
        jsr sh_puts
        lda f:NV_SYSINFO+SIB_CPU
        bne @argon
        ldx #.loword(@wdc)
        bra @cpun
@argon: ldx #.loword(@arg)
@cpun:  jsr sh_puts
        ldx #.loword(@plat)
        jsr sh_puts
        lda f:NV_SYSINFO+SIB_PLATFORM
        cmp #$00E0
        bne @board
        ldx #.loword(@emu)
        jsr sh_puts
        bra @map
@board: jsr sh_putw
@map:   ldx #.loword(@reg)
        jsr sh_puts
        lda f:NV_SYSINFO+SIB_REGMAP
        jsr sh_putw
        ldx #.loword(@bios)
        jsr sh_puts
        lda f:NV_SYSINFO+SIB_BIOS_VER
        jsr sh_putw
        ldx #.loword(@mem)
        jsr sh_puts
        lda f:NV_SYSINFO+SIB_RAM_FRAMES
        sta r0
        lda f:NV_SYSINFO+SIB_RAM_FRAMES+2
        sta r1
        asl r0                  ; 2 KB frames
        rol r1
        jsr sh_putu
        ldx #.loword(@text)
        jsr sh_puts
        lda f:NV_SYSINFO+SIB_COLS
        jsr sh_putw
        ldx #.loword(@by)
        jsr sh_puts
        lda f:NV_SYSINFO+SIB_ROWS
        jsr sh_putw
        ldx #.loword(@boot)
        jsr sh_puts
        lda f:NV_SYSINFO+SIB_BOOT_SRC
        cmp #2
        beq @card
        ldx #.loword(@stage)
        jsr sh_puts
        bra @up
@card:  ldx #.loword(@part)
        jsr sh_puts
        lda f:NV_SYSINFO+SIB_BOOT_PART
        jsr sh_putw
@up:    jsr sh_nl
        lda #0
        rts
@cpu:   .byte "cpu:        ", 0
@wdc:   .byte "W65C816S", 0
@arg:   .byte "Argon", 0
@plat:  .byte $0A, "platform:   ", 0
@emu:   .byte "reference emulator", 0
@reg:   .byte $0A, "registers:  map version ", 0
@bios:  .byte $0A, "bios:       version ", 0
@mem:   .byte $0A, "memory:     ", 0
@text:  .byte " KB", $0A, "text:       ", 0
@by:    .byte " x ", 0
@boot:  .byte $0A, "boot:       ", 0
@stage: .byte "staged image", 0
@part:  .byte "card, partition ", 0
.endproc

; ---- CON-02: a read-only prompt -------------------------------------------------
.proc bi_pwd
        lda #.loword(sh_path)
        ldx #PATH_MAX
        jsr getcwd
        cmp #0
        bmi @out
        ldx #.loword(sh_path)
        jsr sh_puts
        jsr sh_nl
        lda #0
@out:   rts
.endproc

.proc bi_cd                     ; defaults to /; a built-in in both builds
        lda #.loword(path_root)
        sta sh_earg
        lda sh_argc
        cmp #2
        bcc @go
        lda #1
        jsr bi_arg
@go:    lda sh_earg
        jmp chdir
.endproc

; X = path -> X = its last component
.proc basename
        stx r0
@next:  lda a:0,x
        and #$00FF
        beq @end
        inx
        cmp #'/'
        bne @next
        lda a:0,x               ; a trailing slash names nothing new
        and #$00FF
        beq @next
        stx r0
        bra @next
@end:   ldx r0
        rts
.endproc

; X = directory, Y = name -> sh_path = directory "/" name
.proc sh_join
        phy
        ldy #.loword(sh_path)
        jsr strcpy
        ldx #.loword(sh_path)
        jsr strlen
        tax
        beq @slash
        lda sh_path-1,x
        and #$00FF
        cmp #'/'
        beq @name
@slash: lda #'/'
        sta sh_path,x           ; the high byte lands where the name is about to go
        inx
@name:  txa
        clc
        adc #.loword(sh_path)
        tay
        plx
        jmp strcpy
.endproc

; print the name in sh_de, with a slash if it is a directory
.proc ls_name
        ldx #.loword(sh_de+DE_NAME)
        jsr sh_puts
        lda sh_de+DE_TYPE
        and #$00FF
        cmp #VDIR
        bne @done
        lda #'/'
        jsr sh_putc
@done:  rts
.endproc

; one line of ls -l for the nv_stat in sh_st: type, size, then the name
.proc ls_long
        lda sh_st+ST_MODE
        and #S_IFMT
        ldx #'-'
        cmp #S_IFDIR
        bne @lnk
        ldx #'d'
@lnk:   cmp #S_IFLNK
        bne @type
        ldx #'l'
@type:  txa
        jsr sh_putc
        lda sh_st+ST_SIZE
        sta r0
        lda sh_st+ST_SIZE+2
        sta r1
        jsr u32toa
        stx bi_n
        jsr strlen
        sta r3
@pad:   lda r3
        cmp #11
        bcs @num
        lda #' '
        jsr sh_putc
        inc r3
        bra @pad
@num:   ldx bi_n
        jsr sh_puts
        lda #' '
        jsr sh_putc
        lda #' '
        jmp sh_putc
.endproc

.proc bi_ls                     ; ls [-l] [path]
        stz bi_long
        lda #.loword(@dot)
        sta bi_src
        lda #1
        sta bi_i
@args:  lda bi_i
        cmp sh_argc
        bcs @go
        jsr bi_arg
        ldy #.loword(@optl)
        jsr strcmp
        bne @path
        inc bi_long
        bra @argn
@path:  lda sh_earg
        sta bi_src
@argn:  inc bi_i
        bra @args
@go:    lda bi_src
        sta sh_earg
        ldx #.loword(sh_st)
        jsr stat
        cmp #0
        bpl @what
        rts
@what:  lda sh_st+ST_MODE
        and #S_IFMT
        cmp #S_IFDIR
        beq @dir
        lda bi_long             ; a file: list just that
        beq @fname
        jsr ls_long
@fname: ldx bi_src
        jsr sh_puts
        jsr sh_nl
        lda #0
        rts
@dir:   lda bi_src
        jsr opendir
        cmp #0
        bpl @open
        rts
@open:  sta bi_fd
        stz bi_n
@next:  lda bi_fd
        ldx #.loword(sh_de)
        jsr readdir
        cmp #1
        bne @end
        lda bi_long
        beq @short
        ldx bi_src
        ldy #.loword(sh_de+DE_NAME)
        jsr sh_join
        lda #.loword(sh_path)
        ldx #.loword(sh_st)
        jsr stat
        cmp #0
        bmi @end
        jsr ls_long
        jsr ls_name
        jsr sh_nl
        bra @poll
@short: lda bi_n
        beq @first
        lda #' '
        jsr sh_putc
        lda #' '
        jsr sh_putc
@first: inc bi_n
        jsr ls_name
@poll:  jsr sh_interrupted
        cmp #0
        beq @next
        lda #0
@end:   pha
        lda bi_fd
        jsr closedir
        lda bi_long
        bne @ret
        lda bi_n
        beq @ret
        jsr sh_nl
@ret:   pla
        rts
@dot:   .byte ".", 0
@optl:  .byte "-l", 0
.endproc

.proc bi_cat                    ; cat file...
        lda sh_argc
        cmp #2
        bcs @go
        jmp bi_usage
@go:    lda #1
        sta bi_i
@next:  lda bi_i
        cmp sh_argc
        bcs @ok
        jsr bi_arg
        txa
        ldx #NV_O_RDONLY
        jsr open
        cmp #0
        bmi @out
        jsr sh_copy
        cmp #0
        bmi @out
        inc bi_i
        bra @next
@ok:    stz sh_earg
        lda #0
@out:   rts
.endproc

.proc bi_stat                   ; stat path
        lda sh_argc
        cmp #2
        bcs @go
        jmp bi_usage
@go:    lda #1
        jsr bi_arg
        txa
        ldx #.loword(sh_st)
        jsr stat
        cmp #0
        bpl @show
        rts
@show:  ldx #.loword(@name)
        jsr sh_puts
        ldx sh_earg
        jsr sh_puts
        ldx #.loword(@type)
        jsr sh_puts
        lda sh_st+ST_MODE
        and #S_IFMT
        ldx #.loword(@other)
        cmp #S_IFREG
        bne @t1
        ldx #.loword(@file)
@t1:    cmp #S_IFDIR
        bne @t2
        ldx #.loword(@dir)
@t2:    cmp #S_IFLNK
        bne @t3
        ldx #.loword(@link)
@t3:    cmp #S_IFCHR
        bne @t4
        ldx #.loword(@chr)
@t4:    jsr sh_puts
        ldx #.loword(@size)
        jsr sh_puts
        lda sh_st+ST_SIZE
        sta r0
        lda sh_st+ST_SIZE+2
        sta r1
        jsr sh_putu
        ldx #.loword(@blocks)
        jsr sh_puts
        lda sh_st+ST_BLOCKS
        sta r0
        lda sh_st+ST_BLOCKS+2
        sta r1
        jsr sh_putu
        ldx #.loword(@links)
        jsr sh_puts
        lda sh_st+ST_NLINK
        jsr sh_putw
        ldx #.loword(@mode)
        jsr sh_puts
        lda sh_st+ST_MODE       ; three octal digits
        asl a
        asl a
        xba
        and #$0007
        ora #'0'
        jsr sh_putc
        lda sh_st+ST_MODE
        lsr a
        lsr a
        lsr a
        and #$0007
        ora #'0'
        jsr sh_putc
        lda sh_st+ST_MODE
        and #$0007
        ora #'0'
        jsr sh_putc
        ldx #.loword(@inode)
        jsr sh_puts
        lda sh_st+ST_FILE_ID
        sta r0
        lda sh_st+ST_FILE_ID+2
        sta r1
        jsr sh_putu
        jsr sh_nl
        stz sh_earg
        lda #0
        rts
@name:   .byte "  name: ", 0
@type:   .byte $0A, "  type: ", 0
@file:   .byte "file", 0
@dir:    .byte "directory", 0
@link:   .byte "symbolic link", 0
@chr:    .byte "device", 0
@other:  .byte "special", 0
@size:   .byte $0A, "  size: ", 0
@blocks: .byte $0A, "blocks: ", 0
@links:  .byte $0A, " links: ", 0
@mode:   .byte $0A, "  mode: 0", 0
@inode:  .byte $0A, " inode: ", 0
.endproc

; ---- CON-03: a read-write console -----------------------------------------------

; apply the call in sh_fn to every argument; there must be at least one
.proc bi_each
        lda sh_argc
        cmp #2
        bcs @go
        jmp bi_usage
@go:    lda #1
        sta bi_i
@next:  lda bi_i
        cmp sh_argc
        bcs @ok
        jsr bi_arg
        txa
        jsr @call
        cmp #0
        bmi @out
        inc bi_i
        bra @next
@ok:    stz sh_earg
        lda #0
@out:   rts
@call:  jmp (sh_fn)
.endproc

.proc bi_mkdir
        lda #.loword(mkdir)
        sta sh_fn
        jmp bi_each
.endproc
.proc bi_rmdir
        lda #.loword(rmdir)
        sta sh_fn
        jmp bi_each
.endproc
.proc bi_rm
        lda #.loword(unlink)
        sta sh_fn
        jmp bi_each
.endproc

.proc bi_sync
        jmp sync
.endproc

; the two operands of mv and cp -> bi_src and bi_dst. A destination that is an
; existing directory means "into it, under the same name".
.proc bi_operands
        lda sh_argc
        cmp #3
        beq @go
        jmp bi_usage
@go:    lda sh_argv+2
        sta bi_src
        lda sh_argv+4
        sta bi_dst
        ldx #.loword(sh_st2)
        jsr stat
        cmp #0
        bmi @plain
        lda sh_st2+ST_MODE
        and #S_IFMT
        cmp #S_IFDIR
        bne @plain
        ldx bi_src
        jsr basename
        txy
        ldx bi_dst
        jsr sh_join
        lda #.loword(sh_path)
        sta bi_dst
@plain: lda bi_src
        sta sh_earg
        lda #0
        rts
.endproc

.proc bi_mv                     ; mv is rename, and fails across mounts
        jsr bi_operands
        cmp #0
        bne @out
        lda bi_src
        ldx bi_dst
        jsr rename
@out:   rts
.endproc

.proc bi_cp                     ; cp loops with a 2 KB buffer: one NVFS block
        jsr bi_operands
        cmp #0
        beq @go
        rts
@go:    lda bi_src
        ldx #.loword(sh_st)
        jsr stat
        cmp #0
        bpl @src
        rts
@src:   lda sh_st+ST_MODE
        and #S_IFMT
        cmp #S_IFDIR
        bne @same
        lda #E_ISDIR
        rts
@same:  lda bi_dst              ; onto itself would truncate what it is about to read
        ldx #.loword(sh_st2)
        jsr stat
        cmp #0
        bmi @open
        lda sh_st+ST_FILE_ID
        cmp sh_st2+ST_FILE_ID
        bne @open
        lda sh_st+ST_FILE_ID+2
        cmp sh_st2+ST_FILE_ID+2
        bne @open
        lda bi_dst
        sta sh_earg
        lda #E_INVAL
        rts
@open:  lda bi_src
        ldx #NV_O_RDONLY
        jsr open
        cmp #0
        bpl @in
        rts
@in:    sta bi_fd
        lda bi_dst
        sta sh_earg
        ldx #NV_O_WRONLY | NV_O_CREAT | NV_O_TRUNC
        jsr open
        cmp #0
        bpl @out
        pha
        lda bi_fd
        jsr close
        pla
        rts
@out:   sta bi_fd2
@more:  lda #2048
        sta r0
        lda bi_fd
        ldx #.loword(sh_buf)
        jsr read
        cmp #1
        bmi @end
        sta r0
        lda bi_fd2
        ldx #.loword(sh_buf)
        jsr write
        cmp #0
        bmi @end
        jsr sh_interrupted
        cmp #0
        beq @more
        lda #0
@end:   pha
        lda bi_fd
        jsr close
        lda bi_fd2
        jsr close
        pla
        cmp #0
        bmi @ret
        stz sh_earg
        lda #0
@ret:   rts
.endproc
