; keymap.s — the Spanish ISO keymap, in ISO-8859-15 (CN1.11, Q182).
;
; Three tables indexed by HID usage (page $07), $00-$67: base, Shift, AltGr.
; Zero means the key gives nothing. Values 1-4 are dead keys. Enter is $0A,
; Backspace $08, Delete $7F, Tab $09, Escape $1B; the line discipline decides
; what they do. The composition table follows: dead key, letter, result.

.macro KM_ROW0                  ; $00-$03: no event, errors
        .byte 0, 0, 0, 0
.endmacro
.macro KM_TAIL                  ; $39-$67: Caps Lock, function keys, navigation, keypad
        .byte 0                                         ; $39 Caps Lock
        .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0        ; $3A-$45 F1-F12
        .byte 0, 0, 0, 0, 0, 0                          ; $46-$4B
        .byte $7F                                       ; $4C Delete
        .byte 0, 0, 0, 0, 0, 0, 0                       ; $4D-$53 End, PgDn, arrows, Num Lock
        .byte '/', '*', '-', '+', $0A                   ; $54-$58 keypad
        .byte '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '.'     ; $59-$63
.endmacro

keymap_es_base:
        KM_ROW0
        .byte "abcdefghijklmnopqrstuvwxyz"              ; $04-$1D
        .byte "1234567890"                              ; $1E-$27
        .byte $0A, $1B, $08, $09, ' '                   ; $28-$2C
        .byte $27, $A1                                  ; $2D ' · $2E inverted !
        .byte DEAD_GRAVE, '+'                           ; $2F ` · $30 +
        .byte $E7, $E7                                  ; $31, $32: c-cedilla on either report
        .byte $F1                                       ; $33 n-tilde
        .byte DEAD_ACUTE                                ; $34
        .byte $BA                                       ; $35 masculine ordinal
        .byte ",.-"                                     ; $36-$38
        KM_TAIL
        .byte '<', 0, 0, 0                              ; $64-$67
.assert * - keymap_es_base = KEYMAP_LEN, error, "keymap table length"

keymap_es_shift:
        KM_ROW0
        .byte "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        .byte '!', '"', $B7, '$', '%', '&', '/', '(', ')', '='
        .byte $0A, $1B, $08, $09, ' '
        .byte '?', $BF                                  ; ? · inverted ?
        .byte DEAD_CIRC, '*'
        .byte $C7, $C7
        .byte $D1
        .byte DEAD_DIAER
        .byte $AA                                       ; feminine ordinal
        .byte ";:_"
        KM_TAIL
        .byte '>', 0, 0, 0
.assert * - keymap_es_shift = KEYMAP_LEN, error, "keymap table length"

keymap_es_altgr:
        KM_ROW0
        .byte 0, 0, 0, 0, $A4, 0, 0, 0, 0, 0, 0, 0, 0   ; a-m: AltGr+E is the euro sign
        .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0     ; n-z
        .byte '|', '@', '#', '~', $A4, $AC, 0, 0, 0, 0  ; 1-0
        .byte 0, 0, 0, 0, 0
        .byte 0, 0
        .byte '[', ']'
        .byte '}', '}'
        .byte 0
        .byte '{'
        .byte $5C                                       ; backslash
        .byte 0, 0, 0
        KM_TAIL
        .byte 0, 0, 0, 0
.assert * - keymap_es_altgr = KEYMAP_LEN, error, "keymap table length"

; what a dead key gives when no letter combines with it. ISO-8859-15 has no
; spacing acute or diaeresis, so those fall back to the ASCII marks.
dead_alone:
        .byte '`', $27, '^', '"'

keymap_es_dead:
        .byte DEAD_GRAVE, ' ', '`'
        .byte DEAD_GRAVE, 'a', $E0, DEAD_GRAVE, 'e', $E8, DEAD_GRAVE, 'i', $EC
        .byte DEAD_GRAVE, 'o', $F2, DEAD_GRAVE, 'u', $F9
        .byte DEAD_GRAVE, 'A', $C0, DEAD_GRAVE, 'E', $C8, DEAD_GRAVE, 'I', $CC
        .byte DEAD_GRAVE, 'O', $D2, DEAD_GRAVE, 'U', $D9
        .byte DEAD_ACUTE, ' ', $27
        .byte DEAD_ACUTE, 'a', $E1, DEAD_ACUTE, 'e', $E9, DEAD_ACUTE, 'i', $ED
        .byte DEAD_ACUTE, 'o', $F3, DEAD_ACUTE, 'u', $FA, DEAD_ACUTE, 'y', $FD
        .byte DEAD_ACUTE, 'A', $C1, DEAD_ACUTE, 'E', $C9, DEAD_ACUTE, 'I', $CD
        .byte DEAD_ACUTE, 'O', $D3, DEAD_ACUTE, 'U', $DA, DEAD_ACUTE, 'Y', $DD
        .byte DEAD_CIRC, ' ', '^'
        .byte DEAD_CIRC, 'a', $E2, DEAD_CIRC, 'e', $EA, DEAD_CIRC, 'i', $EE
        .byte DEAD_CIRC, 'o', $F4, DEAD_CIRC, 'u', $FB
        .byte DEAD_CIRC, 'A', $C2, DEAD_CIRC, 'E', $CA, DEAD_CIRC, 'I', $CE
        .byte DEAD_CIRC, 'O', $D4, DEAD_CIRC, 'U', $DB
        .byte DEAD_DIAER, ' ', '"'
        .byte DEAD_DIAER, 'a', $E4, DEAD_DIAER, 'e', $EB, DEAD_DIAER, 'i', $EF
        .byte DEAD_DIAER, 'o', $F6, DEAD_DIAER, 'u', $FC, DEAD_DIAER, 'y', $FF
        .byte DEAD_DIAER, 'A', $C4, DEAD_DIAER, 'E', $CB, DEAD_DIAER, 'I', $CF
        .byte DEAD_DIAER, 'O', $D6, DEAD_DIAER, 'U', $DC, DEAD_DIAER, 'Y', $BE
        .byte 0
