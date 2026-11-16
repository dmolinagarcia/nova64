; init.s — kernel initialisation, in the order of CN1's table. The order is
; the same in both builds; what differs is only step 8.

.proc kmain
        jsr con_init            ; 2. kprintf and panic work from here
        ldx #.loword(msg_banner)
        jsr kputs
        jsr sib_summary

        jsr clock_init          ; 3. panics if the counter is dead

        jsr kbd_init            ; 4. without a keyboard: an output-only console
        cmp #E_OK
        beq @blk
        ldx #.loword(msg_nokbd)
        jsr kputs

@blk:   jsr crc_init
        jsr vfs_init
        jsr blk_init            ; 5. the card
        cmp #E_OK
        beq @mount
        ldx #.loword(msg_nocard)
        jsr kputs
        bra @tty
@mount: jsr root_mount          ; 6. the MBR scan, NVFS at /

@tty:   lda #0                  ; 7. descriptors 0, 1 and 2 on the console
        sta r6
        ldy #DEV_INIT
        jsr dev_call
        lda #.loword(path_tty)
        ldx #NV_O_RDWR
        jsr sys_open
        cmp #0
        bne @nofd
        jsr sys_dup
        cmp #1
        bne @nofd
        lda #0
        jsr sys_dup
        cmp #2
        beq @shell
@nofd:  ldx #.loword(msg_notty)
        jmp panic

@shell: jsr sh_main             ; 8. monolithic: call the shell, and again if it returns
        bra @shell
.endproc

; CON-00: a summary of the information block
.proc sib_summary
        lda f:SIB+SIB_MAGIC
        cmp #$534E
        bne @none
        lda f:SIB+SIB_MAGIC+2
        cmp #$4249
        beq @ok
@none:  ldx #.loword(msg_nosib)
        jmp panic
@ok:    ldx #.loword(msg_cpu)
        jsr kputs
        lda f:SIB+SIB_CPU
        bne @argon
        ldx #.loword(msg_wdc)
        bra @cpu
@argon: ldx #.loword(msg_argon)
@cpu:   jsr kputs
        ldx #.loword(msg_ram)
        jsr kputs
        lda f:SIB+SIB_RAM_FRAMES ; 2 KB frames -> KB
        asl a
        jsr kputu
        ldx #.loword(msg_text)
        jsr kputs
        lda f:SIB+SIB_COLS
        jsr kputu
        lda #'x'
        jsr kputc
        lda f:SIB+SIB_ROWS
        jsr kputu
        lda #$0A
        jmp kputc
.endproc

; find the first NVFS partition and mount it at / (CN1.16, D110)
.proc root_mount
        lda #MBR_TYPE_NVFS
        jsr blk_find_part
        cmp #E_OK
        beq @found
        ldx #.loword(msg_nopart)
        jmp kputs
@found: jsr nv_mount
        cmp #E_OK
        beq @up
        pha
        ldx #.loword(msg_mfail)
        jsr kputs
        pla
        eor #$FFFF
        inc a
        jsr kputu
        lda nv_badbit           ; a refused feature is named, never just "unsupported" (Y2.20)
        bmi @nl
        ldx #.loword(msg_bit)
        jsr kputs
        lda nv_badbit
        jsr kputu
@nl:    lda #$0A
        jmp kputc
@up:    ldx #.loword(msg_root)
        jsr kputs
        lda part_num
        clc
        adc #'0'
        jsr kputc
        lda nv_ro
        beq @rw
        ldx #.loword(msg_ro)
        jsr kputs
        lda nv_badbit
        bmi @rw
        ldx #.loword(msg_robit)
        jsr kputs
        lda nv_badbit
        jsr kputu
@rw:    lda nv_replayed
        beq @end
        ldx #.loword(msg_replay)
        jsr kputs
@end:   lda #$0A
        jmp kputc
.endproc

msg_banner: .byte "noVa64 kernel (monolithic)", $0A, 0
msg_nosib:  .byte "no system information block", 0
msg_cpu:    .byte "cpu: ", 0
msg_wdc:    .byte "W65C816S", 0
msg_argon:  .byte "Argon", 0
msg_ram:    .byte ", ", 0
msg_text:   .byte " KB, text ", 0
msg_nokbd:  .byte "kbd: no keyboard", $0A, 0
msg_nocard: .byte "blk: no card", $0A, 0
msg_nopart: .byte "root: no NVFS partition", $0A, 0
msg_mfail:  .byte "root: mount failed, error ", 0
msg_bit:    .byte ", incompat feature bit ", 0
msg_root:   .byte "root: NVFS on sd0p", 0
msg_ro:     .byte ", read-only", 0
msg_robit:  .byte ": ro_compat feature bit ", 0
msg_replay: .byte ", journal replayed", 0
msg_notty:  .byte "no console device", 0
path_tty:   .byte "/dev/tty", 0
