# BIOS — what it provides
> residency · services · boundary with the EC

There's no BIOS chip: it's a binary on the SD that the RP2040 preloads into SRAM. The 65816 comes to life with the `$FFFC` vector pointing inside that zone.

- I.1 — Residency: `BIOS.BIN` on the SD → preloaded by the RP2040 into the pinned SRAM zone (step 5 of Fig. 3). No dedicated flash.
- I.2 — At reset it provides: CPU init (native mode `CLC/XCE`, stack, direct page), MMU in supervisor identity mapping, cache init, quick memory test, UART console, and boot screen.
- I.3 — Machine monitor: peek/poke, dumps, serial loading — the working tool for stages E2–E4.
  NOTE: A user button wired to Helium raises NMI, which is how you drop into the monitor on a machine that is already running rather than resetting it to get there.
- I.4 — OS loader: mounts FAT (read-only), locates `KERNEL.BIN`, loads it, and jumps to it passing an **info block**: RAM size, device map, battery state, and RTC time (via EC). [[open]]
  NOTE: Exact block format yet to be finalized (→ N).
- I.5 — Residual services to the OS: a small `JSL` jump table (putchar, getchar, read sector) during the transition; once the kernel drivers take over, the BIOS stops being used.
- I.6 — Boundary of responsibilities: the RP2040 is the "board BIOS" (everything pre-CPU); this binary is the "system BIOS" (everything post-reset). Together they cover what a single firmware does in a PC.
