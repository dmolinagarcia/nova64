; sh_exec.s — the one shell source that differs between the two builds (CN1.26).
;
; Monolithic: there are no processes, so an external command is "not found",
; and Ctrl-C is a flag the long-running built-ins poll for (CN1.13). The
; user-mode build replaces this file with the real launcher -- spawn, TTY_SETFG,
; wait -- and an sh_interrupted that never fires, because there the tty sends
; TERM to the foreground process instead.

; A = argc, X = argv -> a shell status
.proc sh_exec
        lda #SH_E_NOCMD
        rts
.endproc

; -> A not zero if the user pressed Ctrl-C since the last call
.proc sh_interrupted
        jsr tty_poll
        lda tty_intr
        stz tty_intr
        rts
.endproc
