; kernel.s — the noVa64 kernel, monolithic console build (sheet CN1).
;
; One image, no MMU, no processes: the build of CON-00 to CON-03. The shell is
; linked in and calls the kernel's sys_* handlers directly; in the user-mode
; build of CON-04 the same shell sources link against COP stubs instead, and
; that link is the only thing that changes (CN1.1).
;
; Layout. The kernel's code and data share bank $FD (D102), entered with
; DBR = $FD so that 16-bit absolute addressing reaches all of it; the image is
; therefore linked bank-relative and KBANK supplies the bank where a far
; pointer is needed. Bank $FC, the first bank of downward growth, holds the
; block cache and the console's scrollback. Direct page and stack are in
; bank $00, where the 65816 needs them.
;
; Conventions are in include/macros.inc. A routine returns its status in A:
; zero or a positive result, or a negative code from the one error space of
; Y1.15. TRY propagates a failure to the caller.

.include "macros.inc"
.include "hw.inc"
.include "abi.inc"

.macro TRY fn
        jsr fn
        cmp #$0000
        bpl :+
        rts
:
.endmacro

SIB = SIB_ADDR                  ; bank $00, reached with long addressing

.segment "CODE"
.include "crt0.s"
.include "lib.s"
.include "con.s"
.include "clock.s"
.include "kbd.s"
.include "keymap.s"
.include "tty.s"
.include "dev.s"
.include "blk.s"
.include "crc.s"
.include "bcache.s"
.include "journal.s"
.include "nvfs.s"
.include "nvfsext.s"
.include "nvfsdir.s"
.include "nvfsops.s"
.include "vfs.s"
.include "sys.s"
.include "init.s"

; ---- the shell: user side, calls nothing but the libc surface ----------------
.include "../shell/libc.s"
.include "../shell/sh_main.s"
.include "../shell/sh_parse.s"
.include "../shell/sh_builtin.s"
.include "../shell/sh_exec.s"
