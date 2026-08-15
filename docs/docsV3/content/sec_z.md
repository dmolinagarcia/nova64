# Glossary
> every acronym and term used across sheets A–R

Document-wide reference. Every acronym is also expanded on first use in place, so this sheet is deliberately redundant — it exists to be jumped to, not read through. Grouped by domain; within each group, roughly in the order the concepts appear.

## Virtual memory and the MMU
  NOTE: → [sheet K](sec_k) · [sheet L](sec_l)

| Term | Meaning |
|---|---|
| Virtual memory | A layer of indirection between the addresses the CPU generates and those that reach the RAM. |
| Virtual address | The address the CPU emits — what the program "believes". 24 bits here. |
| Physical address | The address that reaches the SRAM/SDRAM chips. 27 bits here. |
| Page | A fixed-size block of the virtual space. 2 KB. |
| Frame | A block of the same size in physical memory. To translate is to pair a page with a frame. |
| VPN | Virtual Page Number — high bits of the virtual address; identifies the page. 13 bits. |
| Offset | Low bits; the byte within the page. Never translated. 11 bits. |
| MMU | Memory Management Unit — the hardware that translates. Not in the CPU here: Helium implements it. |
| Page table | An array indexed by VPN holding the PTEs. One per process, 32 KB. |
| PTE | Page Table Entry — one entry of that table. 32 bits: frame number plus flags. |
| PTE flags | `P` present in RAM · `W` writable · `X` executable · `U` reachable from user mode · `D` dirty (modified since load) · `A` accessed recently · `NC` non-cacheable · `PIN` non-evictable · `SW` free for the kernel's own use. |
| TLB | Translation Lookaside Buffer — cache of VPN→frame translations inside the MMU. Avoids re-reading the table on every access. |
| TLB hit / miss | Success or failure in that cache. A miss costs one table walk; it is not an error. |
| TLB reach | Entries × page size — the working set the TLB can cover before it starts thrashing. 64 KB here with 32 entries; the main thing a larger page buys. |
| Page walk / walker | The traversal of the page table that resolves a miss. Hardware here (≈100 ns, four 8-bit SRAM accesses); a software exception in other architectures. |
| ASID | Address Space IDentifier — tag marking which process each TLB entry belongs to. Without it the whole TLB would be flushed on every context switch. |
| Page fault | Exception raised on access to a page with `P=0`. Recoverable. |
| Demand paging | Loading pages only when touched rather than all up front. Deferred to post-v1. |
| Eager loading | Mapping and filling every segment at `exec` time. What v1 does, in place of demand paging. |
| Swapping | Evicting rarely used pages to storage in order to free frames. |
| Pinning | Marking a page as non-evictable. Required for the kernel, the interrupt vectors and the bank `$00` stack. |
| Memory protection | Preventing a process from reading or writing outside its own space. Achieved through the PTE flags, not through translation itself. |
| Cache line | The tagged unit of Helium's cache — 2 KB, one tag per line, but filled and written back in 256 B sub-blocks. |
| Sub-block | The 256 B unit a fill actually moves, with its own valid and dirty bits. Keeps a miss at ~6 µs instead of ~41 µs and bounds interrupt latency. |
| Set associative | Cache organization where a given address may sit in any of N positions ("ways") within one set. Four ways here; direct mapping would put 32,768 physical lines in competition for 384 slots. |
| Tag | The bits stored alongside a cache line saying which physical line it currently holds. Kept in EBR, ~1.5 KB, beside the TLB. |
| Dirty victim | The line a fill has to evict when it has been written to — it must go back to SDRAM first, which is why a miss can cost double. |
| Write-back | Cache policy where writes stay in the cache and reach memory only on eviction. Implied by the dirty bits. |
| Backing store | The memory a cache is a cache *of*. Here the SDRAM, which the PTEs always name. |
| Copy-on-write | Sharing a page read-only between processes and duplicating it only when one writes. An ABORT case ([L.4](sec_l#l4)). |
| Static core | A CPU whose state survives an arbitrarily slow or stopped clock. The W65C816S is one, which is what makes the PHI2 stall legal. |
| Clock gating | Stopping and restarting PHI2 in gateware. Must be glitch-free, and the first pulse on resume must meet the minimum high width. |
| Trampoline | The short privileged stubs in bank `$00` that receive every vector — the CPU forces PBR=0 on entry — and `JSL` straight into the real kernel in `$01` (→ [L.11](sec_l#l11)). |
| ASID recycling | Reassigning a used ASID to a new process. Legal only after flushing that ASID's TLB entries; skipping the flush leaks the old address space into the new one, silently (→ [M.8](sec_m#m8)). |
| Address as opcode | Command convention where each operation has its own address and the value written is its argument, not a command code (→ [M.2](sec_m#m2)). |
| WDM | Opcode `$42`, reserved by WDC and executed as a two-byte, two-cycle no-op. Considered as a command channel and rejected; it stays a no-op, and a softcore must implement it as one. |
| Memory barrier | An instruction forcing memory ordering. Not needed here and not provided: the 65816 has no prefetch queue and no store buffer, so every bus cycle is already in program order. |
| Watchdog | Timer that resumes PHI2 and raises NMI or ABORT if a fill overruns — without it a hung fill freezes the machine with no clock and no diagnostic path. |
| Refresh | The periodic recharge SDRAM needs to keep its contents. Must be interleaved into the fill state machine, not deferred until it finishes. |

## CPU, bus and gateware
  NOTE: → [sheet B](sec_b) · [sheet F](sec_f)

| Term | Meaning |
|---|---|
| Bank | A 64 KB slice of the 65816's 24-bit space, selected by DBR/PBR. The virtual bank map is in [L.10](sec_l#l10). |
| DBR · PBR · DP | Data Bank, Program Bank and Direct Page registers — the 65816 state that decides which bank an access lands in. |
| Native mode | The 65816's 16-bit, 24-bit-address mode, entered with `CLC/XCE`. The opposite is 6502 emulation mode. |
| PHI2 | The CPU's master clock, generated by Helium. Target 8 MHz. |
| BE | Bus Enable — the pin that makes the physical CPU tristate its address, data and RWB drivers and release the bus. Driven by Helium, which uses it far more often than Argon would (→ [R.3](sec_r#r3)). |
| RESB | The 65816's active-low reset. Released by the RP2040 as the last step before the CPU runs. |
| ABORTB | W65C816S pin that cancels the instruction in progress without side effects. The system's fault mechanism. |
| VPA / VDA | Pins indicating whether the cycle is an instruction fetch or a data access. They make checking the `X` flag possible. |
| RDY | The pin Helium pulls low to stall the CPU while a cache miss is being filled. |
| Northbridge | Borrowed PC term for the chip sitting between CPU and memory. Here it is Helium: MMU, cache, arbiter, timer and I/O. |
| Gateware | The logic loaded into an FPGA — the FPGA equivalent of firmware. Written in HDL, not compiled to instructions. |
| Softcore | A CPU implemented as gateware rather than as a chip. Argon's optional 65816 is one (→ [sheet E](sec_e)). |
| Fmax | The highest clock a placed-and-routed design will meet timing at. A property of the design *on a given part* — quoting one across FPGA families is meaningless. |
| CPI | Cycles Per Instruction. The 65816 spends 2–7, many wasted on bus protocol. Lowering it buys throughput without touching the critical path, which is why it beats chasing clock. |
| Microcode | The internal table that sequences each instruction. On a soft core it sits in block RAM, and registering its output is the standard first move against the critical path. |
| Cycle-exact | A core reproducing the original's timing cycle for cycle. Needed to emulate a specific machine; unnecessary here, where only programmer-visible behaviour must match (→ [E.6](sec_e#e6)). |
| ILP | Instruction-Level Parallelism — independent work a core can overlap. An accumulator architecture offers little, which bounds what any 65816 core can gain from pipelining. |
| Accumulator architecture | A design where most operations route through one register. Compact to encode, heavy on memory traffic, and inherently serial. |
| Bus arbiter | The logic deciding who drives the shared bus in each cycle: CPU, cache fill, video, or refresh. |
| PIC | Programmable Interrupt Controller — collects device IRQs, applies priorities, and raises IRQ/NMI to the CPU. |
| EBR | Embedded Block RAM — RAM blocks inside the iCE40. Its 128 Kbit are the reason only the TLB fits on-chip while the page table lives in external SRAM. |
| HDL | Hardware Description Language — Verilog or VHDL. The choice is still open ([Q2](sec_q#q2)). |
| Stub | In layout, a track branching off a bus. Long stubs ruin signal integrity at ~100 MHz, hence the short comb of [F.13](sec_f#f13). |

## Board, power and configuration
  NOTE: → [sheet C](sec_c) · [sheet D](sec_d)

| Term | Meaning |
|---|---|
| EC | Embedded Controller — the always-on microcontroller handling power, boot and housekeeping. Here, the RP2040. |
| Rail | A supply voltage distributed across the board (3V3, 1.2V, VPP…). "Sequencing" is the order they come up in. |
| SYS | The node after the charger, fed by USB or battery indifferently. Everything hangs off it rather than off the battery. |
| Power-path | Charger topology that powers the system and charges the cell at once, so the machine runs on USB with a flat or absent battery. |
| 1S | One lithium cell in series — a nominal ~3.7V pack. |
| Fuel gauge | Chip that estimates remaining charge from voltage and current history. Here the MAX17048, read by the EC over I2C. |
| Buck-boost | Converter holding its output steady whether the input is above or below it — needed because a cell crosses 3.3V as it drains. |
| Boost | Converter that only steps voltage up. Used for the backlight and the 5V host VBUS. |
| LDO | Low-DropOut regulator — simple linear regulator, used for the 1.2V FPGA cores. |
| Load switch | A controlled switch cutting power to a whole block — here the panel, off by default. |
| Always-on | A rail that stays up whenever the machine has any power, so the EC can run before anything else exists. |
| SSPI | Slave SPI — the iCE40 configuration mode in which an external master (the RP2040) clocks the bitstream in. |
| Bitstream | The compiled gateware file loaded into an FPGA at every power-up. iCE40s are SRAM-based: they forget on power-off. |
| CRESET_B / CDONE | The iCE40's configuration handshake: held low to start loading, raised by the FPGA when configuration succeeded. |
| Bring-up | First powering of a new board, block by block, verifying each before enabling the next. |
| Free-run | Diagnostic where the CPU is fed a constant NOP so it just counts through addresses — proves clock, reset and address bus without any memory. |
| Handoff | Transfer of a shared resource between two owners — here the microSD passing from the RP2040 to Helium through the '3257 mux. |
| TQFP · PLCC · TSOP · BGA | Chip packages. The first three have accessible leads and can be hand-soldered; BGA hides its balls underneath and cannot, which is why it is excluded ([D01](sec_q#d01)). |

## Video, audio and peripherals
  NOTE: → [sheet H](sec_h)

| Term | Meaning |
|---|---|
| Chip RAM | Amiga term reused here: Neon's own 64 MB SDRAM (framebuffer + audio DMA), outside the CPU hierarchy and reached through the `$FE` window. |
| Framebuffer | The region of memory holding the pixels currently on screen. Exposed to processes as `/dev/fb`. |
| VRAM window | The 64 KB opening in bank `$FE` through which the CPU reaches video memory, gated by the MMU's VRAM_SEL. |
| Px-doubling | Drawing at 512×300 and emitting each pixel twice in both axes to fill 1024×600 — quarters the framebuffer and its bandwidth. |
| RGB-TTL | Parallel video interface: one wire per colour bit plus sync and clock. No bridge chip needed, at the cost of many pins. |
| VSYNC | The pulse marking the end of a frame. Raised as an IRQ so the GUI can redraw without tearing. |
| FPC | Flexible Printed Circuit — the flat ribbon connecting the panel. "FPC-50" is its 50-contact connector. |
| eDP | Embedded DisplayPort — serial panel interface. Rejected for v1 as it needs a bridge chip ([D05](sec_q#d05)). |
| R-2R | A resistor-ladder DAC — the cheapest way to get analogue VGA levels out of FPGA pins. Bring-up only. |
| I2S | Serial audio bus between the FPGA and the DAC. Unrelated to I2C despite the name. |
| QSPI | Quad SPI — 4-bit-wide serial bus. Historical here: it went with the PSRAM ([D13](sec_q#d13)) and no longer appears on the board. |
| HID | Human Interface Device — the USB class covering keyboards and mice. |
| PIO-USB | USB host implemented on the RP2040's programmable I/O blocks, since the chip has no hardware host controller. |
| USB-CDC | Communications Device Class — makes the RP2040 appear as a serial port on the development PC. Carries the console. |
| Mux | Multiplexer — switch routing one set of signals to one of several destinations. The '3257 gives the microSD two possible owners. |
| DMA | Direct Memory Access — a device reading or writing memory itself, without the CPU moving each byte. |

## Operating system and toolchain
  NOTE: → [sheet I](sec_i) · [sheet J](sec_j) · [sheet N](sec_n) · [sheet O](sec_o)

| Term | Meaning |
|---|---|
| BIOS | Here it means two things kept distinct: the RP2040 is the "board BIOS" (everything pre-CPU), and `BIOS.BIN` is the "system BIOS" (everything post-reset). |
| Info block | The structure the BIOS hands the kernel at load time: RAM size, device map, battery state, RTC time. Format still open ([Q5](sec_q#q5)). |
| Monitor | Minimal interactive debugger: examine and alter memory, load over serial. Two of them exist — one in the BIOS, running on the 65816, and the RP2040 console of [sheet R](sec_r), which covers the same ground from outside the CPU and is available a whole stage earlier. |
| Kernel | The resident, privileged core of the OS. Lives in virtual bank `$01`, pinned in SRAM. |
| Syscall | A service request from a process to the kernel. Invoked here through the `COP` instruction, with the service number in the accumulator. |
| PCB | Process Control Block — the per-process record holding saved registers, ASID and page-table pointer. ((Not the printed circuit board, which this document always spells out.)) |
| Context switch | Switching process; entails saving state to the PCB and pointing the MMU at a different page table. |
| Preemptive | The kernel takes the CPU back on its own (on the timer tick) rather than waiting for the process to yield it. |
| Round-robin | Scheduling that simply cycles through ready processes in turn, each getting one quantum. |
| Tick | The periodic timer interrupt driving scheduling. 100 Hz here. |
| Zombie | A finished process whose exit status is still held for its parent to collect with `wait`. |
| Driver | Kernel module handling one device behind a fixed 5-function interface, exposed as a `/dev/*` node. |
| devfs | The synthetic filesystem where those `/dev/*` nodes live — no bytes on the SD card. |
| ioctl | The escape hatch of the unified I/O interface: device-specific operations that are neither read nor write, such as mapping the framebuffer. |
| FAT | The filesystem on the microSD. Chosen so any PC can write the boot card. |
| BSS | The zero-initialised data of a binary. Carries no bytes in the file: the loader just maps it and clears it. |
| Relocation | Patching a binary's addresses to match where it was actually loaded. The MMU removes the need: every process sees the same addresses. |
| ABI | Application Binary Interface — the contract user binaries rely on. Drivers may be rewritten as long as it holds. |
| JSL / RTL | The 65816's long call and return, crossing banks. The basis of the large memory model and of syscall stubs. |
| Large model | Compiler model where code is addressed across all banks with JSL/RTL. Paired here with a fixed DBR so data access stays cheap. |
| Toolchain | The full chain from source to loadable artefact. Open end to end here: KiCad · Yosys · nextpnr · IceStorm · pico-sdk · ca65/64tass. |

## Debug and instrumentation
  NOTE: → [sheet R](sec_r)

| Term | Meaning |
|---|---|
| Debug agent | The block inside Helium that performs memory and bus accesses on command from the RP2040. A requester in the arbiter, not an external master. |
| Internal access | A transaction satisfied entirely inside Helium — SRAM, SDRAM or Helium's own registers. Nothing appears on the CPU bus. |
| External cycle | A transaction where Helium drives the CPU bus pins, so devices outside it see a cycle indistinguishable from the 65816's. The only way to reach the `$FE` aperture. |
| PHI2 stall | Freezing the CPU clock to steal bus time or to halt. Legal because the core is static; the same gating logic serves cache fills, halts and external cycles. |
| Cycle stealing | Taking the bus within a window the CPU is not using, instead of stalling it. The cheaper of the two paths when it is available. |
| Trace buffer | Ring buffer in EBR recording one entry per bus cycle — address, data, and the control pins. A logic analyser built into the gateware. |
| Trigger · arming | The address-and-mask comparison that starts or stops a capture, and the act of enabling it. Position selects whether the captured window sits before, around or after the match. |
| Scan chain | The register-readout path debuggable CPUs provide. The W65C816S has none, which is why its registers can only be inferred or dumped by a stub (→ [R.15](sec_r#r15)). |
| Debug stub | A short routine that pushes every CPU register to a known location. Authoritative and intrusive — it perturbs the state it reports, so the console never runs it silently. |
| W1C | Write-1-to-Clear — a status bit cleared by writing a one to it, so two readers cannot clear each other's flags by accident. |
| March test | Memory test that walks a pattern up and down the array in a defined order, catching stuck, coupled and address-decode faults that a simple write-read pass misses. |
| Walking ones · address-in-address | The two other standard patterns: one bit moved through each word, and each cell holding its own address. The second is what finds a swapped address line. |
| Turnaround | The gap a bus needs between one driver releasing and the next asserting. Sized in clocks from the datasheet, never guessed — too short shorts two drivers, too long wastes the cycle. |
| Clock domain crossing | Passing a signal between two unrelated clocks — here the SPI link and the Helium core. Needs synchronisers and a handshake, and is a classic home of intermittent faults. |
| GDB stub | Firmware speaking the GDB remote serial protocol, letting a host debugger drive the target. Deferred until the command set stops moving (→ [R.24](sec_r#r24)). |
| Breakpoint · watchpoint | Halt on reaching an address · halt on touching a datum. The first is `BRK` or a trace-trigger comparator; the second is deferred. |
