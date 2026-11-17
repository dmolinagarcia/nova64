; libc.s — the libc surface, monolithic build (CN1.1, D113).
;
; The shell calls these names and nothing else of the system. Here they are
; the kernel's own handlers, reached by a direct call: the link costs nothing
; and this file is all of it. In the user-mode build the same names are COP
; stubs -- LDY #n, COP #n, RTL -- generated from the service table, and no
; shell source changes.
;
;       first argument A, second X, third r0; a 32-bit argument in r0:r1
;       result in A, 32-bit results in A (low) and X (high); negative = error

open     = sys_open             ; (path, flags) -> descriptor
close    = sys_close            ; (fd)
read     = sys_read             ; (fd, buf, n) -> bytes
write    = sys_write            ; (fd, buf, n) -> bytes
ioctl    = sys_ioctl            ; (fd, op, arg)
stat     = sys_stat             ; (path, struct nv_stat *)
unlink   = sys_unlink           ; (path)
rename   = sys_rename           ; (from, to)
mkdir    = sys_mkdir            ; (path)
rmdir    = sys_rmdir            ; (path)
chdir    = sys_chdir            ; (path)
getcwd   = sys_getcwd           ; (buf, size) -> length
sync     = sys_sync             ; ()
opendir  = sys_opendir          ; (path) -> descriptor
readdir  = sys_readdir          ; (fd, struct nv_dirent *) -> 1, or 0 at the end
closedir = sys_close            ; a directory descriptor is a descriptor (D114)
uptime   = sys_uptime           ; () -> milliseconds

; nv_sysinfo() needs no call: the information block is a read-only page at a
; fixed address in every address space (D91, LB1).
NV_SYSINFO = SIB_ADDR
