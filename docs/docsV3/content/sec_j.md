# Operating system
> how it uses the BIOS · what it provides to processes

Small monolithic kernel, written in C with assembly only where it hurts. It consumes the BIOS for a few milliseconds and then provides everything else.

- J.1 — Model: preemptive kernel resident in virtual bank `$01` (64 KB, pinned in SRAM), mapped identically and privileged in every process's page table.
- J.2 — How it uses the BIOS: only the info block and the `JSL` table during the transition; its drivers replace the console and SD, and from then on the BIOS goes inert.
- J.3 — Syscall mechanism: `COP`, with the service number **in the accumulator**, loaded by the libc stub immediately before the instruction. The handler indexes its dispatch table straight from A; arguments stay wherever the C compiler's calling convention left them. So the stable ABI is exactly "**calling convention + a number in A**" — nothing is marshalled, and the stubs that carry it are three lines each (→ [O.7](sec_o#o7)). ((The inline signature byte is still emitted, but no longer read: it survives for disassembly, tracing and static verification of a binary. Taking the number from A instead of walking the stack for it saves 20–30 cycles on every single call.))
  NOTE: The 65816's other software interrupt, `BRK`, is reserved for the debugger and must stay that way — two software traps with separate vectors is exactly enough to keep breakpoints from colliding with system calls.
- J.4 — Internal drivers with a fixed 5-function interface — `init · read · write · ioctl · irq_handler` — reached through a device table indexed by device number, and exposed as `/dev/*`. A minimal VFS decides only whether an `open` routes to the filesystem or to that table.
  NOTE: Drivers expose **no syscalls of their own**, and that is the point: per-driver calls would break the ABI every time a chip changed. Swapping hardware means rewriting one driver against the same internal interface, while libc and the syscall numbering stay put.
- J.5 — Framebuffer: the preferred path is an ioctl that maps VRAM pages (`$FE`) into the process's address space — no per-pixel `write()`. **Power is the mirror case and now the proof of the rule**: shutting the machine down is as privileged an act as exists, and it still adds nothing to the dispatch table. It is `/dev/power` — an `ioctl` for poweroff and reboot, a `read` for battery state — and the privilege check comes free from the node's permissions rather than from a bespoke gate inside a `sys_poweroff()` (→ [S.11](sec_s#s11)).
  NOTE: The kernel's own shutdown path is not a driver concern and is spelled out in [S.13](sec_s#s13). One step of it belongs here: **quiescing the MMU before halting.** The cache is write-back, so a shutdown that flushes the filesystem but not the cache loses exactly the writes the filesystem just made.
- J.6 — Filesystem: FAT over the SD + minimal devfs. Per-device blocking queues and basic IPC. [[sketch]]

## v1 dispatch table — Unix-shaped, deliberately small. Numbering the `SYS_*` constants is still pending.

| Group | Calls | Notes |
|---|---|---|
| Processes | `exec(path)` · `exit(code)` · `wait(pid)` · `getpid()` · `yield()` · `sleep(ms)` · `kill(pid,sig)` | `exec` loads the binary from the card (→ [N.6](sec_n#n6)). `sleep` is backed by Helium's free-running counter, never by cycle counting. Signals stay minimal: KILL, TERM, perhaps one user-defined. |
| Memory | `sbrk(n)` · `mmap(addr,n,prot)` · `munmap` · `mshare(pid,addr)` | `sbrk` only moves the heap end; the frames arrive later by ABORT. `prot` maps straight onto the PTE R/W/X bits. `mshare` is optional — shared-memory IPC, and the reason the cache may never migrate a frame (→ [L.3](sec_l#l3)). |
| Files and devices | `open(path,flags)` · `close(fd)` · `read` · `write` · `seek` · `stat` · `unlink` · `rename` · `mkdir` · `readdir` · `ioctl(fd,op,arg)` | One unified surface: a path resolves either to the filesystem or to the device table, and everything past that point is the same seven calls. `ioctl` is the escape valve for whatever does not fit. |
| Time and IPC | `time()` · `pipe()` | Little more than this in v1. |
