# Programming and toolchain
> assembly first · C where it's already possible

Boot is assembly-first, but the compiler question is settled: C arrives as soon as the kernel has firm footing. The useful framing is that **the compiler is a solved problem and the runtime around it is not** — what this project has to build is the board support, not a code generator.

- O.1 — Assemblers: **ca65** (cc65 suite) and **64tass** — the base for the BIOS, monitor, and kernel boot.
- O.2 — C compiler: **Calypsi C**, WDC65816 target — selected, not merely shortlisted. C99, fully re-entrant code model, integers to 64 bits, IEEE-754 floats, large code and data models with far pointers and JSL/RTL cross-bank calls; actively maintained and in real use by the Foenix community, on new-build 65816 machines comparable to this one.
  NOTE: **The pointer width was recorded wrongly and the correction changes the ABI, not just a sentence.** This document said "24-bit pointers", which is what the machine's address space is; **Calypsi's default far pointer is 32 bits — four bytes.** The qualifiers are `__tiny` for 8-bit direct-page pointers, `__near` for 16-bit within the current data bank, `__far24` for the 24-bit pointer this document assumed by default, and `__far` for the 32-bit one that actually is the default. **That decides struct layout, the calling convention and every kernel and driver bank-crossing convention**, so the qualifier is chosen per object and written down rather than inherited — and [E0.8](sec_ai_p3#e08)'s audit is where it is checked against compiled output rather than against the manual (→ [Q114](sec_ai_q#q114)).
  NOTE: Supersedes the earlier WDCTools / LCC-816 shortlist; WDC's own C survives as a dated fallback. Ruled out: cc65 and vbcc (6502 only, no 65816 codegen) and llvm-mos — it accepts `-mcpu=mosw65816`, but native 16-bit codegen is immature and the target was still at the RFC stage. Worth re-checking later.
  NOTE: **vbcc reappears elsewhere in this document and it is not a contradiction.** [D96](sec_ai_q#d96) selects it as the **NVM32** cross compiler ([sheet VM4](sec_ai_vm4)) — a target designed after it, with 32-bit registers and a flagless compare-and-branch it models natively — whereas here it is ruled out for the 65816, where it has no 16-bit code generation at all. **Calypsi stays the kernel compiler, the two toolchains never share objects, and their ABIs meet only at the syscall marshalling layer** ([VM1.6](sec_ai_vm1#vm16)).
- O.3 — Memory model: **large code** (24-bit, JSL/RTL) + **small data** pinned to one bank with a fixed DBR, reaching for far pointers only where genuinely needed — dereferencing one is expensive on the 65816, whether through long addressing or DBR reloads, and **`__far24` should be preferred to the 32-bit default wherever a far pointer is unavoidable**: the fourth byte is dead weight in a 24-bit address space, and it costs a load, a store and four bytes of every structure that holds one.
- O.4 — The BSP — five things nobody else will write for us. **crt0**: set up stack, direct page and DBR, initialise `.data` and `.bss`, call `main()`, and turn its return into `exit()`. **A user linker script**, one canonical copy for every user binary. **A kernel linker script**, separate, linking against the privileged space rather than the user 16 MB map. **The syscall stubs**. And **an audit of the compiler's calling convention**, including how much direct page it claims as pseudo-registers — that audit defines the context-switch save set (→ [N.1](sec_ai_n#n1)).
- O.5 — A consequence of full paging that lands squarely on the toolchain: because every process sees an identical virtual layout, **all user binaries link at fixed virtual addresses**. Zero relocation, no load-time fixups, no relocation records in the format — one linker script serves every program (→ [N.4](sec_ai_n#n4)).
  NOTE: **The NVM32 toolchain reaches the same conclusion from the same premise, and it is worth noting that it was reached twice independently** ([VM1.42](sec_ai_vm1#vm142)). A first draft of the VM tried position-independent code and found it unworkable — a 16-bit PC-relative displacement cannot reach the data region from the text region — before arriving at what this item already says. **Position independence belongs only to a shared-library mechanism, and neither toolchain has one.**
- O.6 — Kernel: C with assembly only in vectors, context switch, critical MMU/cache routines, and boot (~hundreds of lines of asm total).
  NOTE: **And no self-modifying code anywhere in project code** ([D92](sec_ai_q#d92)). It is free to promise today and expensive to withdraw: a core with an instruction cache or prefetch diverges from a real 65816 the moment code already fetched is overwritten, and **forbidding the practice is the cheap side of that hazard** — the alternative is obliging Argon 2 to snoop writes into its instruction cache ([NV1.25](sec_ai_nv1#nv125)). The rule reaches the VM gate too, which is a pointer in data for exactly this reason ([VM2.17](sec_ai_vm2#vm217)).
- O.7 — libc: about twenty three-line syscall stubs — `COP #SYS_n` + `RTL` — with Calypsi's low-level libc hooks (`open`, `read`, `write`, `sbrk` …) pointed at them. Each stub loads the service number into A, then `COP`; the compiler emits an ordinary `JSL` and the `COP` never leaves the stub.
  NOTE: Which makes the stable ABI literally "the compiler's calling convention plus a number in A": arguments sit wherever Calypsi put them. Numbering the `SYS_*` constants is a prerequisite for writing any of it.
- O.8 — Development flow: compile on the PC (Makefile) → custom binary → transfer over the EC's console UART or SD → run; debug via serial console + Helium debug port.
  NOTE: **And at source level in the emulator first**: [sheet EM8](sec_ai_em8)'s adapter puts breakpoints, stepping and registers in the editor, and reaches the board later through the same Debug Agent commands the console uses ([D112](sec_ai_q#d112)).
- O.9 — Gateware: Verilog-2005 ([D101](sec_ai_q#d101)) with Yosys + nextpnr-ice40 + IceStorm, linted by Verilator; simulation always before the board.
- O.10 — **ca65 conventions, fixed before the boot images grow** ([D113](sec_ai_q#d113)–[D115](sec_ai_q#d115)). Three rules, each paid for with somebody's afternoon — most of them recorded in DN-SW-EMU-004, where a hobby 65816 OS arrived at the first two on its own.
  1. **Register width is stated three ways.** `.smart +` makes ca65 follow `REP` and `SEP` through straight-line code and set the operand widths itself; the macros below emit the instruction and the directive together, so the two cannot disagree where smart mode cannot see; and **every routine opens with the width directives it expects on entry**, which is its contract with its callers — smart mode follows neither a branch nor a `JSR` from a caller that left the registers otherwise ([EM4.16](sec_ai_em4#em416)).
  2. **One build, with debug information.** Every object is assembled with `-g` and a listing, and every image linked with a map, a label file and `--dbgfile`, since none of them changes a byte of the image. The `.dbg` is what [sheet EM8](sec_ai_em8) reads, and the label file what the monitor loads.
  3. **No vector is ever zero.** All five native vectors of [E.12](sec_ai_e#e12) and the emulation set point at handlers, and a handler with nothing to do prints its own name and halts, as [CN1.8](sec_ai_cn1#cn18)'s panic does — never a `$0000` that sends a stray `COP` or `NMI` into whatever bank `$00` holds at that address.
  ```asm
  .macro  longa           ; 16-bit accumulator and memory
          rep #$20
          .a16
  .endmacro
  .macro  shorta          ; 8-bit accumulator and memory
          sep #$20
          .a8
  .endmacro
  .macro  longi           ; 16-bit index registers
          rep #$10
          .i16
  .endmacro
  .macro  shorti          ; 8-bit index registers
          sep #$10
          .i8
  .endmacro
  .macro  longr           ; both 16-bit
          rep #$30
          .a16
          .i16
  .endmacro
  .macro  shortr          ; both 8-bit
          sep #$30
          .a8
          .i8
  .endmacro
  ```
  ```make
  %.o: %.s
  	ca65 --cpu 65816 -g -l $(@:.o=.lst) -o $@ $<

  boot.bin: $(OBJS) boot.cfg
  	ld65 -C boot.cfg -m boot.map -Ln boot.lbl --dbgfile boot.dbg -o $@ $(OBJS)
  ```
  NOTE: The macro names are the ones 65816-OS uses. Its build is the counter-example for the second rule — a separate `build-debug` target — and its vector table for the third, with `COP`, `BRK`, `ABORT` and `NMI` all left at `$0000`.
