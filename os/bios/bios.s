; bios.s — the noVa64 system BIOS (sheet I, CN1.4).
;
; An 8 KB image covering $00:E000-$00:FFFF with its own vectors, which the EC
; writes into memory before it releases the CPU from reset (I.1, D82). It does
; four things and then goes inert (I.5):
;
;   1. native mode, stack, direct page                               CN1.4 (1)
;   2. probes the CPU and Helium once and writes the system
;      information block                                      CN1.4 (2), I.7
;   3. prints a banner on Mode 0, which needs no initialisation      CN1.4 (3)
;   4. finds a kernel image -- staged by the Debug Agent, or in the
;      card's boot partition -- checks it, loads it, jumps to it  CN1.4 (4-8)
;
; On any failure it prints why and halts. It never reboots in a loop.

.include "macros.inc"
.include "hw.inc"
.include "abi.inc"

BIOS_VERSION = $0001

; ---- bank $00 working storage -------------------------------------------------
con_row   = $0300
con_col   = $0302
sd_lba    = $0304
sd_addr   = $0308
sd_hc     = $030C
sd_t0     = $030E
sd_tmp    = $0310
part_lba  = $0312
part_secs = $0316
part_num  = $031A
mbr_type  = $031C
crc_lo    = $031E
crc_hi    = $0320
len_lo    = $0322
len_hi    = $0324
want_lo   = $0326
want_hi   = $0328
seg_left  = $032A
seg_ptr   = $032C
entry     = $032E               ; far pointer for the final jump
boot_src  = $0332
MBR_BUF   = $0800               ; 512 bytes, bank $00

STAGE_LO  = .loword(STAGE_ADDR)
STAGE_BK  = .bankbyte(STAGE_ADDR)

; ==============================================================================
.segment "JUMPS"                ; the JSL table of I.5, at $00:E000
        jml f_putchar
        jml f_getchar
        jml f_readsec

.segment "CODE"

.proc reset
        sei
        clc
        xce                     ; native mode
        rep #$38                ; 16-bit A, X, Y; decimal off
        ldx #STACK_TOP
        txs
        lda #$0000
        tcd
        pha
        plb
        plb                     ; DBR = 0

        ldx #16                 ; every vector starts out pointing at "unexpected"
@vec:   lda #.loword(unexpected)
        sta RAMVEC,x
        lda #$0000
        sta RAMVEC+2,x
        dex
        dex
        dex
        dex
        bpl @vec

        jsr con_adopt
        jsr sib_write
        ldx #.loword(msg_banner)
        jsr puts

        ; ---- a kernel staged by the Debug Agent? (CN1.6) ----
        lda #STAGE_LO
        sta fp
        lda #STAGE_BK
        sta fp+2
        jsr image_check
        cmp #E_OK
        bne @card
        lda #1
        sta boot_src
        ldx #.loword(msg_staged)
        jsr puts
        jmp boot

        ; ---- the card's boot partition (CN1.4 steps 5-7) ----
@card:  lda f:HE_CAPS
        and #$0002
        beq @nocard
        jsr sd_init
        cmp #E_OK
        beq @inited
@nocard:
        ldx #.loword(msg_nocard)
        jmp die
@inited:
        lda #MBR_BUF
        sta fp
        sta sd_addr
        stz fp+2
        stz sd_addr+2
        lda #MBR_TYPE_BOOT
        jsr mbr_find
        cmp #E_OK
        beq @part
        cmp #E_IO
        bne @nopart
        ldx #.loword(msg_ioerr)
        jmp die
@nopart:
        ldx #.loword(msg_nopart)
        jmp die
@part:  lda part_lba            ; the header: one 2 KB read into the staging area
        sta sd_lba
        lda part_lba+2
        sta sd_lba+2
        lda #STAGE_LO
        sta fp
        sta sd_addr
        lda #STAGE_BK
        sta fp+2
        sta sd_addr+2
        lda #4
        jsr sd_read
        cmp #E_OK
        beq @hdr
@ioerr: ldx #.loword(msg_ioerr)
        jmp die
@hdr:   jsr header_check
        cmp #E_OK
        beq @size
@bad:   ldx #.loword(msg_badimg)
        jmp die
@size:  ldy #NX_FILESIZE        ; sectors = (size + 511) >> 9, size < 64 KB
        lda [fp],y
        clc
        adc #511
        xba
        and #$00FF
        lsr a
        jsr sd_read
        cmp #E_OK
        bne @ioerr
        jsr image_check
        cmp #E_OK
        bne @bad
        lda #2
        sta boot_src
        ldx #.loword(msg_card)
        jsr puts
        lda part_num
        clc
        adc #'0'
        jsr putchar
        lda #$0A
        jsr putchar
        ; fall through
.endproc

; load the segments of the image at fp, publish where it came from, and go
.proc boot
        ldy #NX_ENTRY
        lda [fp],y
        sta entry
        iny
        iny
        lda [fp],y
        sta entry+2
        ldy #NX_NSEG
        lda [fp],y
        sta seg_left
        lda #NX_SEGS
        sta seg_ptr
@seg:   lda seg_left
        beq @go
        jsr seg_load
        lda seg_ptr
        clc
        adc #NXS_SIZEOF
        sta seg_ptr
        dec seg_left
        bra @seg
@go:    lda #$0000              ; the image is used: a warm reset must not find it (CN1.6)
        ldy #NX_MAGIC
        sta [fp],y
        lda boot_src
        sta SIB_ADDR+SIB_BOOT_SRC
        lda part_lba
        sta SIB_ADDR+SIB_BOOT_LBA
        lda part_lba+2
        sta SIB_ADDR+SIB_BOOT_LBA+2
        lda part_num
        sta SIB_ADDR+SIB_BOOT_PART
        jmp [entry]             ; nothing is passed: the block is at a fixed address
.endproc

; copy one segment out of the image at fp, and zero the rest of it. A segment
; lies inside one bank and so does its source, which image_check enforced.
.proc seg_load
        ldy seg_ptr             ; fq = vaddr
        lda [fp],y
        sta fq
        iny
        iny
        lda [fp],y
        sta fq+2
        lda seg_ptr
        clc
        adc #NXS_FILESZ
        tay
        lda [fp],y
        sta len_lo
        lda seg_ptr
        clc
        adc #NXS_MEMSZ
        tay
        lda [fp],y
        sta want_lo
        lda seg_ptr
        clc
        adc #NXS_FOFF
        tay
        lda [fp],y              ; source offset in the file
        tax
        ldy #0
        A8
@copy:  cpy len_lo
        beq @zero
        phy
        txy
        lda [fp],y
        ply
        sta [fq],y
        inx
        iny
        bra @copy
@zero:  cpy want_lo
        beq @done
        lda #$00
        sta [fq],y
        iny
        bra @zero
@done:  A16
        rts
.endproc

; the fixed part of the header at fp: magic, version, a size that fits the
; staging bank, at least one segment
.proc header_check
        ldy #NX_MAGIC
        lda [fp],y
        cmp #$4E7F              ; $7F 'N'
        bne @bad
        iny
        iny
        lda [fp],y
        cmp #$3436              ; '6' '4'
        bne @bad
        ldy #NX_VERSION
        lda [fp],y
        cmp #1
        bne @bad
        ldy #NX_FILESIZE+2
        lda [fp],y
        bne @bad                ; 64 KB or more: this loader cannot stage it
        ldy #NX_FILESIZE
        lda [fp],y
        cmp #NX_SEGS+NXS_SIZEOF
        bcc @bad
        ldy #NX_NSEG
        lda [fp],y
        beq @bad
        cmp #9
        bcs @bad
        lda #E_OK
        rts
@bad:   lda #E_NOEXEC
        rts
.endproc

; header, then the CRC-32 of the whole file with its CRC field read as zero
.proc image_check
        jsr header_check
        cmp #E_OK
        beq @crc
        rts
@crc:   ldy #NX_FILESIZE
        lda [fp],y
        sta len_lo
        ldy #NX_CRC
        lda [fp],y
        sta want_lo
        iny
        iny
        lda [fp],y
        sta want_hi
        lda #$FFFF
        sta crc_lo
        sta crc_hi
        ldy #0
@byte:  cpy len_lo
        beq @fin
        lda [fp],y
        and #$00FF
        cpy #NX_CRC
        bcc @use
        cpy #NX_CRC+4
        bcs @use
        lda #$0000
@use:   eor crc_lo
        sta crc_lo
        ldx #8
@bit:   lsr crc_hi
        ror crc_lo
        bcc @next
        lda crc_lo
        eor #$8320
        sta crc_lo
        lda crc_hi
        eor #$EDB8
        sta crc_hi
@next:  dex
        bne @bit
        iny
        bra @byte
@fin:   lda crc_lo
        eor #$FFFF
        cmp want_lo
        bne @bad
        lda crc_hi
        eor #$FFFF
        cmp want_hi
        bne @bad
        lda #E_OK
        rts
@bad:   lda #E_NOEXEC
        rts
.endproc

; ---- the system information block (I.4, I.7, D91) ----------------------------
.proc sib_write
        ldx #SIB_SIZEOF-2
@clr:   stz SIB_ADDR,x
        dex
        dex
        bpl @clr
        lda #$534E              ; 'N' 'S'
        sta SIB_ADDR+SIB_MAGIC
        lda #$4249              ; 'I' 'B'
        sta SIB_ADDR+SIB_MAGIC+2
        lda #1
        sta SIB_ADDR+SIB_VERSION
        lda #SIB_SIZEOF
        sta SIB_ADDR+SIB_SIZE
        lda #$0000              ; the CPU probe: a W65C816S leaves A alone (D89)
        .byte $42, WDM_CPUID
        sta SIB_ADDR+SIB_CPU
        lda f:HE_PLATFORM
        sta SIB_ADDR+SIB_PLATFORM
        lda f:HE_REGMAP
        sta SIB_ADDR+SIB_REGMAP
        lda f:HE_CAPS
        sta SIB_ADDR+SIB_CAPS
        asl a                   ; devices: b1 HID, b2 SD from the capability bits
        and #$0006
        sta SIB_ADDR+SIB_DEVICES
        A8
        lda f:NEON_ID           ; the one sensible probe: is a Neon fitted (I.2)
        cmp #$4E
        A16
        bne @noneon
        lda SIB_ADDR+SIB_DEVICES
        ora #$0001
        sta SIB_ADDR+SIB_DEVICES
@noneon:
        lda f:HE_RAM_FRAMES
        sta SIB_ADDR+SIB_RAM_FRAMES
        lda #TEXT_COLS
        sta SIB_ADDR+SIB_COLS
        lda #TEXT_ROWS
        sta SIB_ADDR+SIB_ROWS
        lda #$FFFF
        sta SIB_ADDR+SIB_BATTERY
        lda #BIOS_VERSION
        sta SIB_ADDR+SIB_BIOS_VER
        rts
.endproc

; ---- console: stores to the text ring, nothing else (T1.54) -------------------
.proc con_adopt                 ; start where the EC's banner, if any, left the cursor
        A8
        lda f:NEON_CURSOR_POS
        sta r0
        lda f:NEON_CURSOR_POS+1
        sta r0+1
        lda f:NEON_TEXT_START
        sta r1
        lda f:NEON_TEXT_START+1
        sta r1+1
        A16
        lda r0
        sec
        sbc r1
        and #$0FFF
        pha
        and #TEXT_COLS-1
        sta con_col
        pla
        asl a                   ; row = offset / 128
        xba
        and #$001F
        sta con_row
        rts
.endproc

.proc cell_index                ; -> A = ring index of (con_row, con_col)
        A8
        lda f:NEON_TEXT_START
        sta r1
        lda f:NEON_TEXT_START+1
        sta r1+1
        A16
        lda con_row
        xba
        lsr a                   ; row * 128
        clc
        adc con_col
        clc
        adc r1
        and #$0FFF
        rts
.endproc

.proc putchar                   ; A = character; newline is the only control it knows
        and #$00FF
        cmp #$000A
        beq @nl
        ora #TEXT_ATTR << 8
        pha
        jsr cell_index
        asl a
        tax
        pla
        sta f:NEON_TEXT,x
        inc con_col
        lda con_col
        cmp #TEXT_COLS
        bcc @cursor
@nl:    stz con_col
        inc con_row
        lda con_row
        cmp #TEXT_ROWS
        bcc @cursor
        dec con_row             ; scroll: move the ring origin, clear the new row (T1.27)
        lda r1
        clc
        adc #TEXT_COLS
        and #$0FFF
        A8
        sta f:NEON_TEXT_START
        xba
        sta f:NEON_TEXT_START+1
        A16
        jsr cell_index
        asl a
        tax
        ldy #TEXT_COLS
        lda #(TEXT_ATTR << 8) | $20
@clr:   sta f:NEON_TEXT,x
        inx
        inx
        dey
        bne @clr
@cursor:
        jsr cell_index
        A8
        sta f:NEON_CURSOR_POS
        xba
        sta f:NEON_CURSOR_POS+1 ; latches here
        A16
        rts
.endproc

.proc puts                      ; X = string in bank $00, NUL-terminated
@next:  lda a:0,x
        and #$00FF
        beq @done
        phx
        jsr putchar
        plx
        inx
        bra @next
@done:  rts
.endproc

.proc die                       ; X = message. Print and halt; never loop (I.4)
        jsr puts
        ldx #.loword(msg_halt)
        jsr puts
        stp
.endproc

.proc unexpected
        rep #$38
        .a16
        .i16
        lda #$0000
        tcd
        pha
        plb
        plb
        ldx #.loword(msg_trap)
        jmp die
.endproc

; ---- the JSL services (I.5) ---------------------------------------------------
.proc f_putchar
        phb
        phd
        pea $0000
        plb
        plb
        pea $0000
        pld
        jsr putchar
        pld
        plb
        rtl
.endproc

.proc f_getchar                 ; raw HID event; the keymap is the kernel's (D106)
        lda f:HID_STATUS
        and #$0001
        beq @none
        lda f:HID_EV_KEY
        sta f:HID_POP
        clc
        rtl
@none:  sec
        rtl
.endproc

.proc f_readsec                 ; A = count, X/Y = LBA, destination in fp
        phb
        phd
        pea $0000
        plb
        plb
        pea $0000
        pld
        stx sd_lba
        sty sd_lba+2
        ldx fp
        stx sd_addr
        ldx fp+2
        stx sd_addr+2
        jsr sd_read
        pld
        plb
        rtl
.endproc

.include "../common/sdcard.s"
.include "../common/mbr.s"

; the vectors land here and leave through RAM, so a kernel can take them over
v_cop:   jml [RAMVEC_COP]
v_brk:   jml [RAMVEC_BRK]
v_abort: jml [RAMVEC_ABORT]
v_nmi:   jml [RAMVEC_NMI]
v_irq:   jml [RAMVEC_IRQ]
v_emu:   stp                    ; an interrupt in emulation mode: nothing runs there

msg_banner: .byte "noVa64 BIOS", $0A, 0
msg_staged: .byte "kernel: staged image", $0A, 0
msg_card:   .byte "kernel: loaded from sd0p", 0
msg_nocard: .byte "BIOS: no card", 0
msg_ioerr:  .byte "BIOS: card read error", 0
msg_nopart: .byte "BIOS: no boot partition", 0
msg_badimg: .byte "BIOS: bad kernel image", 0
msg_trap:   .byte "BIOS: unexpected interrupt", 0
msg_halt:   .byte " -- halted", $0A, 0

.segment "VECTORS"              ; $00:FFE0
        .word 0, 0              ; FFE0, FFE2 reserved
        .word v_cop             ; FFE4 COP
        .word v_brk             ; FFE6 BRK
        .word v_abort           ; FFE8 ABORT
        .word v_nmi             ; FFEA NMI
        .word 0                 ; FFEC reserved
        .word v_irq             ; FFEE IRQ
        .word 0, 0              ; FFF0, FFF2
        .word v_emu             ; FFF4 COP (emulation)
        .word 0                 ; FFF6
        .word v_emu             ; FFF8 ABORT
        .word v_emu             ; FFFA NMI
        .word reset             ; FFFC RESET
        .word v_emu             ; FFFE IRQ/BRK
