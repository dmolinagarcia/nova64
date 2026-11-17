; sh_main.s — the shell: the loop, the prompt, dispatch (CN1.19).
;
; A parser over file descriptors. Screen, keyboard and files are descriptors
; and everything below the libc surface is the kernel's (CN1.2). It prints
; through write() and a few helpers of its own, not through stdio (CN1.18);
; every buffer is static and there is no malloc (CN1.24). Error text lives
; here: the kernel carries no message strings (CN1.21).

SH_LINE_MAX = 256
SH_ARGV_MAX = 16
SH_E_NOCMD  = $FF80             ; shell statuses outside the kernel's error space
SH_E_USAGE  = $FF81
sh_fn       = $2A               ; direct page: the vector a built-in is called through

.segment "BSS"
sh_line:   .res SH_LINE_MAX
sh_argv:   .res (SH_ARGV_MAX+1)*2
sh_argc:   .res 2
sh_redir:  .res 2               ; path of the redirection target, or 0
sh_append: .res 2
sh_out:    .res 2               ; the output descriptor after redirection
sh_werr:   .res 2               ; the first error a write to it returned
sh_earg:   .res 2               ; the argument an error is about, or 0
sh_status: .res 2
sh_ch:     .res 2
sh_path:   .res PATH_MAX+NAME_MAX+2
sh_st:     .res ST_SIZEOF
sh_st2:    .res ST_SIZEOF
sh_de:     .res DE_SIZEOF       ; the caller provides the directory entry (Y1.12)
sh_buf:    .res 2048            ; one NVFS block (CN1.21)
.segment "CODE"

.proc sh_main
        jsr sh_motd
@loop:  jsr sh_prompt
        lda #SH_LINE_MAX-1
        sta r0
        lda #0
        ldx #.loword(sh_line)
        jsr read
        cmp #1
        bmi @loop               ; the login shell ignores end of file
        tax
        A8
        stz sh_line,x
        lda sh_line-1,x
        A16
        and #$00FF
        cmp #$000A
        beq @parse
@drain: lda #SH_LINE_MAX-1      ; no newline: the line was longer than the buffer.
        sta r0                  ; An error, never a silent truncation (CN1.20).
        lda #0
        ldx #.loword(sh_line)
        jsr read
        cmp #1
        bmi @long
        tax
        lda sh_line-1,x
        and #$00FF
        cmp #$000A
        bne @drain
@long:  ldx #.loword(msg_long)
        jsr sh_error
        bra @loop
@parse: jsr sh_parse
        cmp #0
        beq @loop
        jsr sh_run
        bra @loop
.endproc

.proc sh_motd                   ; prints /etc/motd if present
        lda #1
        sta sh_out
        lda #.loword(path_motd)
        ldx #NV_O_RDONLY
        jsr open
        cmp #0
        bmi @none
        jsr sh_copy
@none:  rts
.endproc

; copy descriptor A to sh_out until it ends, then close it -> a status
.proc sh_copy
        pha
@more:  lda #2048
        sta r0
        pla
        pha
        ldx #.loword(sh_buf)
        jsr read
        cmp #1
        bmi @end
        sta r0
        lda sh_out
        ldx #.loword(sh_buf)
        jsr write
        cmp #0
        bmi @end
        jsr sh_interrupted
        cmp #0
        beq @more
        lda #0
@end:   tax
        pla
        phx
        jsr close
        pla
        rts
.endproc

.proc sh_prompt                 ; getcwd() + "> ", or "(no root)" in degraded mode (CN1.7)
        lda #1
        sta sh_out
        lda #.loword(path_root)
        ldx #.loword(sh_st)
        jsr stat
        cmp #0
        bpl @cwd
        ldx #.loword(msg_noroot)
        jsr sh_puts
        bra @mark
@cwd:   lda #.loword(sh_path)
        ldx #PATH_MAX
        jsr getcwd
        ldx #.loword(sh_path)
        jsr sh_puts
@mark:  ldx #.loword(msg_prompt)
        jmp sh_puts
.endproc

; run the parsed command: a built-in, else sh_exec()
.proc sh_run
        lda #1
        sta sh_out
        stz sh_werr
        stz sh_earg
        lda sh_redir
        beq @find
        ldx #NV_O_WRONLY | NV_O_CREAT | NV_O_TRUNC
        lda sh_append
        beq @open
        ldx #NV_O_WRONLY | NV_O_CREAT | NV_O_APPEND
@open:  lda sh_redir
        jsr open
        cmp #0
        bpl @opened
        sta sh_status
        lda sh_redir
        sta sh_earg
        ldx #.loword(msg_sh)
        jmp sh_report
@opened:
        sta sh_out

@find:  stz r4
@next:  ldx r4
        lda bi_table,x
        beq @extern
        tay
        ldx sh_argv
        jsr strcmp
        beq @builtin
        lda r4
        clc
        adc #4
        sta r4
        bra @next
@builtin:
        ldx r4
        lda bi_table+2,x
        sta sh_fn
        lda sh_argc
        ldx #.loword(sh_argv)
        jsr @call
        bra @ran
@extern:
        lda sh_argc
        ldx #.loword(sh_argv)
        jsr sh_exec
@ran:   sta sh_status
        lda sh_out
        cmp #1
        beq @status
        jsr close
        lda #1
        sta sh_out
@status:
        lda sh_status
        bne @report
        lda sh_werr             ; the command was content; was its output?
        beq @done
        sta sh_status
        lda sh_redir
        sta sh_earg
@report:
        ldx sh_argv
        jmp sh_report
@done:  rts
@call:  jmp (sh_fn)
.endproc

; print "X: arg: message" for sh_status on descriptor 2
.proc sh_report
        lda #2
        sta sh_out
        jsr sh_puts
        ldx #.loword(msg_colon)
        jsr sh_puts
        lda sh_earg
        beq @msg
        tax
        jsr sh_puts
        ldx #.loword(msg_colon)
        jsr sh_puts
@msg:   lda sh_status
        cmp #SH_E_NOCMD
        bne @usage
        ldx #.loword(msg_nocmd)
        bra @put
@usage: cmp #SH_E_USAGE
        bne @code
        ldx #.loword(msg_usage)
        bra @put
@code:  eor #$FFFF
        inc a
        cmp #E_LAST+1
        bcc @known
        lda #0
@known: asl a
        tax
        lda errtab,x
        tax
@put:   jsr sh_puts
        jsr sh_nl
        lda #1
        sta sh_out
        rts
.endproc

.proc sh_error                  ; X = message: "sh: message" on descriptor 2
        phx
        lda #2
        sta sh_out
        ldx #.loword(msg_sh)
        jsr sh_puts
        ldx #.loword(msg_colon)
        jsr sh_puts
        plx
        jsr sh_puts
        jsr sh_nl
        lda #1
        sta sh_out
        rts
.endproc

; ---- output helpers ------------------------------------------------------------
.proc sh_puts                   ; X = string -> sh_out
        jsr strlen
        ; fall through
.endproc
.proc sh_write                  ; X = bytes, A = count -> sh_out
        cmp #0
        beq @done
        sta r0
        lda sh_out
        jsr write
        cmp #0
        bpl @done
        ldx sh_werr
        bne @done
        sta sh_werr
@done:  rts
.endproc

.proc sh_putc                   ; A = character
        sta sh_ch
        ldx #.loword(sh_ch)
        lda #1
        jmp sh_write
.endproc

.proc sh_nl
        lda #$0A
        jmp sh_putc
.endproc

.proc sh_putu                   ; r0:r1 = unsigned, decimal
        jsr u32toa
        jmp sh_puts
.endproc

.proc sh_putw                   ; A = unsigned 16-bit
        sta r0
        stz r1
        jmp sh_putu
.endproc

path_motd:  .byte "/etc/motd", 0
path_root:  .byte "/", 0
msg_prompt: .byte "> ", 0
msg_noroot: .byte "(no root)", 0
msg_sh:     .byte "sh", 0
msg_colon:  .byte ": ", 0
msg_long:   .byte "line too long", 0
msg_nocmd:  .byte "not found", 0
msg_usage:  .byte "missing operand", 0

; the shell's own table of what each code means (CN1.21)
errtab: .word e_unknown
        .word e01, e02, e03, e04, e05, e06, e07, e08, e09, e10
        .word e11, e12, e13, e14, e15, e16, e17, e18, e19, e20
        .word e21, e22, e23, e24, e25, e26, e27, e28, e29, e30
e_unknown: .byte "error", 0
e01: .byte "I/O error", 0
e02: .byte "not found", 0
e03: .byte "exists", 0
e04: .byte "not a directory", 0
e05: .byte "is a directory", 0
e06: .byte "directory not empty", 0
e07: .byte "invalid argument", 0
e08: .byte "no space left", 0
e09: .byte "no inodes left", 0
e10: .byte "read-only file system", 0
e11: .byte "busy", 0
e12: .byte "too many symbolic links", 0
e13: .byte "name too long", 0
e14: .byte "too many open files", 0
e15: .byte "bad descriptor", 0
e16: .byte "cross-device", 0
e17: .byte "file system corrupt", 0
e18: .byte "unsupported feature", 0
e19: .byte "not supported", 0
e20: .byte "file too large", 0
e21: .byte "no root file system", 0
e22: .byte "out of memory", 0
e23: .byte "bad address", 0
e24: .byte "not executable", 0
e25: .byte "no child", 0
e26: .byte "no such process", 0
e27: .byte "not a tty", 0
e28: .byte "broken pipe", 0
e29: .byte "no such call", 0
e30: .byte "argument list too long", 0
