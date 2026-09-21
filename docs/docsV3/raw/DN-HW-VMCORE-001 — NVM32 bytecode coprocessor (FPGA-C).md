# DN-HW-VMCORE-001 — NVM32 bytecode coprocessor (FPGA-C)

*noVa64 · Rev A (draft for discussion)* · 2026-09-20 · @Someone

## Revision history

| Rev | Date | Change |
| --- | --- | --- |
| A | 2026-09-20 | Initial draft. Multi-cycle FSM coprocessor executing NVM32 natively on FPGA-C, sharing Helium's MMU and page tables, with the 65816 as host. |

*Review cycle: reviewers, dates and decisions to be recorded here before this note leaves draft status.*

## Status and scope

**Deferred twice over**: FPGA-C is already deferred at project level, and the VM programme is deferred until the kernel runs a user process. This note exists so the ISA can be frozen against a concrete hardware target rather than a hope.

Scope: a core on FPGA-C that executes NVM32 binaries natively — not by emulating a 65816 — while the W65C816S remains the host and runs the native kernel. The core is a bus-mastering coprocessor: it reaches memory through Helium's arbitration and shares the MMU, TLB, page tables and ASIDs, issuing 24-bit virtual addresses in the same per-process space, so one binary's pointers are valid on both executors.

**Relationship to DN-HW-ARGON-001.** Argon is the noVa128 successor: one tri-mode core replacing the 65816 entirely. This note is the noVa64 form, where the 65816 stays and the core is an accelerator beside it. The two share the ISA, the state window layout and the fault model; they differ in that Argon also executes 65816 instructions and therefore does not fit an iCE40. If noVa128 proceeds directly, this note becomes the stepping stone rather than the endpoint — build it on the Phase 1 ECP5 first either way.

**Discrepancy to record.** FPGA-C was originally described as a "softcore 65816". This design makes it bytecode-native instead, so **the native 65816 kernel can never run on FPGA-C unless also compiled to NVM32**. That is acceptable under the coprocessor model but must be an explicit, recorded decision. Standalone mode is the only path to a softcore-only machine and requires a bytecode kernel.

## Microarchitecture

**Multi-cycle FSM with hardwired control** — not a pipeline, not microcode. The design is memory-bound, so a pipeline is a poor trade, and hardwired control for \~60 opcodes is smaller than a microstore.

States: FETCH0 → DECODE (length and fields) → \[FETCH1/FETCH2\] → EXEC (ALU or address calculation) → MEM (may stall or fault) → WB (commit register file and PC). **Commit only in WB**, which is what makes faults precise.

| Block | Choice | Notes |
| --- | --- | --- |
| Decoder | Combinational table on the first byte | Trivial because formats are few and fields are fixed nibbles |
| ALU | 32-bit, LUT4 on the fast carry chain | Add, subtract, logic |
| Shifter | Iterative, 1 bit per cycle | Or a small two-level shifter at \~40 LUT4 |
| Multiply / divide | Iterative shift-add, mandatory | \~32–40 cycles; a 32×32 multiplier is \~307 LUT4, or \~112 with accumulator shifting. No DSP is available in the open flow on iCE40 HX |
| Register file | 16 × 32-bit in EBR | \~2 EBR for one read port, \~4 for two. A 4 Kbit EBR is 256×16, so depth is nearly free — relevant to the register-window candidate in DN-SW-VMISA-001 |

**Resource estimate:** core \~2,500–3,500 LUT4 and 4–6 EBR, inside HX8K's 7,680 logic cells and 32 EBR, leaving \~4,000 LUT4 for a small cache and the Helium bus-master interface. Calibration points: PicoRV32 is 1,982 logic cells at 65.22 MHz on HX8K with CPI ≈ 4; FemtoRV32 is under 1,200 LUTs; SERV is \~198 LUTs but CPI ≈ 32, too slow here. **Fmax target 40–50 MHz**, which matches the project's "more CPU" note. Treat all figures as ±20% until a trial synthesis is run.

Alternative worth keeping open: **share Helium's memory-mapped multiply/divide unit** instead of instantiating one, saving core LUTs and reusing gateware the interpreter already wants.

```verilog
localparam S_FETCH0=0,S_DECODE=1,S_FETCH1=2,S_EXEC=3,S_MEM=4,S_WB=5,S_HALT=6;
always @(posedge clk) begin
  case (state)
    S_FETCH0: begin ir0 <= mem_rdata16; mem_addr <= pc;       state <= S_DECODE; end
    S_DECODE: begin
        opcode <= ir0[7:0]; rd <= ir0[15:12]; rs <= ir0[11:8];
        ilen   <= len_tab[ir0[7:0]];              // 1, 2, or 3 units
        state  <= need_unit1 ? S_FETCH1 : S_EXEC;
    end
    S_EXEC: begin alu_y <= alu_out; state <= is_mem ? S_MEM : S_WB; end
    S_MEM:  begin
        if      (fault)   begin cause <= mcause; state <= S_HALT; end  // no WB -> precise
        else if (mem_ack) state <= S_WB;                               // else stall
    end
    S_WB:   begin regfile[rd] <= wb_val; pc <= next_pc; state <= S_FETCH0; end
    S_HALT: begin /* raise trap to the 65816 kernel; freeze regs and pc */ end
  endcase
end
```

**ZPU-style trap-to-software is reserved, not used.** Feature bits and the illegal-opcode trap allow a future minimal core to omit complex operations, but v1 keeps multiply, divide and block operations mandatory on both implementations so semantics stay bit-identical without a software runtime on the core.

## Memory interface

**Bus mastering through Helium's arbitration**, sharing the MMU, TLB, page tables and ASIDs exactly as the 65816 does. The core issues 24-bit virtual addresses in the same per-process space, so page tables are identical and one binary's pointers are valid on both executors.

**Restartable fault signalling equivalent to ABORTB.** On a fault response from Helium, the MEM state freezes the register file and PC, latches the cause and halts — the architectural analogue of ABORTB's "no register modification, restart the instruction". Helium must block the write on a faulting store, which is the same requirement ABORTB already imposes for the 65816.

**Bandwidth is the binding constraint, not Fmax.** A naive two-bytes-per-fetch FSM at 40 MHz can saturate the shared bus on its own. Mitigations, in order of value:

1. A small local instruction cache, burst-filled a line at a time through Helium.
2. A data cache or store buffer.
3. Burst and prefetch of sequential code units, which the encoding makes easy because instructions are 16-bit aligned and length is known from the first byte.

Because misses stall the core the same way they stall PHI2, effective throughput is set by hit rate rather than clock frequency. Any effort budget should go to the cache before the core.

**Estimated speedup:** at \~5–10 cycles per VM instruction at 40 MHz, roughly 4–8M VM-instructions per second against the interpreter's 0.15–0.4M — **20–100× before contention, realistically 20–40×** once shared-bus contention and misses are included. That is the regime that makes high-resolution graphics modes viable, which is the reason the softcore is on the roadmap at all.

## Host integration

**Start, stop, preempt and resume through a state window and mailbox** in the reserved host region. The kernel writes {R0–R15, PC, ASID, entry} and releases the core. To preempt, it asserts stop; the core finishes or aborts the current instruction restartably and writes {R0–R15, PC} back. A per-core "please stop" doorbell bounds preemption latency.

The window layout is the same PCB the interpreter uses (DN-SW-VMINTERP-001), which is what makes the next property work.

**How the kernel reaches the core.** Through the same VM gate as the interpreter, using its coprocessor implementation (gate C in DN-SW-VMINTERP-001). The BIOS selects it at boot when the CPU probe reports a physical W65C816S and Helium's capability register reports an NVM32 core on FPGA-C, and records it in the system information block. The kernel source does not change between an interpreted, coprocessor or Argon machine; only the vector does.

**Process migration.** Because architectural state is identical on both executors, a process can move mid-execution between interpreter and softcore: run it interpreted until the core is free, then hand over the exact same {R0–R15, PC}. This is the headline benefit of the shared-state design and the main reason the ISA forbids any implementation-specific state.

**Syscall forwarding.** `SYSCALL` halts the core with cause, number and arguments in the window; the 65816 kernel runs the *same* marshalling and `COP` path as the interpreter and resumes the core. Cost is high — halting, waking the host, marshalling, resuming — which is one of the arguments for Argon, where the same trap becomes a mode switch inside one core.

**Interrupts.** Device interrupts remain the 65816's. The softcore is a coprocessor: it is stopped and resumed, never interrupted. Both executors therefore share the rule that the outside world interrupts the host, never the VM.

**Debug.** Breakpoints through `BKPT` plus address-match comparators, single-step as a run-one-instruction FSM mode, and trace export through Helium's existing Debug Agent rather than new infrastructure.

## Verification

**Same conformance suite, same golden model.** The C11 reference interpreter is the oracle for all three executors. Verilator co-simulation runs the core against the reference with per-instruction trace comparison on {R0–R15, PC, memory writes}. Differential testing across the C reference, the 65816 interpreter and the core is the acceptance gate — the pattern already proven on NVFS.

**Cheap formal checks** where they pay for themselves: the decoder's length and field extraction, and the ALU edge cases (shift masking, divide by zero, `INT_MIN / −1`) are small enough for bounded model checking against the reference truth tables.

**The one instruction that needs explicit attention** is `MEMCPY`. The interpreter maps it to `MVN`/`MVP`, which is bank-confined and segmented per 64 KB; the core has a flat 24-bit datapath. Both must produce identical memory-write order and identical restart behaviour. This is the instruction whose implementation differs most between targets and therefore the likeliest place for a silent divergence.

## Standalone mode (sketch)

The reserved opcode block `$F0–$FE` hosts a privileged extension: a supervisor-mode bit, a handful of control registers (status, trap cause, trap PC, page-table base, ASID), trap vectors, and MMU control operations such as TLB flush and page-table-base load.

In standalone mode the core is the only CPU and boots a kernel compiled to NVM32; user and supervisor separation plus those control registers replace the 65816 kernel's role.

**This is explicitly not the primary design.** The encoding space is reserved so it can be added without breaking v1 binaries; v1 defines only user-mode NVM32 plus the coprocessor model above. If a softcore-only machine becomes the goal, this is the path — and it requires a bytecode kernel, which is a decision with consequences well beyond this note.

## Abandonment conditions and open questions

- **Success threshold to justify building the core at all:** a measured **≥20× speedup** over the interpreter on graphics-mode workloads. Below that, the coprocessor does not earn its FPGA.
- If the core plus a minimum viable cache cannot fit HX8K within timing at ≥40 MHz, then either (a) keep the core on the larger ECP5 in the product, (b) move multiply, divide and block operations to the reserved trap-to-software path to shrink it, or (c) narrow the datapath to 16 bits with 32-bit operations as multi-cycle.
- If noVa128 and Argon are adopted directly, this note becomes a stepping stone: build it on the ECP5 for the verification harness and the ISA validation, and do not spend effort on iCE40 fitting.

Open questions:

- [ ] **Instruction-cache coherence.** If the core caches guest `.text` locally and the kernel remaps pages (copy-on-write, paging), the cache must be flushed on the relevant TLB and ASID events. Specify before implementation.
- [ ] Cache organisation and size, which matters more than any core parameter.
- [ ] Does the core instantiate multiply and divide, or share Helium's unit?
- [ ] Arbitration policy between the 65816 and the core: fixed priority, round robin, or quota. Affects both worst-case interpreter latency and core throughput.
- [ ] Does the core participate in the Debug Agent's existing command set, or need new commands?
