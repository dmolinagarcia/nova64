; hw.inc — the hardware as the kernel and the BIOS see it.
;
; Addresses marked SPEC are fixed by a sheet of docs/docsV3. Addresses marked
; PROVISIONAL are this tree's choice where the sheets leave the allocation open;
; each names the open question it stands in for. Change them here and nowhere
; else. The register-map version the BIOS publishes in the information block
; (M.16) is what software should test, never these constants.

; ---- Neon, Mode 0 (SPEC: T1.19, T1.26, T1.27) ------------------------------
NEON_TEXT       = $FE0000       ; 4096 cells x 2 bytes: glyph, attribute
NEON_FONT       = $FE2000
TEXT_CELLS      = 4096
TEXT_COLS       = 128           ; D44: 128 x 32 on every board
TEXT_ROWS       = 32
TEXT_ATTR       = $0F           ; light grey on black, the power-on attribute

; ---- Neon register file (SPEC: T1.21, base $FF:8000, byte-wide) -----------
NEON_ID         = $FF8000       ; reads $4E
NEON_VER        = $FF8001
NEON_MODE       = $FF8002
NEON_CTRL       = $FF8003
NEON_STATUS     = $FF8004
NEON_TEXT_START = $FF8010       ; 12-bit cell index, two bytes
NEON_CURSOR_POS = $FF8012       ; 12-bit cell index, latches on the high byte
NEON_CURSOR_CTL = $FF8014       ; b4 enable

; ---- Helium MMU and cache block (SPEC: sheet M, 16-bit, M=0) ---------------
HE_TLB_FLUSH_ALL    = $FF0012
HE_CACHE_FLUSH_FRAME = $FF0018
HE_CACHE_FLUSH_ALL  = $FF001C
HE_MMU_STATUS       = $FF001E   ; b0 BUSY
HE_CACHE_INVAL_FRAME = $FF0020

; ---- Helium capability registers (PROVISIONAL: Q168) -----------------------
HE_CAPS         = $FF0022       ; b0 HID queue · b1 SD block · b2 NVM32 core
HE_REGMAP       = $FF0024       ; register-map version
HE_PLATFORM     = $FF0026       ; 0 target · 1 rig · 2 carrier · $E0 emulator
HE_RAM_FRAMES   = $FF0028       ; 2 KB frames the CPU can reach

; ---- Fixed-frequency counter (PROVISIONAL: L.15, Q22) ----------------------
; Free-running, 1 kHz, 32 bits in two halves. Reads have no side effects, so
; a consistent value is read high-low-high.
TMR_MS_L        = $FF0100
TMR_MS_H        = $FF0102

; ---- HID event queue (PROVISIONAL: CN1.28, Q180, Q4) -----------------------
HID_STATUS      = $FF0200       ; b0 not empty · b1 overflow, sticky
HID_COUNT       = $FF0202
HID_EV_KEY      = $FF0204       ; head entry: low = HID usage (page 7), high = modifiers
HID_EV_FLAGS    = $FF0206       ; head entry: b0 press · b1 repeat · b7:4 device (0 keyboard)
HID_POP         = $FF0208       ; any write drops the head entry; reads pop nothing
HID_CTRL        = $FF020A       ; b0 IRQ enable · b1 clear overflow

; ---- SD storage block (SPEC: sheet G register set; base PROVISIONAL: Q22) --
SD_BASE         = $FF0300
SD_LBA_L        = SD_BASE + $00
SD_LBA_H        = SD_BASE + $02
SD_DST_FRAME    = SD_BASE + $04
SD_DST_OFF      = SD_BASE + $06
SD_DMA_READ     = SD_BASE + $08
SD_DMA_WRITE    = SD_BASE + $0A
SD_STATUS       = SD_BASE + $12 ; b0 BUSY · b3 ERR_CRC · b4 ERR_TIMEOUT · b5 ERR_ARB · b6 NO_CARD
SD_CARD         = SD_BASE + $14 ; b0 present · b1 initialised · b2 SDHC · b3 write-protected
SD_CTRL         = SD_BASE + $16 ; b2 clear errors · b3 full-speed clock
SD_CMD          = SD_BASE + $18
SD_ARG_L        = SD_BASE + $1A
SD_ARG_H        = SD_BASE + $1C
SD_RESP         = SD_BASE + $1E
SD_ST_BUSY      = $0001
SD_ST_ERRORS    = $0078

; ---- Memory layout ----------------------------------------------------------
BIOS_BASE       = $E000         ; bios.bin covers $00:E000-$00:FFFF
SIB_ADDR        = $F800         ; last 2 KB page of bank $00 (D91)
RAMVEC          = $0200         ; bank $00: far pointers the BIOS vectors jump through
RAMVEC_COP      = RAMVEC + 0
RAMVEC_BRK      = RAMVEC + 4
RAMVEC_ABORT    = RAMVEC + 8
RAMVEC_NMI      = RAMVEC + 12
RAMVEC_IRQ      = RAMVEC + 16
STACK_TOP       = $DFFF         ; LB1 "A program's memory": the stack ends below $E000
STAGE_ADDR      = $100000       ; PROVISIONAL: the BIOS staging area (CN1.6)
KBANK           = $FD           ; D102: the kernel's fixed bank
KFAR_BANK       = $FC           ; the kernel's first bank of growth: block cache, scrollback

; ---- BIOS service table (I.5): JSL to these, A/X/Y 16-bit, native mode ----
BIOS_PUTCHAR    = $00E000       ; A = character
BIOS_GETCHAR    = $00E004       ; -> A = raw HID_EV_KEY, carry set if none; pops the event
BIOS_READSEC    = $00E008       ; A = sector count, X/Y = LBA low/high, destination in fp ($20)

MBR_TYPE_BOOT   = $7E           ; PROVISIONAL: the boot partition's type byte (Q177)
MBR_TYPE_NVFS   = $7F           ; SPEC: Y2.25
WDM_CPUID       = $01           ; PROVISIONAL: the WDM operand of the CPU probe (I.7)
