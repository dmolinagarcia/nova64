# Source-level debugging
> one adapter · the emulator first · the Debug Agent later · the guest never knows

The emulator runs code, but nothing in this document yet lets a developer stop it on a line of source, look at the registers and step. This sheet specifies that tool: a Debug Adapter Protocol server run by Visual Studio Code — the editor [sheet V7](sc_v7) already puts in every project container — in front of the emulator now and in front of the Debug Agent of [sheet R](sec_ai_r) later. The idea, and none of the code, comes from 65816-OS, a hobby 65816 project whose author built exactly this for their own machine and then did most of the development in it; the reading is DN-SW-EMU-004, and what that project got wrong is written here as requirements.

## Where it sits — the adapter is host tooling, and the guest never learns it exists.

| Layer | Runs in | Contents |
|---|---|---|
| Editor | The browser, through either entrypoint of [sheet V7](sc_v7) | Breakpoint gutter · Variables · Call Stack · Debug Console · the display webview |
| Debug adapter | The project container, in VS Code's extension host | DAP server · source map · stepping · symbol lookup |
| Backend | The emulator now · the Debug Agent later | [Sheet R](sec_ai_r)'s command set, plus host-only extensions marked as such |
| Guest | 65816 code | Unaware |

- EM8.1 — **Everything on this sheet is a host-only facility in the sense of [EM3.2](sec_ai_em3#em32).** With a debugger attached the guest sees no register, no event and no memory change it would not see without one; a breakpoint in the emulator is a comparison in the host, never a `BRK` written into guest memory. The one exception is the hardware backend's stub, which perturbs the state it reports and is labelled as doing so ([EM8.10](sec_ai_em8#em810)).
- EM8.2 — **The front end is Visual Studio Code, through the Debug Adapter Protocol, because that is where the code is already being written.** Both entrypoints of [sheet V7](sc_v7) run the editor server inside the project container, so an extension installed there runs beside the toolchain and the emulator on either one, and only its webview panes render in the browser. **One adapter therefore serves vscode.dev and code-server alike**, and nothing has to be built for the browser specifically.
  NOTE: **DAP on this sheet is the Debug Adapter Protocol**, the JSON protocol between an editor and a debugger. It is unrelated to CMSIS-DAP and DAPLink, the probe protocols of [sheet D2](sec_ai_d2) and [D57](sec_ai_q#d57); the two share the letters and nothing else.
- EM8.3 — **The adapter reaches the machine only through the Debug Agent's command set, plus host-only extensions marked as such** ([D112](sec_ai_q#d112)). That is [EM3.3](sec_ai_em3#em33)'s debug parity applied to a client: the adapter is written once and pointed at hardware later without a rewrite. The extensions are what only the emulator can offer — exact registers, any number of breakpoints, the shadow stack, the counters of [EM3.15](sec_ai_em3#em315) — and each is a separate command that the hardware backend answers with "not available" rather than with an imitation.
  NOTE: The prior art put its emulator inside the adapter and called it directly, and its author calls the pair "(too) tightly coupled". The price is concrete: that debugger can never reach the board it was written for.
- EM8.4 — **Which process hosts the emulator is open** (→ [Q184](sec_ai_q#q184)). Either the wasm module of [sheet EM5](sec_ai_em5) is loaded into the extension host — one process, and the module is already a library with a sliceable run loop ([EM5.6](sec_ai_em5#em56)) — or the native build runs as a child process speaking the command set over a pipe. **The second is the recommendation**: the pipe is then the same boundary the hardware backend will have. Speed is not the argument: only the wasm build has been measured, at nearly three times real time under Node ([EM5.13](sec_ai_em5#em513)), which is already enough.

## The source map — from the linker's debug file, and checked against the image.

- EM8.5 — **Assembly is mapped through the file `ld65 --dbgfile` writes**: text, one record per line, tab-separated, with comma-separated `key=value` fields. Five kinds of record are needed. **The address of a line is `seg.start + span.start`, and one line may name several spans.**

| Record | Carries | Used for |
|---|---|---|
| `file` | id · name · size · mtime | Source paths, and the staleness check of [EM8.7](sec_ai_em8#em87) |
| `seg` | id · name · start · size · output name and offset | The base of every span, and the image check |
| `span` | id · segment · start · size | One contiguous run of bytes |
| `line` | file · line · type · spans joined by `+` | The two maps; `type=2` marks a line inside a macro body |
| `sym` | name · value · size · segment · type | Frame names on the call stack, and watches |

- EM8.6 — **A source line maps to every address it produced, and a breakpoint on it binds all of them.** A macro line expanded ten times has ten addresses. The prior art kept only the last one it read, so a breakpoint on a macro line stopped at one expansion in ten; the map here holds a set per line. In the other direction an address maps to one line, and **the line inside the macro body wins over the invocation**, so stepping through a macro shows the macro.
- EM8.7 — **The adapter refuses a `.dbg` that does not describe the image it was given, and warns about sources changed since the build.** Every `seg` record names its output file, its offset in it and its size, which checks the image; every `file` record carries the size and modification time the assembler saw, which checks the sources. A breakpoint landing an instruction away from its line because the file moved after the build is the most confusing failure a source-level debugger has, and the data that prevents it is already in the file. The prior art checked neither.
- EM8.8 — **There is one build, and it always emits debug information** ([D113](sec_ai_q#d113)). `ca65 -g` and `ld65 --dbgfile` leave every byte of the image unchanged, so a separate debug target — which the prior art had — buys nothing except a second binary able to drift from the first. The flags are in [O.10](sec_ai_o#o10).
- EM8.9 — **C is mapped through DWARF, and that path is not yet confirmed** (→ [Q186](sec_ai_q#q186)). Calypsi's objects are ELF with DWARF debug information, which a DWARF reader handles in principle. Whether it survives to the image the loader actually uses, and how the 24-bit addresses and the far pointers of [O.2](sec_ai_o#o2) are encoded in it, has not been checked. **Assembly comes first** because the boot images, the BIOS and the kernel's entry paths are assembly ([O.6](sec_ai_o#o6)).

## Run control and inspection.

- EM8.10 — **Registers are shown at their live width, with their provenance.** A, X and Y follow M and X, the flags are shown one by one with E beside them, and every value says where it came from: **exact** from the emulator, and on hardware **inferred** from trace or **forced** by the debug stub — the three sources of [R.15](sec_ai_r#r15), whose rule that the distinction stays visible binds the Variables view as much as the console.
- EM8.11 — **Memory is read and written along the command set's two axes.** A DAP memory reference carries `v:` or `p:`, as the console's addresses do ([R.5](sec_ai_r#r5)), so a hex view states whether it shows what the current process sees or what the chips hold. A watch is a symbol name, resolved to an address and a size from its `sym` record and formatted by that size.
- EM8.12 — **Device registers get a scope of their own, and reading them is safe only because every register reads without side effects** ([M.15](sec_ai_m#m15), [D92](sec_ai_q#d92)). Opening the Variables view reads every register it lists. A register that popped on read would lose an entry each time the developer looked, which gives [Q181](sec_ai_q#q181)'s pop-on-read FIFO one more reason to go.
- EM8.13 — **Breakpoints in the emulator are an address set compared at every opcode fetch**, which the bus layer recognises as the hardware does, by `VPA` and `VDA` asserted together ([R.14](sec_ai_r#r14)). On hardware there is one trace trigger until [R.24](sec_ai_r#r24)'s comparators exist, and a `BRK` breakpoint needs the stub; the adapter reports how many breakpoints it could bind and refuses the rest, rather than dropping them in silence.
- EM8.14 — **Step over and step out stop at the return, found by the stack pointer rather than by counting opcodes.** At a call — `JSR`, `JSL`, `JSR (abs,X)` — the adapter records the return address and S, and runs until PC reaches that address with S back at its level; step out does the same for the innermost frame. Counting call and return opcodes, as the prior art did, loses its place at the first stack manipulation. **An interrupt, `COP` or `BRK` taken during a step is stepped over like a call** and runs to its `RTI`, so a timer tick does not drop the developer into a handler — what [R.14](sec_ai_r#r14) needs `IRQ_MASK` for on hardware, obtained here without changing what the guest sees.
- EM8.15 — **The call stack is a shadow stack kept by the emulator, one per address space.** A frame is pushed at every call and every interrupt entry, recording the S it was entered with, and discarded when S rises above that level rather than when a return opcode is seen — so a `TCS` onto another stack leaves no stale frames. Keying the shadow stacks by ASID makes a context switch show the stack of the process switched to ([N.2](sec_ai_n#n2)). Frames are named by the nearest preceding `sym`. On hardware there is no shadow stack, and a call stack is at best a heuristic walk of stack memory.

## The display and the keyboard.

- EM8.16 — **The screen is the browser shell of [sheet EM5](sec_ai_em5), hosted in a webview beside the editor.** It already renders frames to a canvas and captures input, so the adapter feeds it frames and receives its input. Key events reach the guest by key code through the mailbox, as [EM3.7](sec_ai_em3#em37)–[EM3.9](sec_ai_em3#em39) and [EM5.11](sec_ai_em5#em511) require, and are recorded for replay like any other input ([EM3.10](sec_ai_em3#em310)).
  NOTE: The prior art did the same for a PS/2 keyboard, by physical key code through an explicit table. Only the table changes: here it maps key code to HID usage on page `0x07`, the encoding the EC sends ([CN1.11](sec_ai_cn1#cn111)).
- EM8.17 — [[open]] **Two things a webview may withhold have not been tried.** The workbench takes some key chords before a webview sees them, as the browser takes `Ctrl+W` — the reason [sheet V7](sc_v7) installs its entrypoints as applications; and pointer lock inside a webview's frame, which [EM5.10](sec_ai_em5#em510) relies on for capture, is untested. The capture toggle's rule that its hotkey is never forwarded ([EM3.14](sec_ai_em3#em314)) holds either way.

## What 65816-OS showed — and what it did not.

- EM8.18 — **The idea is proven by a project of exactly this kind.** 65816-OS — a breadboard W65C816, an OS in ca65 with preemptive multitasking and a shell, an emulator in TypeScript and a VS Code debugger grown from Microsoft's mock-debug sample — became its author's main development platform, with the hardware reprogrammed "once in a while". What this sheet takes from it is the architecture, the `.dbg` reading and the assembler conventions of [O.10](sec_ai_o#o10); its code stays where it is.
- EM8.19 — **Its CPU core is the plainest evidence for [EM7.22](sec_ai_em7#em722).** `SBC` returns the wrong value and the wrong carry, `BRK` jumps to the vector's address instead of through it, native-mode interrupts take the emulation vector, one opcode decodes the wrong addressing mode, and decimal mode is absent — in a core that ran a multitasking OS for years. **A program that runs proved nothing about it.** The first defect sits on that OS's context-switch path, so the emulator and the board it modelled computed different stack pointers at every task switch.

| From 65816-OS | Here |
|---|---|
| A DAP adapter in front of an emulator, the display in a webview | [EM8.2](sec_ai_em8#em82), [EM8.16](sec_ai_em8#em816) — behind the Debug Agent's command set, not wrapped around the emulator |
| The ld65 `.dbg` reader | [EM8.5](sec_ai_em8#em85)–[EM8.7](sec_ai_em8#em87) — every address of a line, and a staleness check |
| Width macros, `.smart`, an entry width on every routine | [O.10](sec_ai_o#o10) |
| Its context switch | [N.2](sec_ai_n#n2) — a lesson, not a design |
| Its CPU core · its hardware | Nothing |

## Series · DBG-00–DBG-05 — the emulator backend first; hardware last, and not before its condition holds.

- [ ] DBG-00 — **Symbols.** The adapter loads a boot image ([EM4.15](sec_ai_em4#em415)) and its `.dbg`, refuses a mismatched pair, and binds a breakpoint on every address of a line.
  TEST: a breakpoint on a macro line expanded n times is hit n times · a `.dbg` from another build is refused.
- [ ] DBG-01 — **Run control.** Launch with stop on entry, continue, pause, step one instruction, stop at a breakpoint — against the emulator.
  TEST: a run that stops at a breakpoint and continues ends with the same cycle count and state hash as a run with none, which is [EM8.1](sec_ai_em8#em81) measured.
- [ ] DBG-02 — **Inspection.** Registers at their width with provenance, the flags, memory through `v:` and `p:`, watches by symbol, device registers.
  TEST: reading every device register leaves the machine's state hash unchanged.
- [ ] DBG-03 — **Stepping and the call stack.** Step over and step out across `JSR`, `JSL`, `JSR (abs,X)`, `COP`, `BRK` and interrupts, and a call stack that survives a context switch.
  TEST: stepping over a call during which the timer fires stops on the next line · after a context switch the call stack is the next process's.
- [ ] DBG-04 — **Screen and keyboard.** The shell of [sheet EM5](sec_ai_em5) in a webview, key codes through the mailbox, recorded.
  TEST: a recorded session replays to the same screen with the adapter attached and without it.
- [ ] DBG-05 — **The same adapter against hardware, through the Debug Agent.** Only once [R.24](sec_ai_r#r24)'s condition holds — the command set has stopped moving — and the host transport of [Q185](sec_ai_q#q185) is decided.
  TEST: DBG-00 to DBG-02 pass against the board, registers shown as inferred, and every emulator-only extension refused rather than imitated.
