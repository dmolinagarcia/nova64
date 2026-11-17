; kbd.s — the console's input backend: Helium's HID queue and the kernel's
; keymap (CN1.11, D106).
;
; The EC sends raw key events; everything that turns a key into a byte is
; here. Releases and non-keyboard devices are dropped, which the console can
; afford because every event carries the modifier state. Bytes leave in the
; console encoding, ISO-8859-15 (Q182).
;
; The queue is popped by an explicit write and never by a read (CN1.28, D92).

MOD_CTRL  = $11                 ; HID modifier bits: either Control
MOD_SHIFT = $22                 ; either Shift
MOD_ALTGR = $40                 ; right Alt

KEYMAP_LEN = $68                ; usages $00-$67

DEAD_GRAVE = 1                  ; markers in the keymap tables
DEAD_ACUTE = 2
DEAD_CIRC  = 3
DEAD_DIAER = 4

.segment "BSS"
kbd_caps:  .res 2               ; Caps Lock, tracked here: the LED has no path back yet
kbd_dead:  .res 2               ; a dead key waiting for its letter
kbd_pend:  .res 2               ; a byte owed after a dead key that did not combine
kbd_usage: .res 2
kbd_mods:  .res 2
km_base:   .res 2               ; the loaded keymap: three tables and the compositions
km_shift:  .res 2
km_altgr:  .res 2
km_dead:   .res 2
.segment "CODE"

; E_OK, or E_IO when Helium reports no HID queue
.proc kbd_init
        stz kbd_caps
        stz kbd_dead
        stz kbd_pend
        lda #.loword(keymap_es_base)    ; the first keymap is the keyboard's own: Spanish ISO
        sta km_base
        lda #.loword(keymap_es_shift)
        sta km_shift
        lda #.loword(keymap_es_altgr)
        sta km_altgr
        lda #.loword(keymap_es_dead)
        sta km_dead
        lda f:SIB+SIB_DEVICES
        and #$0002
        bne @drain
        lda #E_IO
        rts
@drain: lda f:HID_STATUS
        and #$0001
        beq @empty
        sta f:HID_POP
        bra @drain
@empty: lda #$0002              ; clear the sticky overflow flag
        sta f:HID_CTRL
        lda #E_OK
        rts
.endproc

; -> carry set and A = a byte, or carry clear when the queue has nothing more
.proc kbd_poll
        lda kbd_pend
        beq @next
        stz kbd_pend
        sec
        rts
@next:  lda f:HID_STATUS
        and #$0001
        bne @event
        clc
        rts
@event: lda f:HID_EV_FLAGS
        sta r0
        lda f:HID_EV_KEY
        sta f:HID_POP
        tax
        lda r0
        and #$00F1              ; a keyboard, and a press
        cmp #$0001
        bne @next
        txa
        and #$00FF
        sta kbd_usage
        txa
        xba
        and #$00FF
        sta kbd_mods

        lda kbd_usage
        cmp #$39                ; Caps Lock
        bne @notcaps
        lda kbd_caps
        eor #$0001
        sta kbd_caps
        bra @next
@notcaps:
        cmp #KEYMAP_LEN
        bcs @next               ; arrows, function keys: nothing until the ANSI subset exists
        lda kbd_mods
        and #MOD_CTRL
        beq @table
        lda kbd_usage           ; Ctrl with a letter: $01-$1A
        cmp #$04
        bcc @table
        cmp #$1E
        bcs @table
        sec
        sbc #3
        sec
        rts

@table: lda km_base
        sta p0
        lda kbd_mods
        and #MOD_ALTGR
        beq @shift
        lda km_altgr
        sta p0
        bra @look
@shift: lda kbd_mods
        and #MOD_SHIFT
        beq @look
        lda km_shift
        sta p0
@look:  ldy kbd_usage
        lda (p0),y
        and #$00FF
        jeq @next               ; this key gives nothing
        cmp #DEAD_DIAER+1
        bcs @char
        sta kbd_dead            ; a dead key: wait for the letter
        jmp @next

@char:  sta r1
        lda kbd_caps            ; Caps Lock reaches the letters of the base table only
        beq @dead
        lda kbd_mods
        and #MOD_SHIFT | MOD_ALTGR
        bne @dead
        lda r1
        jsr is_lower
        bcc @dead
        lda r1
        eor #$0020
        sta r1
@dead:  lda kbd_dead
        bne @compose
        lda r1
        sec
        rts

        ; the composition table: dead, letter, result ... 0
@compose:
        lda km_dead
        sta p0
        ldy #0
@scan:  lda (p0),y
        and #$00FF
        beq @nomatch
        cmp kbd_dead
        bne @skip
        iny
        lda (p0),y
        dey
        and #$00FF
        cmp r1
        bne @skip
        iny
        iny
        lda (p0),y
        and #$00FF
        stz kbd_dead
        sec
        rts
@skip:  iny
        iny
        iny
        bra @scan
@nomatch:                       ; no such letter: the accent on its own, then the byte
        ldx kbd_dead
        stz kbd_dead
        lda r1
        sta kbd_pend
        lda dead_alone-1,x
        and #$00FF
        sec
        rts
.endproc

; carry set if A is a lower-case letter of ISO-8859-15
.proc is_lower
        cmp #'a'
        bcc @no
        cmp #'z'+1
        bcc @yes
        cmp #$E0
        bcc @no
        cmp #$F7
        beq @no
        cmp #$FF
        bcs @no
@yes:   sec
        rts
@no:    clc
        rts
.endproc
