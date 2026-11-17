; tty.s — the console device: a line discipline between two backends (CN1.9).
;
; Line editing lives here and not in the shell (D104), so every program gets
; complete lines for nothing. The discipline is CN1.12's table; raw mode is
; defined although nothing in the console uses it yet.
;
; Input is polled: tty_poll moves pending hardware events into the discipline
; and spots the interrupt character. That is valid only while the system is
; single-threaded, and it goes when read() can block on the HID interrupt
; (Q180, CON-04).

TTY_LINE_MAX = 255              ; bytes, plus the newline that ends the line

.segment "BSS"
tty_mode:  .res 2
tty_len:   .res 2               ; bytes in the line being edited
tty_avail: .res 2               ; bytes of a finished line still unread
tty_rpos:  .res 2
tty_eof:   .res 2
tty_intr:  .res 2               ; Ctrl-C seen: a flag in this build, a signal in the next (CN1.13)
tty_fg:    .res 2
tty_ptr:   .res 2
tty_cnt:   .res 2
tty_done:  .res 2
tty_line:  .res TTY_LINE_MAX+1
.segment "CODE"

.proc tty_init
        stz tty_mode
        stz tty_len
        stz tty_avail
        stz tty_rpos
        stz tty_eof
        stz tty_intr
        lda #E_OK
        rts
.endproc

.proc tty_echo                  ; X = string
        jmp kputs
.endproc

; move pending key events into the discipline
.proc tty_poll
@next:  lda tty_avail           ; a finished line waits to be read: leave the rest queued
        beq @poll
        lda tty_mode
        beq @out
@poll:  jsr kbd_poll
        bcs @byte
@out:   rts
@byte:  sta r4
        lda tty_mode
        beq @canon
        ldx tty_avail           ; raw: every byte is available at once, no echo
        cpx #TTY_LINE_MAX
        bcs @next
        A8
        lda r4
        sta tty_line,x
        A16
        inc tty_avail
        bra @next

@canon: lda r4
        cmp #$000A
        beq @enter
        cmp #$0008
        beq @erase
        cmp #$007F
        beq @erase
        cmp #$0015
        beq @kill
        cmp #$0003
        beq @intr
        cmp #$0004
        beq @eof
        cmp #$0020
        bcc @next               ; other controls: dropped
        cmp #$0080
        bcc @print
        cmp #$00A0
        bcc @next
@print: ldx tty_len
        cpx #TTY_LINE_MAX
        bcs @next               ; full: printable characters are dropped
        A8
        sta tty_line,x
        A16
        inc tty_len
        jsr kputc
        bra @next

@erase: lda tty_len
        beq @next
        dec tty_len
        ldx #.loword(str_erase)
        jsr tty_echo
        jmp @next

@kill:  lda tty_len             ; Ctrl-U: the line discarded and erased on screen
        beq @tonext
        dec tty_len
        ldx #.loword(str_erase)
        jsr tty_echo
        bra @kill

@enter: ldx tty_len
        A8
        lda #$0A
        sta tty_line,x
        A16
        inx
        stx tty_avail
        stz tty_rpos
        stz tty_len
        lda #$0A
        jsr kputc
        rts

@intr:  stz tty_len             ; Ctrl-C: the line discarded, ^C echoed, the interrupt raised
        ldx #.loword(str_intr)
        jsr tty_echo
        lda #1
        sta tty_intr
        rts

@eof:   lda tty_len             ; Ctrl-D on an empty line: read() returns 0
        bne @tonext
        lda #1
        sta tty_eof
        rts
@tonext:
        jmp @next
.endproc
str_erase: .byte $08, ' ', $08, 0
str_intr:  .byte "^C", $0A, 0

; ---- the five functions of J.4 ------------------------------------------------

; A = buffer, X = count -> bytes read. At most one line; a line may be consumed
; over several reads.
.proc tty_read
        sta tty_ptr
        stx tty_cnt
        stz tty_intr
        stz tty_eof
        txa
        bne @wait
        rts
@wait:  jsr tty_poll
        lda tty_avail
        bne @copy
        lda tty_eof
        ora tty_intr
        beq @wait
        lda #0                  ; end of file, or the line was interrupted
        rts
@copy:  cmp tty_cnt
        bcc @n
        lda tty_cnt
@n:     sta tty_done
        lda tty_ptr
        sta p0
        ldx tty_rpos
        ldy #0
        A8
@byte:  lda tty_line,x
        sta (p0),y
        inx
        iny
        cpy tty_done
        bne @byte
        A16
        stx tty_rpos
        lda tty_avail
        sec
        sbc tty_done
        sta tty_avail
        bne @ret
        stz tty_rpos
@ret:   lda tty_done
        rts
.endproc

; A = buffer, X = count -> bytes written
.proc tty_write
        sta tty_ptr
        stx tty_cnt
        stx tty_done
        txa
        beq @done
@byte:  ldx tty_ptr
        lda a:0,x
        jsr con_putc
        inc tty_ptr
        dec tty_cnt
        bne @byte
        jsr con_cursor
@done:  lda tty_done
        rts
.endproc

; A = operation, X = argument
.proc tty_ioctl
        stx p0
        cmp #TTY_GETMODE
        beq @getmode
        cmp #TTY_SETMODE
        beq @setmode
        cmp #TTY_GETSIZE
        beq @getsize
        cmp #TTY_CLEAR
        beq @clear
        cmp #TTY_SETFG
        beq @setfg
        and #$FF00              ; another device's class: not meant for a tty
        cmp #$5400
        beq @inval
        lda #E_NOTTY
        rts
@inval: lda #E_INVAL
        rts
@getmode:
        lda tty_mode
        sta (p0)
        bra @ok
@setmode:
        lda (p0)
        cmp #TTY_MODE_RAW+1
        bcs @inval
        sta tty_mode
        stz tty_len
        stz tty_avail
        stz tty_rpos
        bra @ok
@getsize:
        lda con_cols
        sta (p0)
        ldy #2
        lda con_rows
        sta (p0),y
        bra @ok
@clear: jsr con_clear
        bra @ok
@setfg: stx tty_fg              ; the foreground PID: nothing to signal in this build
@ok:    lda #E_OK
        rts
.endproc

.proc tty_irq                   ; unused while input is polled
        rts
.endproc
