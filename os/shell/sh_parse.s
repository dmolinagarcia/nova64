; sh_parse.s — the tokeniser and redirection (CN1.20).
;
; Splits on spaces and tabs and cuts tokens in place, with no copies: "..."
; groups characters into one token and \ escapes the next one; > file and
; >> file redirect output, and only the last redirection counts. More than 16
; arguments is an error, never a silent truncation. There are no pipes, no
; ';', no '&', no '<', no variables and no globbing.

.segment "BSS"
sp_in:     .res 2               ; read index
sp_out:    .res 2               ; write index: never ahead of the read index
sp_quote:  .res 2
sp_redir:  .res 2               ; a '>' ended the last token, or stands alone
sp_append: .res 2
sp_end:    .res 2
sp_any:    .res 2               ; the token had something in it, if only ""
.segment "CODE"

; sh_line -> A = argc with sh_argv filled, or 0 with a message printed
.proc sh_parse
        stz sp_in
        stz sp_out
        stz sp_redir
        stz sp_end
        stz sh_argc
        stz sh_redir
@next:  lda sp_end
        jne @done
        jsr @blank
        lda sp_redir
        bne @redir
        ldx sp_in
        lda sh_line,x
        and #$00FF
        jeq @done
        cmp #$000A
        jeq @done
        cmp #'>'
        bne @arg
        inc sp_in
@redir: stz sp_redir            ; '>' or '>>', then the target
        stz sp_append
        ldx sp_in
        lda sh_line,x
        and #$00FF
        cmp #'>'
        bne @target
        inc sp_append
        inc sp_in
@target:
        jsr @blank
        lda sp_out
        pha
        jsr @word
        pla
        ldx sp_any
        bne @hasred
        ldx #.loword(msg_syntax)
        jmp @fail
@hasred:
        clc
        adc #.loword(sh_line)
        sta sh_redir            ; only the last redirection counts
        lda sp_append
        sta sh_append
        jmp @next
@arg:   lda sh_argc
        cmp #SH_ARGV_MAX
        bcc @room
        ldx #.loword(msg_toomany)
        jmp @fail
@room:  asl a
        tax
        lda sp_out
        clc
        adc #.loword(sh_line)
        sta sh_argv,x
        inc sh_argc
        jsr @word
        lda sp_quote
        jeq @next
        ldx #.loword(msg_quote)
        jmp @fail
@done:  lda sh_argc
        asl a
        tax
        stz sh_argv,x           ; argv[argc] = NULL
        lda sh_argc
        rts
@fail:  jsr sh_error
        lda #0
        rts

@blank: ldx sp_in               ; skip spaces and tabs
        lda sh_line,x
        and #$00FF
        cmp #' '
        beq @skip
        cmp #$0009
        beq @skip
        rts
@skip:  inc sp_in
        bra @blank

        ; cut one token in place. The byte that ends it is consumed before the
        ; terminator is written, since the two can be the same byte.
@word:  stz sp_quote
        stz sp_any
@char:  ldx sp_in
        lda sh_line,x
        and #$00FF
        beq @eol
        cmp #$000A
        beq @eol
        cmp #'"'
        bne @plain
        lda sp_quote
        eor #$0001
        sta sp_quote
        inc sp_any
        inc sp_in
        bra @char
@plain: cmp #$005C
        bne @bare
        inc sp_in               ; the next character, whatever it is
        ldx sp_in
        lda sh_line,x
        and #$00FF
        beq @eol
        cmp #$000A
        beq @eol
        bra @keep
@bare:  ldy sp_quote
        bne @keep
        cmp #' '
        beq @space
        cmp #$0009
        beq @space
        cmp #'>'
        bne @keep
        inc sp_redir
@space: inc sp_in
        bra @cut
@keep:  ldx sp_out
        A8
        sta sh_line,x
        A16
        inc sp_out
        inc sp_in
        inc sp_any
        bra @char
@eol:   inc sp_end
@cut:   ldx sp_out
        A8
        stz sh_line,x
        A16
        inc sp_out
        rts
.endproc

msg_syntax:  .byte "syntax error", 0
msg_toomany: .byte "too many arguments", 0
msg_quote:   .byte "unterminated quote", 0
