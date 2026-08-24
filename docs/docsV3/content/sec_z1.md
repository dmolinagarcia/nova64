# Appendix — glossary
> every acronym and term · grouped by domain

The document's terminology in one place. Every acronym is also expanded on first use where it appears, so this sheet is deliberately redundant — it exists to be jumped into, not read through. It is grouped by domain, and within each group runs roughly in the order the concepts appear. The figures have a sheet of their own ([Z2](sec_z2)).

## Virtual memory and the MMU
  NOTE: → [sheet K](sec_k) · [sheet L](sec_l)

| Term | Meaning |
|---|---|
| Virtual memory | A layer of indirection between the addresses the CPU generates and those that reach the RAM. Every process here sees a clean, contiguous 16 MB of it, and the indirection is Helium's: the 65816 has no MMU of its own. |
| Virtual address | The address the CPU emits — what the program "believes". 24 bits here. |
| Physical address | The address that reaches the SRAM/SDRAM chips. 27 bits here. 27 bits here — a 128 MB ceiling, with the SDRAM frames at the bottom of that space and the pinned SRAM frames at the top ([L.6](sec_l#l6)). |
| Page | A fixed-size block of the virtual space. 2 KB. 8,192 of them per process, and the size is deliberately decoupled from the 256 B granularity a fill actually moves. |
| Frame | A block of the same size in physical memory. To translate is to pair a page with a frame. 32,768 of them across the SDRAM, tracked by a bitmap in the kernel. |
| VPN | Virtual Page Number — high bits of the virtual address; identifies the page. 13 bits. |
| Offset | Low bits; the byte within the page. Never translated. 11 bits. |
| MMU | Memory Management Unit — the hardware that translates. Not in the CPU here: Helium implements it. The TLB, the hardware walker, the permission checks and the `ABORTB` that reports a failure are all Helium's. |
| Page table | An array indexed by VPN holding the PTEs. One per process, 32 KB. Flat, not multi-level: 8,192 entries × 32 bits, held in the pinned SRAM region so the walker reads it directly. |
| PTE | Page Table Entry — one entry of that table. 32 bits: frame number plus flags. 16 bits of frame number plus the flags below — which is what puts the physical ceiling at 128 MB. |
| PTE flags | `P` present in RAM · `W` writable · `X` executable · `U` reachable from user mode · `D` dirty (modified since load) · `A` accessed recently · `NC` non-cacheable · `PIN` non-evictable · `SW` free for the kernel's own use. |
| TLB | Translation Lookaside Buffer — cache of VPN→frame translations inside the MMU. Avoids re-reading the table on every access. |
| TLB hit / miss | Success or failure in that cache. A miss costs one table walk; it is not an error. |
| TLB reach | Entries × page size — the working set the TLB can cover before it starts thrashing. 64 KB here with 32 entries; the main thing a larger page buys. |
| Page walk / walker | The traversal of the page table that resolves a miss. Hardware here (≈100 ns, four 8-bit SRAM accesses); a software exception in other architectures. |
| ASID | Address Space IDentifier — tag marking which process each TLB entry belongs to. Without it the whole TLB would be flushed on every context switch. |
| Page fault | Exception raised on access to a page with `P=0`. Recoverable. Delivered as an ABORT; the handler grows a heap or stack, resolves a copy-on-write, or kills the process ([L.14](sec_l#l14)). |
| Demand paging | Loading pages only when touched rather than all up front. Deferred to post-v1. |
| Eager loading | Mapping and filling every segment at `exec` time. What v1 does, in place of demand paging. |
| Swapping | Evicting rarely used pages to storage in order to free frames. Order matters: flush the frame's dirty sub-blocks to SDRAM before writing it to the card ([L.16](sec_l#l16)). |
| Pinning | Marking a page as non-evictable. Required for the kernel, the interrupt vectors and the bank `$00` stack. It is a physical address range rather than a cache attribute, so an access there skips tag lookup entirely and completes at fixed latency. |
| Memory protection | Preventing a process from reading or writing outside its own space. Achieved through the PTE flags, not through translation itself. |
| Cache line | The tagged unit of Helium's cache — 2 KB, one tag per line, but filled and written back in 256 B sub-blocks. |
| Sub-block | The 256 B unit a fill actually moves, with its own valid and dirty bits. Keeps a miss at ~6 µs instead of ~41 µs and bounds interrupt latency. |
| Set associative | Cache organization where a given address may sit in any of N positions ("ways") within one set. Four ways here; direct mapping would put 32,768 physical lines in competition for 384 slots. |
| Tag | The bits stored alongside a cache line saying which physical line it currently holds. Kept in EBR, ~1.5 KB, beside the TLB. |
| Dirty victim | The line a fill has to evict when it has been written to — it must go back to SDRAM first, which is why a miss can cost double. |
| Write-back | Cache policy where writes stay in the cache and reach memory only on eviction. Implied by the dirty bits. It is also why a frame is flushed before eviction and invalidated before a DMA engine fills it. |
| Backing store | The memory a cache is a cache *of*. Here the SDRAM, which the PTEs always name. |
| Flush vs invalidate | The two ways to empty a cache line, and they are not interchangeable. **Flush** writes dirty contents back and then marks the line clean — used before the CPU's data has to be visible to someone else. **Invalidate** drops the line without writing anything, dirty or not — used when the contents are about to be overwritten by someone else. Getting them backwards loses data in one direction and merely wastes a burst in the other ([M.12](sec_m#m12), [G.9](sec_g#g9)). |
| Copy-on-write | Sharing a page read-only between processes and duplicating it only when one writes. An ABORT case ([L.4](sec_l#l4)). The downgrade to read-only is one `TLB_INVAL_PAGE`, not a full flush. |
| Static core | A CPU whose state survives an arbitrarily slow or stopped clock. The W65C816S is one, which is what makes the PHI2 stall legal. |
| Clock gating | Stopping and restarting PHI2 in gateware. Must be glitch-free, and the first pulse on resume must meet the minimum high width. The same gate serves the cache-miss stall, Neon's busy stall and the machine's low-power state. |
| Trampoline | The short privileged stubs in bank `$00` that receive every vector — the CPU forces PBR=0 on entry — and `JSL` straight into the real kernel in `$01` (→ [L.11](sec_l#l11)). |
| ASID recycling | Reassigning a used ASID to a new process. Legal only after flushing that ASID's TLB entries; skipping the flush leaks the old address space into the new one, silently (→ [M.8](sec_m#m8)). |
| Address as opcode | Command convention where each operation has its own address and the value written is its argument, not a command code (→ [M.2](sec_m#m2)). |
| WDM | Opcode `$42`, reserved by WDC and executed as a two-byte, two-cycle no-op. Considered as a command channel and rejected; it stays a no-op, and a softcore must implement it as one. |
| Memory barrier | An instruction forcing memory ordering. Not needed here and not provided: the 65816 has no prefetch queue and no store buffer, so every bus cycle is already in program order. |
| Watchdog | Timer that resumes PHI2 and raises NMI or ABORT if a fill overruns — without it a hung fill freezes the machine with no clock and no diagnostic path. It covers Neon's read stall too, which is what makes an unfitted Neon a fault report rather than a dead board ([F.11](sec_f#f11)). |
| Refresh | The periodic recharge SDRAM needs to keep its contents. Must be interleaved into the fill state machine, not deferred until it finishes. |

## CPU, bus and gateware
  NOTE: → [sheet B](sec_b) · [sheet F](sec_f)

| Term | Meaning |
|---|---|
| Bank | A 64 KB slice of the 65816's 24-bit space, selected by DBR/PBR. The virtual bank map is in [L.10](sec_l#l10). The map is fixed: `$00` stack, direct page and vectors · `$01` kernel · `$02`–`$FD` user · `$FE` VRAM window · `$FF` privileged I/O. |
| DBR · PBR · DP | Data Bank, Program Bank and Direct Page registers — the 65816 state that decides which bank an access lands in. |
| Native mode | The 65816's 16-bit, 24-bit-address mode, entered with `CLC/XCE`. The opposite is 6502 emulation mode. The M and X flags then pick 8- or 16-bit accumulator and index registers at run time. |
| PHI2 | The CPU's master clock, generated by Helium. Target 8 MHz. Target 8 MHz at 3.3V. Helium may hold it in either state indefinitely — the core is fully static — which is what a cache-miss stall does. |
| BE | Bus Enable — the pin that makes the physical CPU tristate its address, data and RWB drivers and release the bus. Driven by Helium, which uses it far more often than Argon would (→ [R.3](sec_r#r3)). |
| RESB | The 65816's active-low reset. Released by the RP2040 as the last step before the CPU runs. Pulled down when no bitstream is loaded, so a blank board holds the CPU in reset instead of running it into an undriven bus. |
| ABORTB | W65C816S pin that cancels the instruction in progress without side effects. The system's fault mechanism. Asserted by the MMU on a missing page or a permission violation, and the instruction restarts once the kernel has fixed the mapping. |
| VPA / VDA | Pins indicating whether the cycle is an instruction fetch or a data access. They make checking the `X` flag possible. |
| RDY | Bidirectional, open-drain: Helium pulls it low to freeze the CPU while it takes the shared nets ([F.4](sec_f#f4)), and the CPU pulls it low itself during `WAI`. **Cache-miss stalls do not use it** — [D16](sec_q#d16) moved those to PHI2 gating, since some W65C816S revisions ignore RDY during write cycles. |
| Northbridge | Borrowed PC term for the chip sitting between CPU and memory. Here it is Helium: MMU, cache, arbiter, timer and I/O. |
| Gateware | The logic loaded into an FPGA — the FPGA equivalent of firmware. Written in HDL, not compiled to instructions. |
| Softcore | A CPU implemented as gateware rather than as a chip. Argon's optional 65816 is one (→ [sheet E](sec_e)). The footprint ships unpopulated, and what a core owes the board is a pin list as much as an instruction set ([E.13](sec_e#e13)). |
| Fmax | The highest clock a placed-and-routed design will meet timing at. A property of the design *on a given part* — quoting one across FPGA families is meaningless. |
| CPI | Cycles Per Instruction. The 65816 spends 2–7, many wasted on bus protocol. Lowering it buys throughput without touching the critical path, which is why it beats chasing clock. |
| Microcode | The internal table that sequences each instruction. On a soft core it sits in block RAM, and registering its output is the standard first move against the critical path. |
| Cycle-exact | A core reproducing the original's timing cycle for cycle. Needed to emulate a specific machine; unnecessary here, where only programmer-visible behaviour must match (→ [E.6](sec_e#e6)). |
| ILP | Instruction-Level Parallelism — independent work a core can overlap. An accumulator architecture offers little, which bounds what any 65816 core can gain from pipelining. |
| Accumulator architecture | A design where most operations route through one register. Compact to encode, heavy on memory traffic, and inherently serial. |
| Bus arbiter | The logic deciding who drives the shared bus in each cycle: CPU, cache fill, video, or refresh. The CPU is stalled rather than queued: Helium takes the shared nets with `BE` and hands the clock back afterwards. |
| PIC | Programmable Interrupt Controller — collects device IRQs, applies priorities, and raises IRQ/NMI to the CPU. Neon's three sources arrive on one wire and are demultiplexed by the kernel reading `IRQ_STATUS`. |
| EBR | Embedded Block RAM — 32 dual-port blocks of 512 B inside each iCE40, 16 KB in total. On Helium its scarcity is why only the TLB and the cache tags fit on-chip while the page table lives in external SRAM; on Neon it is the whole of Mode 0, and it is **fully allocated with zero margin** ([T.12](sec_t#t12)). The prototype board's ECP5 has ~243 KB of it, roughly fifteen times as much, which is the single most misleading number on that platform ([P.04](sec_p#p04)). |
| Bitstream initialisation | Giving a block RAM its contents from the compiled bitstream rather than loading them at run time. What makes Neon's font and text buffer valid before any software exists — the single most useful property of the design ([D27](sec_q#d27)). |
| HDL | Hardware Description Language — Verilog or VHDL. The choice is still open ([Q2](sec_q#q2)). |
| Stub | In layout, a track branching off a bus. Long stubs ruin signal integrity at ~100 MHz, hence the short comb of [F.13](sec_f#f13). |
| Expansion slot | The parallel connector of [sheet X](sec_x): the machine's own bus, buffered, with a card as a third tenant of the contract Helium already has with Neon. Unpopulated in the first build. |
| `/EXP_SEL` · `/EXP_BSY` · `/EXP_IRQ` | The slot's three nets — select, stall, interrupt — mirroring `NEON_BUS_SEL`/`BSY`/`IRQ`, with `/EXP_BSY` idling **not busy** because an empty slot must not stall the machine ([X.11](sec_x#x11)). |
| Slot aperture | The 1 MB per slot the MMU can map into a process, carved out of the frame numbers [F.10](sec_f#f10) leaves unpopulated and addressed on A11–A19, which are idle while the SRAM's `/CE` is ([X.8](sec_x#x8)). Always non-cacheable. |
| Register window vs aperture | The two ways a card is reachable, and the split is [T.21](sec_t#t21)'s: **control** in bank `$FF`, unmappable to user space by construction; **bulk** through the aperture, mappable with permissions in front of it. |
| Geographic addressing | Telling a card which slot it is in with connector pins strapped differently in each position, so the same board works in either one with no jumper ([X.14](sec_x#x14)). |
| Bus master | A device that drives the address bus instead of the CPU. Here: Helium, and nothing else — a card cannot honour the cache-coherence discipline of [M.12](sec_m#m12), so [D48](sec_q#d48) refuses it the bus rather than trusting it ([X.16](sec_x#x16)). |
| `I2C-EXP` | The third I2C bus, Helium's own and the only one where Helium is master. The slow expansion tier: devices mirrored into I/O windows rather than reached transactionally ([X.22](sec_x#x22)). |
| Window (slow bus) | A copy of a device's registers in Helium's block RAM, refreshed by a sequencer and read by the CPU at bus speed. A **mirror, not a proxy** — a byte over I2C is ~90 µs and nothing may stall PHI2 for that ([X.23](sec_x#x23)). |

## Board, power and configuration
  NOTE: → [sheet C](sec_c) · [sheet D](sec_d) · [sheet S](sec_s)

| Term | Meaning |
|---|---|
| EC | Embedded Controller — the always-on microcontroller handling power, boot and housekeeping. Here, the RP2040. It stays off the shared bus entirely, and its W25Q16 is the board's only mandatory flash. |
| Rail | A supply voltage distributed across the board (3V3_AON, 3V3_MAIN, 1V2, VPP…). "Sequencing" is the order they come up in. Every logic rail here is 3.3V, which is what keeps level shifters off the board. |
| SYS | The node after the charger, fed by USB or battery indifferently. Everything hangs off it rather than off the battery. |
| Power-path | Charger topology that powers the system and charges the cell at once, so the machine runs on USB with a flat or absent battery. |
| 1S | One lithium cell in series — a nominal ~3.7V pack. Two 1S cells in *parallel* are still 1S: more capacity, same voltage, no balancing. |
| Fuel gauge | Chip that estimates remaining charge from voltage and current history. Here the MAX17048, read by the EC over I2C. Distinct from an ADC, which reports a voltage and models nothing. |
| Buck-boost | Converter holding its output steady whether the input is above or below it — needed because a cell crosses 3.3V as it drains. |
| Boost | Converter that only steps voltage up. Used for the backlight and the 5V host VBUS. |
| LDO | Low-DropOut regulator — a linear regulator, which wastes the voltage it drops as heat. The 1.2V rail used to be one and is now a buck ([D20](sec_q#d20)). |
| Always-on · AON | The domain powered whenever SYS exists: the EC, its flash, the PD sink, the charger/gauge bus and the power button. Nothing else belongs there. |
| Switched domain | Everything whose rail the EC can turn off — the FPGAs, the CPU, the memories, the panel, the touch bus. Off by default at EC reset. |
| Iq | Quiescent current — what a converter draws on its own, at no load. Decisive for an always-on rail, irrelevant for a switched one. |
| PG · Power Good | An open-drain output asserting that a converter has reached regulation. Used here as the sequencing interlock: the EC waits on it before raising the next rail. |
| PD · Power Delivery | The USB-C protocol by which a sink asks for more than the default 5V, negotiated over the CC conductors. Here 9V, requested by a resistor. |
| OTG | A charger mode in which the device sources 5V onto its own VBUS. Present on the BQ25896 and deliberately unused ([D21](sec_q#d21)). |
| Ship mode | A charger state disconnecting the battery from SYS entirely, leaving only leakage. The machine's deepest off state; the button's `QON` pin wakes it. |
| QON | The BQ25896 pin that resets the battery FET from hardware, with no firmware involved. The floor under every other way of turning the machine off. |
| Ordered shutdown | The graceful path: the kernel flushes and confirms before any rail drops. Its opposite is *forced*, where the EC stops waiting for an answer. |
| Watchdog | Here, the EC's 10-second bound on how long a kernel may take to acknowledge a shutdown request. Without one, a hung kernel hides itself as a machine that will not turn off. |
| Staging · live · shadow | The three copies of the telemetry bank. The EC dribbles bytes into staging, one commit publishes them to live, one latch snapshots live into shadow — so an 8-bit CPU can read a 16-bit value without it changing underneath ([S.16](sec_s#s16)). |
| Tearing | Reading a multi-byte value while it is being updated, and getting halves from two different samples. The reason for the snapshot mechanism above. |
| SSPI | Slave SPI — the iCE40 configuration mode in which an external master (the RP2040) clocks the bitstream in. |
| Bitstream | The compiled gateware file loaded into an FPGA at every power-up. iCE40s are SRAM-based: they forget on power-off. |
| CRESET_B / CDONE | The iCE40's configuration handshake: held low to start loading, raised by the FPGA when configuration succeeded. |
| Bring-up | First powering of a new board, block by block, verifying each before enabling the next. Each stage of [sheet P](sec_p) closes on an explicit `TEST ▸`, not on a judgement. |
| Free-run | Diagnostic where the CPU is fed a constant NOP so it just counts through addresses — proves clock, reset and address bus without any memory. |
| Handoff | Transfer of a shared resource between two owners — here the microSD passing from the RP2040 to Helium through the '3257 mux. The RP2040 releases the card before Helium's controller claims it — nothing arbitrates it in hardware. |
| TQFP · PLCC · TSOP · BGA | Chip packages. The first three have accessible leads and can be hand-soldered; BGA hides its balls underneath and cannot, which is why it is excluded ([D01](sec_q#d01)). |

## Video, audio and peripherals
  NOTE: → [sheet H](sec_h) · [sheet T](sec_t) · [sheet U](sec_u) · [sheet W](sec_w)

| Term | Meaning |
|---|---|
| Chip RAM | Amiga term reused here: Neon's own 64 MB SDRAM — framebuffers, atlases, level buffers, command lists and audio DMA — outside the CPU hierarchy and reached through the `$FE` window. |
| Framebuffer | The region of memory holding the pixels currently on screen. Exposed to processes as `/dev/fb`. It lives in Neon's SDRAM rather than in system memory: the CPU reaches it through the `$FE` aperture and never has to read it back. |
| VRAM window | The 64 KB opening in bank `$FE` through which the CPU reaches video memory, gated by the MMU's NEON_BUS_SEL. In graphics modes its low 32 KB slides over SDRAM by `VRAM_PAGE`. |
| NEON_BUS_SEL | Helium → Neon: "this cycle is validated for you." Qualifies **both** of Neon's windows — `$FE` after translation and the permission check, `$FF:8000`–`$FF:80FF` after the bank decode and the privilege check ([B.6](sec_b#b6)). |
| NEON_BUS_BSY | Neon → Helium: "I cannot serve this cycle yet." **Busy by default**, so an absent or unconfigured Neon stalls the machine into a watchdog report rather than onto an undriven bus. Its meaningful edge is the deassertion, which means the data is ready ([T.18](sec_t#t18)). |
| Bus-busy stall | How the CPU reads VRAM: Helium decodes the read, holds PHI2 **low** unconditionally, and raises it when Neon clears busy. Stall first, ask afterwards — which removes the race a wait line asserted against the rising edge would have had ([D37](sec_q#d37)). |
| Aperture | The same opening, named from Neon's side. **Data only** — Neon's control registers are not in it but at `$FF:8000`, because `$FE` is user-mappable and `$FF` is not ([D36](sec_q#d36)). |
| Px-doubling | Drawing at a lower resolution and emitting each pixel more than once in both axes to fill 1024×600. Not a policy here but a mode: **Mode 2b** at 2×2, and Mode 2a at 3×3 ([D35](sec_q#d35)). |
| RGB-TTL | Parallel video interface: one wire per colour bit plus sync and clock. No bridge chip needed, at the cost of many pins. Driven here at 18 bpp — RGB666. |
| VSYNC | The pulse marking the end of a frame. Raised as an IRQ so the GUI can redraw without tearing. One of the three sources aggregated onto `NEON_IRQ`. |
| Cell · glyph | One character position — a glyph code plus an attribute byte · the pixel pattern for one code, held in the font. 8 × 16 px here, the standard VGA text cell. |
| Text buffer | The 4096 cells of Mode 0, 8 KB in Neon's block RAM, initialised from the bitstream so the screen is correct at power-on with no software involved. |
| Ring buffer | A buffer addressed modulo its size, so advancing a pointer rotates the visible contents without moving data. `TEXT_START` scrolls the console this way — 256 bytes written instead of 8,192 moved. |
| Blitter | Block image transferrer — hardware that fills, copies and combines rectangular memory regions with no CPU involvement. The centre of Neon's graphics half. |
| Channel | One of the blitter's four memory ports under the model of [sheet U](sec_u) — A, B, C read, D writes. Each carries a pointer, a signed modulo and an enable; a disabled read channel supplies a constant and costs no bandwidth. |
| Minterm · logic function | One row of the truth table combining channels A, B and C. Three inputs give eight rows, so the whole function is one byte — the `LF` — selecting any of **256 operations**. `$0C` copies, `$CA` cookie-cuts, `$3C` is XOR. |
| Cookie-cut | Compositing a masked sprite over a background in one pass: mask on A, pixels on B, destination read back on C, `LF = $CA`. Needs a mask surface in memory, which a colour key does not. |
| Modulo | The signed value added to a channel's pointer at the end of every line — the difference between a bitmap's stride and the width being moved. **It is what makes a blit two-dimensional**, and it expresses clipping with no extra hardware. |
| Surface | Any rectangular pixel buffer in Neon's SDRAM: framebuffer, window backing store, font atlas, icon sheet. |
| Backing store | A surface holding one window's complete contents, **including the parts currently hidden behind other windows** — exactly what a flat framebuffer does not keep ([Q60](sec_q#q60)). |
| Compositor | The kernel code that assembles the visible framebuffer from the backing stores, driving the blitter. Distinct from the window manager, which decides what should be where. Which model it runs is still open: repaint every window intersecting the damage, or recompose from backing stores ([Q60](sec_q#q60)). |
| Descending mode | Blitting with the pointers decrementing rather than incrementing. Required whenever source and destination overlap and the move is down-and-right — a window drag is the case. |
| Damage · dirty rectangle | A region of the screen whose composited content is no longer valid and must be rebuilt. Under [U.31](sec_u#u31) a bug here costs performance, not corruption, because full recomposition stays correct. |
| Cookie-cut vs colour key | The two ways to make a sprite non-rectangular. A key designates one palette index transparent and needs no extra memory; a mask is a second bitmap and needs generating and keeping in step. The blitter model decides which is available ([Q58](sec_q#q58)). |
| Colour key | A palette index designated transparent; pixels of that value are not written during a copy. What makes a sprite a sprite here, with no sprite engine. |
| Barrel shifter | Logic shifting a word by any amount in one cycle. Required to place a 1-bpp image at an arbitrary horizontal position, which is the expensive part of Mode 1 and not optional. |
| Stride | The byte distance from the start of one image row to the next. Independent per source and destination, which is what lets one copy window a viewport out of a much wider buffer. |
| Command list | A sequence of drawing commands in SDRAM, executed autonomously by Neon. The ANTIC display list and the Amiga Copper, generalised to include the blitter. |
| Display list patching | Writing into a list while Neon executes it — the intended usage pattern, not an abuse. Moving an object is one 32-bit store to one word ([T.41](sec_t#t41)). |
| Page flip | Displaying one framebuffer while drawing into another, then exchanging them. `SWAP_BUFFERS`, applied at vblank so a half-drawn frame is never shown. |
| Damage model | Dirty rectangles, z-order traversal and repaint from application state. Unavoidable with a flat framebuffer, because moving a window does not restore what was under it — the pixels were overwritten and are gone. |
| Save-under | Copying a region about to be occluded and restoring it afterwards. Works here despite the CPU not needing to read VRAM, because the *blitter* moves it and the pixels never leave VRAM. |
| Service port | Neon's four configuration SPI pins, reused after `CDONE` as a control channel from the EC. The reason boot progress appears on screen before a CPU exists. |
| Clock enable | A signal gating a register's update without gating its clock. Used to derive Neon's 51.5625 MHz pixel rate inside one 103.125 MHz domain, so there is no second clock domain to cross. |
| Row activation | The delay when SDRAM must open a new row before its data is reachable, ~60 ns. Why glyph lookup stays in block RAM, and why an aperture read has to stall the CPU. |
| SDRAM bank | One of four independent arrays inside the device; accesses to different banks interleave with no row-activation penalty. A layout convention for copies, not something hardware enforces ([Q50](sec_q#q50)). |
| FPC | Flexible Printed Circuit — the flat ribbon connecting the panel. "FPC-50" is its 50-contact connector. |
| eDP | Embedded DisplayPort — serial panel interface. Rejected for v1 as it needs a bridge chip ([D05](sec_q#d05)). |
| R-2R | A resistor-ladder DAC — the cheapest way to get analogue VGA levels out of FPGA pins. Bring-up only on the target board; **the whole video output on the prototype**, at 6 bits per channel into 1 % resistors ([P.08](sec_p#p08)). |
| I2S | Serial audio bus between the FPGA and the DAC — `BCK` the bit clock, `LRCK` the frame clock, `DIN` the data. Unrelated to I2C despite the name. |
| fs · 32fs | The sample rate · a bit clock of 32 `BCK` per stereo frame, which is exactly two 16-bit slots with no padding. **The frame length decides the divider's granularity**, which is why this machine uses 32fs and not the more common 64fs ([W.6](sec_w#w6)). |
| `SCK` grounded | Strapping that moves the PCM5102A's master clock inside the part, onto its own PLL locked to `BCK`. Costs one pin less and means **the sample rate is whatever Neon's divider produces** ([W.5](sec_w#w5)). |
| 44,146 Hz | This machine's actual sample rate — 103.125 MHz / (32 × 73). Not 44,100, because the iCE40 PLL is integer-only and 44.1 kHz needs a denominator factor of 441 that no setting can produce. +1.8 cents of pitch, published in `AUD_RATE` so nothing has to assume it ([W.11](sec_w#w11), [Q77](sec_q#q77)). |
| `XSMT` | The DAC's soft-mute pin, held low through an RC while the rail rises — **the anti-pop**. It protects power-up only; the power-down click is [Q76](sec_q#q76). |
| Saturating add | Clamping a sum at full scale instead of letting it wrap. Four channels at full volume exceed full scale, and a wrap turns the loudest moment in the material into full-amplitude noise ([W.27](sec_w#w27)). |
| Underrun | A DMA buffer running dry before software refills it. Here the channel **holds its last sample** rather than emitting zero — a step to zero is a click — and sets a sticky flag, because an underrun that cannot be observed is a bug report that says "it crackles sometimes" ([W.22](sec_w#w22)). |
| QSPI | Quad SPI — 4-bit-wide serial bus. Historical here: it went with the PSRAM ([D13](sec_q#d13)) and no longer appears on the board. |
| HID | Human Interface Device — the USB class covering keyboards and mice. |
| PIO-USB | USB host implemented on the RP2040's programmable I/O blocks, since the chip has no hardware host controller. |
| USB-CDC | Communications Device Class — makes the RP2040 appear as a serial port on the development PC. Carries the console. |
| Mux | Multiplexer — switch routing one set of signals to one of several destinations. The '3257 gives the microSD two possible owners. |
| DMA | Direct Memory Access — a device reading or writing memory itself, without the CPU moving each byte. The SPI-SD engine is the only one on this board, and it is the whole reason `CACHE_INVAL_FRAME` exists ([M.12](sec_m#m12)). |

## Operating system and toolchain
  NOTE: → [sheet I](sec_i) · [sheet J](sec_j) · [sheet N](sec_n) · [sheet O](sec_o)

| Term | Meaning |
|---|---|
| BIOS | Here it means two things kept distinct: the RP2040 is the "board BIOS" (everything pre-CPU), and `BIOS.BIN` is the "system BIOS" (everything post-reset). |
| Info block | The structure the BIOS hands the kernel at load time: RAM size, device map, battery state, RTC time. Format still open ([Q5](sec_q#q5)). |
| Monitor | Minimal interactive debugger: examine and alter memory, load over serial. Two of them exist — one in the BIOS, running on the 65816, and the RP2040 console of [sheet R](sec_r), which covers the same ground from outside the CPU and is available a whole stage earlier. |
| Kernel | The resident, privileged core of the OS. Lives in virtual bank `$01`, pinned in SRAM. Entered only through `COP` or an interrupt, both of which raise privilege on the vector fetch. |
| Syscall | A service request from a process to the kernel. Invoked here through the `COP` instruction, with the service number in the accumulator. |
| PCB | Process Control Block — the per-process record holding saved registers, ASID and page-table pointer. ((Not the printed circuit board, which this document always spells out.)) |
| Context switch | Switching process; entails saving state to the PCB and pointing the MMU at a different page table. |
| Preemptive | The kernel takes the CPU back on its own (on the timer tick) rather than waiting for the process to yield it. |
| Round-robin | Scheduling that simply cycles through ready processes in turn, each getting one quantum. |
| Tick | The periodic timer interrupt driving scheduling. 100 Hz here. 100 Hz, from a free-running counter in Helium — never from cycle counts, which stop being time the moment PHI2 can stall. |
| Zombie | A finished process whose exit status is still held for its parent to collect with `wait`. |
| Driver | Kernel module handling one device behind a fixed 5-function interface, exposed as a `/dev/*` node. |
| devfs | The synthetic filesystem where those `/dev/*` nodes live — no bytes on the SD card. |
| ioctl | The escape hatch of the unified I/O interface: device-specific operations that are neither read nor write, such as mapping the framebuffer. |
| FAT | The filesystem on the microSD. Chosen so any PC can write the boot card. The BIOS mounts it read-only, purely to find `KERNEL.BIN` and load it. |
| BSS | The zero-initialised data of a binary. Carries no bytes in the file: the loader just maps it and clears it. |
| Relocation | Patching a binary's addresses to match where it was actually loaded. The MMU removes the need: every process sees the same addresses. |
| ABI | Application Binary Interface — the contract user binaries rely on. Drivers may be rewritten as long as it holds. |
| JSL / RTL | The 65816's long call and return, crossing banks. The basis of the large memory model and of syscall stubs. |
| Large model | Compiler model where code is addressed across all banks with JSL/RTL. Paired here with a fixed DBR so data access stays cheap. |
| Toolchain | The full chain from source to loadable artefact. Open end to end here: KiCad · Yosys · nextpnr · IceStorm · pico-sdk · ca65/64tass. |

## The windowing OS
  NOTE: → [sheet V](sec_v)

| Term | Meaning |
|---|---|
| `wserver` | The window server: window objects, z-order, input routing, focus, decoration and client validation. A **user task** here, because none of that needs a privileged instruction — the compositor beside it is kernel code, because command emission does ([Q62](sec_q#q62)). |
| `tk` · `gcl` | The client-side libraries: the widget toolkit and the drawing layer that appends ops to the shared buffer. Both link into the application; neither is a server component. |
| Shared command buffer | The 4 KB page `mshare` maps writable in the client and readable in the server. Drawing ops accumulate in it and flush with one syscall — **a syscall per primitive would cost more than the primitive** ([V.20](sec_v#v20)). |
| Content list · composite list | The two kinds of command list a GUI keeps in SDRAM: one draws into a window's backing store and runs **when its content changes**, the other blits that store into the back buffer and runs **every frame**. Text lives in the first, which is why an idle window costs nothing. |
| String cache | Composed runs of glyphs held as a strip in SDRAM, so drawing a repeated label is one `COPY_RECT` instead of one command per character. Attacks emission cost, not bandwidth — and proportional fonts fall out of it free, since it stores runs rather than glyphs. |
| `DRAW_GLYPHS` | A proposed command taking a string pointer and a count, so a line of text is one command with no cache at all. **Complementary to the cache rather than a replacement**: the cache wins for stable labels, this wins for text that changes ([Q64](sec_q#q64)). |
| Refresh event | The message an Amiga client got when part of its window became visible again, obliging it to repaint. **There is none here**: the backing store already holds the pixels, so a client paints when its own state changes and never otherwise. |
| `layers.library` | The Amiga layer that answered *what is behind this window and who repaints it* — cliprects, `SIMPLE_REFRESH`, `SMART_REFRESH`, `SUPER_BITMAP`. Per-window backing stores delete the question, and the library with it. |
| Intuition · IDCMP · BOOPSI | Amiga's window system, its event protocol and its widget object model. Named here as the reference the design departs from: `wserver`, a ten-event set, and a static vtable toolkit respectively. |
| Z-order | The front-to-back ordering of windows. The compositor draws back to front — painter's algorithm — so occlusion needs no region arithmetic at all: the front window is simply drawn afterwards. |
| Decoration | The frame, title bar and gadgets the server draws **into the window's own backing store**, above the content area and clipped away from the client. Composited as one wide blit rather than four narrow ones, which is what [U.18](sec_u#u18) asks for. |
| Live drag | Moving a window by recompositing every frame rather than dragging an XOR outline and moving the pixels once. Affordable here, and the default. |
| Hit test | Walking the z-list front to back to find which window a pointer position belongs to. Decoration hits stay in the server; content hits are delivered to the client in window-relative coordinates. |
| Port · signal · message | The Exec model, proposed for this kernel: a port bound to one bit of a task's 32-bit signal mask, fixed 32-byte messages copied by the kernel, and `task_wait` as **the single blocking primitive** ([Q63](sec_q#q63)). |
| Coalescing | Collapsing a burst of events into the latest one — never more than one motion event per frame per client. Not an optimisation at ~12 µs of kernel copy per message, and it constrains the HID event format to absolute state rather than deltas. |
| Overdraw | Total composited area ÷ screen area, counting the desktop clear. The number that decides whether full recomposition fits: ~1.8× is comfortable at 1 bpp and consumes the whole frame at 8 ([V.10](sec_v#v10)). |
| Widget vtable | The toolkit's dispatch: a static `init · render · event · layout · attr` table per class, deliberately mirroring the kernel's five-function driver interface. ~15 cycles per call — right at widget granularity, wrong at pixel granularity. |
| Upload | The escape hatch for imagery Neon's primitives cannot produce: the client rasterises into system RAM and the server pushes it through the aperture. **~15 ms for a 200 × 100 patch at 8 bpp** — correct to provide, correct to discourage. |

## Debug and instrumentation
  NOTE: → [sheet R](sec_r)

| Term | Meaning |
|---|---|
| Debug agent | The block inside Helium that performs memory and bus accesses on command from the RP2040. A requester in the arbiter, not an external master. |
| Internal access | A transaction satisfied entirely inside Helium — SRAM, SDRAM or Helium's own registers. Nothing appears on the CPU bus. |
| External cycle | A transaction where Helium drives the CPU bus pins, so devices outside it see a cycle indistinguishable from the 65816's. The only way to reach the `$FE` aperture. |
| PHI2 stall | Freezing the CPU clock to steal bus time or to halt. Legal because the core is static; the same gating logic serves cache fills, halts, external cycles and Neon's read stall. |
| Cycle stealing | Taking the bus within a window the CPU is not using, instead of stalling it. The cheaper of the two paths when it is available. Available only where the CPU leaves a gap; where it does not, the stall above is the fallback. |
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

## The prototype board

| Term | Meaning |
|---|---|
| Prototype board | The single-ECP5 carrier of [sheet P](sec_p) — Helium and Neon merged on one commercial module, with the CPU, SRAM, EC, VGA and audio on a board of our own. Temporary packaging of the design, never a variant of it ([D40](sec_q#d40)). |
| Colorlight i9 v7.2 | The commercial module the prototype is built around: an LFE5U-45F with 8 MB SDRAM, 8 MB SPI flash, two unused Ethernet PHYs and a 25 MHz oscillator, on a DDR2 SODIMM edge connector. Sold for LED-panel drivers and widely used as a cheap FPGA board. |
| ECP5 | Lattice's larger FPGA family. Rejected for the target board by [D01](sec_q#d01) — BGA only in the useful sizes, and the project's rule is hand-solderable — which is exactly why it is acceptable on a module somebody else soldered. |
| SODIMM | The 200-pin edge connector the i9 module plugs into. It carries **raw FPGA balls**, not buffered I/O, which is what makes the module usable as a general-purpose carrier at all. |
| PMOD | The 2 × 6 header convention used by the i9 extension board. Whether its pins are direct or pass through unidirectional buffers decides whether the wire-wrap stage is possible ([Q67](sec_q#q67)). |
| DAPLink | The on-board debug probe of the i9 extension board, giving JTAG plus USB-CDC. Absent on a carrier that hosts the SODIMM socket directly, which is why the RP2040 picks up ECP5 JTAG there ([P.10](sec_p#p10)). |
| prjtrellis · nextpnr-ecp5 | The ECP5 half of the same open toolchain — Yosys and nextpnr, different backend. Added to [E0.1](sec_p#e01) by the prototype; no proprietary tools on either board. |
| Letterboxing | Showing a window into a buffer larger than the display rather than reflowing the buffer. How Mode 0 keeps its 128 × 32 geometry on a 640 × 480 monitor ([D44](sec_q#d44)). |
| Leak | A prototype convenience that survives into the target design and becomes a defect there — shared memory, one clock domain, abundant EBR, a hardcoded size. [Sheet P](sec_p) carries the list, and it is reviewed at every merge, not at the transition. |
