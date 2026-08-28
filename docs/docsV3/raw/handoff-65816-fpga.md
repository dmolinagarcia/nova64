# Handoff — 65816-based Personal Computer on FPGA

**Purpose of this document:** capture the architectural decisions, the reasoning behind them, and the open questions from this design session so the project can be resumed cleanly.

---

## 1. Project goal & philosophy

Design an **original personal computer** built around a **WDC 65816** CPU, implemented on FPGA(s), as a self-directed learning exercise spanning CPU cores, bus design, memory management, and OS design.

Guiding principles established this session:

- **Not compatible with anything.** This is a machine for *its own* software, not for running SNES/Apple IIGS binaries. This is a deliberate freedom: it removes the need for cycle-accurate timing and lets us use a cache with variable latency.
- The 65816 is chosen for the 6502-family simplicity plus 24-bit address space (16 MB), 16-bit registers, and — critically — the **ABORT** pin, which enables real memory protection.
- Target: a **relatively modern OS** with **preemptive multitasking**, **relocatable code**, and **memory protection**. User processes never touch hardware directly; they call the OS.

---

## 2. Four-core architecture (FPGA)

1. **65816 core** — the main CPU. Implemented as a soft core.
   - Start with a **functional** core (easier to debug), not cycle-accurate. Compatibility is not required, so cycle accuracy is not needed.
2. **Management core (MMU / memory)** — see §3. This is the memory-management engine: cache controller + PSRAM controller + protection checker + clock/bus control.
   - **Design note:** keep this as a **pure finite-state machine**, NOT a soft CPU. A soft CPU in the critical memory path adds latency. "Intelligence" (filesystem, USB stack) lives in the peripheral core or in 65816 software.
3. **Peripheral core** — PS/2, SD (SPI), UART, USB host.
4. **Graphics core (VDP)** — with its **own private VRAM**.

### USB approach
Use an **external host-capable support chip that already speaks USB** (e.g. MAX3421E over SPI), with the protocol stack in firmware. A full USB host in pure Verilog is a project in itself and is deferred. **Build order: PS/2 first, USB last.**

### Graphics / VDP approach
- VDP **registers** are memory-mapped in the I/O bank (see §7).
- **VRAM is private to the graphics core** and does **not** live in the 65816's 16 MB address space. The CPU accesses it through a **port window** (write a VRAM address to one register, read/write data through another with auto-increment — the classic TMS9918/SNES pattern). This keeps VRAM out of the main memory map and resolves the dual-access conflict between CPU and the video scan-out.
- Output: **VGA first** (simple analog timing), HDMI/DVI later (needs TMDS + fast serializers). Start around 320×240.

---

## 3. Memory hierarchy & the MMU

The management core is effectively a **CPU cache with a slow backing store**.

- **Backing store:** **16 MB QSPI PSRAM** (read/write, not flash). PSRAM enables write-back with a dirty bit.
- **Cache:** **512 KB SRAM**, acting as a hardware-managed cache over the PSRAM.
- **Page / cache line size: 256 bytes.** This is a sweet spot:
  - Matches the 65816's native 256-byte page concept (direct page, stack page, each page within a bank). Cache line = one 6502 page — trivial mental model.
  - Fills fast (~5 µs for 256 B at ~50 MB/s), keeping stalls short.
  - Stays within the PSRAM's **tCEM** (max CS-low time, ~8 µs). A 256 B burst at 50 MB/s ≈ 5 µs of CS-low — inside the limit but *tight*. **Verify tCEM on the exact PSRAM part before buying.** If needed, split the burst into two 128 B halves with a CS toggle between them.

### Cache organization (starting point)
512 KB / 256 B = **2048 lines**. **Direct-mapped** to start:
- **offset** = A[7:0] — byte within the line / within SRAM
- **index** = A[18:8] (11 bits) — which of the 2048 lines
- **tag** = A[23:19] (5 bits) — stored in a tag table to know which page lives there
- Tag table: 2048 entries × (5-bit tag + valid + dirty) = 7 bits/entry, fits in a small BRAM.
- If conflict misses hurt (unlikely with pinned pages), move to 2-way.
- Policy: **write-back with dirty bit** (not write-through) — correct for stack/DP which are hammered constantly.

### The access FSM (heart of the design)
```
IDLE ─(CPU access)→ LOOKUP
LOOKUP: tag match AND valid?
   HIT  → serve from SRAM (CPU never stalls)
   MISS → stall CPU
       victim line dirty? → WRITEBACK (SRAM→PSRAM, 256 B)
       FETCH (PSRAM→SRAM, 256 B)
       update tag, valid=1, dirty=0
       serve, release CPU
```

### Stalling the CPU on a miss
The CPU must freeze during a miss (a PSRAM access takes far longer than one PHI2). **Preferred method: gate PHI2** (don't advance the CPU clock until data is in SRAM). In a soft core this is the most robust and easiest to reason about — the CPU literally doesn't exist during the miss, no weird intermediate states.
- Alternative is the RDY pin, but the 65816 (6502 NMOS heritage) ignores RDY during write cycles on some revisions. Clock-gating sidesteps that entirely.

### Performance expectations (at ~14 MHz CPU, ~50 MB/s PSRAM)
- Clean miss (fetch only): ~5 µs
- Dirty miss (write-back + fetch): ~10 µs ≈ **~140 CPU cycles stalled**
- With decent locality (stack + DP stay hot), hit rate is very high. Pathological case is jumping across 16 MB with no locality — but that's up to our own software.

---

## 4. Pinned pages / banks

To avoid miss storms on critical state:
- **Pin all of bank 0 resident in SRAM.** 256 pages = 64 KB = only 1/8 of the 512 KB SRAM. Bank 0 holds all stacks + direct pages (see §9), so pinning it means context switches never trigger a cache storm.
- Individual pages (a process's DP, its stack) can also be pinned as needed.

---

## 5. Boot

The 65816 reset vector is at `$00:FFFC–FFFD`. That data must be in SRAM **before** reset is released.
- **Cleanest solution:** a small **boot ROM in BRAM** mapped high in bank 0, containing the reset vector and a small loader. CPU boots from BRAM (zero latency, no cache), then the loader copies what it needs from SD/PSRAM before jumping into cached RAM. Avoids the chicken-and-egg of "need the cache warm to boot the cache."

---

## 6. I/O placement — decided

**I/O lives in a dedicated high bank: `$FF`.** (Not in bank 0, following a cleaner model than classic C64/Apple.) This keeps bank 0 clean and makes the protection rule a simple 8-bit comparison.

---

## 7. I/O map inside bank `$FF` (draft)

The bank decode happens **first**, before the cache lookup:
```
CPU access (bank:page:offset)
   bank == $FF?
      YES → I/O route: bypass cache, straight to peripheral bus
            + privilege check (user → ABORT)
      NO  → memory route: cache lookup (§3 FSM)
```
Benefit: I/O accesses never pollute the cache (no evicting a hot line to read a status register) and have fixed latency.

Draft layout (256 B walls per region so decode/protection stays clean; exact numbers TBD):
```
$FF:0000–$FF:00FF   management (MMU): page/permission table, context, cache control
$FF:0100–$FF:01FF   system timer + IRQ (source of preemptive multitasking)
$FF:0200–$FF:02FF   peripherals: PS/2, SD-SPI, UART, USB host (MAX3421E)
$FF:0300–$FF:03FF   graphics: VDP registers (port window into private VRAM)
...                 remaining space free to grow
```
The `$FF` decoder is a simple demux on the high bits of the offset; each core owns a range.

---

## 8. Protection model (the machine's standout feature)

The bare 65816 has **no supervisor/user mode, no privilege, no memory protection**. On a naked chip, a "modern multitasking OS" would be cooperative with no isolation.

**But because every CPU access passes through the management core, and the CPU is a soft core, we can build real protection:**

1. The OS keeps a **per-process, per-page permission table (R/W/X)** in the management core.
2. On every access, management checks the current process's permission.
3. On violation, management asserts **ABORT** — the 65816 pin *designed for virtual memory / page faults*: it aborts the instruction without corrupting registers, vectors to a handler, and the instruction can be retried after the fault is fixed. The physical WDC chip has the ABORT pin but no access-checking hardware; our design supplies exactly that hardware, sitting in exactly the right place.

**Three privilege classes** (the ABORT handler treats them differently):
- **Bank `$FF` (I/O):** privileged only. User access → ABORT always. Rule of thumb: *user process may never set DBR = $FF or PBR = $FF.*
- **Bank 0 (stacks + DP + OS state):** resident in SRAM. User stack/DP (accessible) coexists with OS structures (protected). 256 B granularity lets us mark, per process, which bank-0 pages it sees.
- **Banks 1–$FE (general memory):** cached, with per-process R/W/X page permissions.

**The latch everything hangs on:** the permission table lives in `$FF:0000–00FF`, and `$FF` is privileged, so **the protection protects itself** — only the OS in privileged mode can reprogram the MMU. This is self-consistent.

**Privilege detection:** OS entry is always via COP (§9), which vectors to a known address. Management can detect "fetching instructions from the OS region → privileged" and drop privilege before returning to user. The context register controlling this must live at an address reachable only in privileged mode, or a user could grant itself permissions.

---

## 9. OS design direction

### Two-layer separation (important terminology)
What a user process calls is **NOT a driver** — it's the **system-call layer (API/ABI)**. The driver is one level below.
- User process: `SYS_WRITE(fd, buffer, len)` — generic, hardware-agnostic.
- OS dispatches, decides `fd` is the console → invokes the **console driver** → driver writes VDP registers.
- Drivers are internal to the OS, hardware-specific. Keeping the two layers separate is what buys portability: user code never changes even if the video hardware changes.

### Syscall mechanism: the COP instruction
The 65816 has a purpose-built instruction: **COP** (co-processor), a software interrupt with **its own vector** and a signature byte operand.
- Process executes `COP #n`; the byte (or a register) gives the syscall number.
- CPU vectors to the OS handler, which switches to its own context (DBR, D, S), does the work, returns with RTI.
- The process doesn't know where the OS routine lives — decoupled and relocatable.
- **BRK** is reserved for the debugger.

### Preemptive multitasking
Needs a **timer that raises an IRQ** (in the management/peripheral core) so the scheduler can preempt. Trivial to add, easy to forget.

### Relocatable code — bank as segment register
- Absolute addressing (`LDA $1234`) takes its bank from **DBR**; code takes its from **PBR**.
- This gives near-free relocation **at bank granularity**: put each process in its own bank, reference its data via DBR, and relocating = changing the bank number. Same absolute addresses work wherever it's loaded.
- **Limit:** valid while a process's code + data fit in its 64 KB bank. Beyond that you need `JSL/JML` (long jumps with explicit bank) and it gets more complex. **Start with one bank per process** to keep relocation trivial. True relocation beyond that is *load-time* (a loader patching absolute addresses), not runtime PIC — the 65816 has little relative addressing (`BRL`, `PER`) and pure PIC is painful.

### Bank 0 is the bottleneck
On the 65816:
- The **stack** (S, 16-bit) always lives in bank 0.
- The **direct page** (D, 16-bit) always lives in bank 0.

So *all* stacks and DPs of *all* processes share one 64 KB bank — a scarce resource limiting how many processes / how much stack. Recommendation: pin all of bank 0 resident (see §4).

---

## 10. Settled decisions (summary)

- Original machine, **no compatibility** requirement → functional 65816 core, variable-latency cache is fine.
- Four FPGA cores: 65816, management (MMU, as a **pure FSM**), peripherals, graphics (with private VRAM).
- USB via external host chip (MAX3421E-class) in firmware; PS/2 first, USB last.
- Backing store: **16 MB QSPI PSRAM**. Cache: **512 KB SRAM**, hardware-managed.
- **256 B** page / cache line. Direct-mapped, write-back with dirty bit, 2048 lines.
- CPU stalled on miss by **gating PHI2**.
- **Bank 0 pinned** resident in SRAM.
- Boot via **small BRAM boot ROM** holding the reset vector + loader.
- **I/O in bank `$FF`**, privileged, bypasses cache, decoded before cache lookup.
- Memory protection via **per-page/per-process permission table** + **ABORT** pin; the table itself lives in `$FF` so protection is self-protecting.
- Syscalls via **COP**; BRK reserved for debugger.
- Preemptive scheduling via a **timer IRQ**.
- Relocation via **one bank per process** (bank = segment).

---

## 11. Open questions / next steps

1. **Context switch + Process Control Block** — the next real knot. It touches everything at once:
   - Save the outgoing process's A/X/Y/S/D/DBR/PBR/P; load the incoming process's.
   - Tell the management core "the active process is now N" so it swaps the live permission table. That **"active process" register in `$FF`** is what ties CPU and MMU together on every switch. Often forgotten.
2. **Exact `$FF` register map** — finalize offsets per core.
3. **Verify PSRAM tCEM** on the specific part before purchase; decide whether 256 B bursts need splitting.
4. **Verilog of the cache FSM** — with the `$FF` bypass branch already included.
5. Optional cache tuning: move to 2-way if conflict misses appear.

### Recommended hardware / toolchain (from discussion)
- Board: **ULX3S (Lattice ECP5)** — SD, USB, video (GPDI), aimed at retrocomputing. Alternative: DE10-Nano (MiSTer ecosystem).
- Toolchain: **Yosys + nextpnr** (open), simulation with **Verilator/Icarus + GTKWave**.

### Suggested build order
1. 65816 + RAM + minimal ROM running a simple program (LED/serial).
2. Bus/interconnect + memory arbiter.
3. VGA output with a dumb framebuffer.
4. PS/2 and SD-SPI.
5. Management core as the cache/MMU FSM.
6. VDP with real sprites/tiles.
7. USB via MAX3421E.
