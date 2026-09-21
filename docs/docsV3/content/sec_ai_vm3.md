# NVM32 — the bytecode coprocessor
> a bus-mastering FSM beside the 65816 · bandwidth, not Fmax · and what it costs the softcore plan

A core on Argon (FPGA-C) that executes NVM32 binaries **natively — not by emulating a 65816** — while the W65C816S remains the host and runs the native kernel. It reaches memory through Helium's arbitration and shares the MMU, TLB, page tables and ASIDs, issuing 24-bit virtual addresses in the same per-process space, **so one binary's pointers are valid on both executors**. The ISA is [sheet VM1](sec_ai_vm1), the interpreter it races [sheet VM2](sec_ai_vm2), and the noVa128 core that supersedes it [sheet NV1](sec_ai_nv1).

- VM3.1 — **Deferred twice over**: Argon is already deferred at project level ([B.4](sec_ai_b#b4), [E.5](sec_ai_e#e5)) and the VM programme is deferred until the kernel runs a user process ([VM1.2](sec_ai_vm1#vm12)). The sheet exists so that **the ISA can be frozen against a concrete hardware target rather than a hope** — an encoding validated only by an interpreter is an encoding with one customer.
- VM3.2 — **The discrepancy this creates is recorded rather than absorbed: Argon stops being a "softcore 65816" and becomes bytecode-native.** The consequence is not small — **the native 65816 kernel can never run on Argon unless it is also compiled to NVM32.** That is acceptable under the coprocessor model, where the physical CPU stays and keeps the kernel, but it is an explicit decision with a date on it ([D88](sec_ai_q#d88)), not an implementation detail.
  NOTE: It also reframes [Q13](sec_ai_q#q13) and [Q14](sec_ai_q#q14) rather than answering them. [E.10](sec_ai_e#e10)'s verdict — that the ceiling is memory and a 65816 softcore wins little — is **why** this core is not a 65816: a bytecode core does four to six times the work per instruction, so it escapes the ceiling that a faster 65816 cannot. Whether that is worth an FPGA is still the open question, now with a number attached ([VM3.25](sec_ai_vm3#vm325)).
- VM3.3 — **Standalone mode is the only path to a softcore-only machine, and it requires a bytecode kernel** ([VM3.23](sec_ai_vm3#vm323)). Anyone reading this sheet as "the softcore finally arrives" should read that item first.

## Microarchitecture — a multi-cycle FSM, and a commit that happens once.

- VM3.4 — **Multi-cycle FSM with hardwired control: not a pipeline, not microcode.** The design is memory-bound, so a pipeline is a poor trade — it buys throughput the bus cannot deliver — and hardwired control for ~60 opcodes is smaller than a microstore. **The preference is a real constraint, not an aesthetic**: it is the reason fixed-form memory operands carry a risk rating in [VM1.46](sec_ai_vm1#vm146).
- VM3.5 — **Six states, and the commit happens in exactly one of them.** FETCH0 → DECODE (length and fields) → optional FETCH1/FETCH2 → EXEC (ALU or address calculation) → MEM (may stall or fault) → WB (commit register file and PC). **Committing only in WB is what makes faults precise**, and it is where [VM1.34](sec_ai_vm1#vm134)'s commit rule stops being free: the 65816 gets it from `ABORTB`, and this core has to build it.
- VM3.6 — **The blocks, and why each is the size it is:**

| Block | Choice | Notes |
|---|---|---|
| Decoder | Combinational table on the first byte | Trivial, because the formats are few and the fields are fixed nibbles ([VM1.24](sec_ai_vm1#vm124)) |
| ALU | 32-bit, LUT4 on the fast carry chain | Add, subtract, logic |
| Shifter | Iterative, 1 bit per cycle | Or a small two-level shifter at ~40 LUT4 |
| Multiply and divide | Iterative shift-add, mandatory | ~32–40 cycles. A 32×32 multiplier is ~307 LUT4, or ~112 with accumulator shifting. **No DSP is available in the open flow on iCE40 HX** |
| Register file | 16 × 32-bit in EBR | ~2 EBR for one read port, ~4 for two. A 4 Kbit EBR is 256×16, **so depth is nearly free** — which is what makes register windows cheap in hardware and expensive in the ISA ([VM1.48](sec_ai_vm1#vm148)) |

- VM3.7 — **Resource estimate: ~2,500–3,500 LUT4 and 4–6 EBR**, inside HX8K's 7,680 logic cells and 32 EBR, leaving ~4,000 LUT4 for a small cache and the Helium bus-master interface. **Fmax target 40–50 MHz.** Calibration points: PicoRV32 is 1,982 logic cells at 65.22 MHz on HX8K with CPI ≈ 4; FemtoRV32 is under 1,200 LUTs; SERV is ~198 LUTs but CPI ≈ 32, far too slow here.
  NOTE: **Treat every figure as ±20% until a trial synthesis is run** (→ [Q15](sec_ai_q#q15)). The PicoRV32 number is a community benchmark rather than a vendor table, and several small-core figures are measured on slower silicon than HX8K.
  NOTE: **[E.9](sec_ai_e#e9)'s EBR objection does not apply here, and it is worth saying why.** That budget is committed because Helium's TLB, cache tags and trace ring share one device's 128 Kbit. **Argon is a separate part with its own EBR**, so the register file and a cache compete only with each other.
- VM3.8 — **The state machine in outline**, with the fault path visible: no write-back means no architectural change, which is the whole of the precision argument.

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

- VM3.9 — **ZPU-style trap-to-software is reserved and not used.** Feature bits and the illegal-opcode trap ([VM1.41](sec_ai_vm1#vm141)) allow a future minimal core to omit complex operations, but **v1 keeps multiply, divide and block operations mandatory on every implementation**, so semantics stay bit-identical without a software runtime living on the core. The reservation costs nothing; using it would cost the one property the VM is built on.

## Memory interface — where the design is actually constrained.

- VM3.10 — **Bus mastering through Helium's arbitration, sharing the MMU, TLB, page tables and ASIDs exactly as the 65816 does** ([L.7](sec_ai_l#l7), [L.8](sec_ai_l#l8)). The core issues 24-bit virtual addresses in the same per-process space, so **page tables are identical and need no second walker**, and one binary's pointers are valid on both executors.
- VM3.11 — **Restartable fault signalling equivalent to `ABORTB`.** On a fault response from Helium the MEM state freezes the register file and the PC, latches the cause and halts — the architectural analogue of "no register modification, restart the instruction" ([E.16](sec_ai_e#e16)). **Helium must block the write on a faulting store**, which is the same requirement [E.17](sec_ai_e#e17)'s write-gate equation already imposes for the 65816. One gate, two masters.
- VM3.12 — **Bandwidth is the binding constraint, not Fmax, and a naive core can saturate the bus by itself.** At two bytes per fetch at 40 MHz, an FSM with no cache asks for more of the shared bus than the machine has. Mitigations in order of value: **a small local instruction cache, burst-filled a line at a time through Helium**; then a data cache or store buffer; then burst and prefetch of sequential code units, which the encoding makes easy because instructions are 16-bit aligned and length is known from the first byte ([VM1.22](sec_ai_vm1#vm122)).
  NOTE: **Because misses stall the core the same way they stall PHI2, effective throughput is set by hit rate rather than clock frequency. Any effort budget goes to the cache before the core** — the same conclusion [E.10](sec_ai_e#e10) reached about a 65816 softcore, reached again from the other end.
- VM3.13 — **Estimated speedup: 20–100× before contention, realistically 20–40×.** At ~5–10 cycles per VM instruction at 40 MHz, roughly 4–8M VM-instructions per second against the interpreter's 0.15–0.4M ([VM2.14](sec_ai_vm2#vm214)); shared-bus contention and misses take the rest. **That is the regime that makes the high-resolution graphics modes viable, which is the reason the core is on the roadmap at all** ([T1.39](sec_ai_t1#t139)).

## Host integration — the same gate, the same window.

- VM3.14 — **Start, stop, preempt and resume through a state window and a mailbox in the reserved host region.** The kernel writes {R0–R15, PC, ASID, entry} and releases the core; to preempt, it asserts stop, and the core finishes or aborts the current instruction restartably and writes {R0–R15, PC} back. **A per-core "please stop" doorbell bounds preemption latency.** The window layout is the same PCB the interpreter uses ([VM2.8](sec_ai_vm2#vm28)), which is what makes the next two items work.
- VM3.15 — **The kernel reaches the core through the same gate as the interpreter, using implementation C** ([VM2.22](sec_ai_vm2#vm222)). The BIOS selects it at boot when the CPU probe reports a physical W65C816S *and* Helium's capability register reports an NVM32 core on Argon, and records the choice in the system information block ([VM2.25](sec_ai_vm2#vm225)). **The kernel source does not change between an interpreted, a coprocessor and an Argon 2 machine; only the vector does.**
- VM3.16 — **A process can move mid-execution between interpreter and core**, because architectural state is identical on both: run it interpreted until the core is free, then hand over the exact same {R0–R15, PC}. **This is the headline benefit of the shared-state design and the main reason the ISA forbids any implementation-specific state** ([VM1.14](sec_ai_vm1#vm114)).
- VM3.17 — **`SYSCALL` halts the core with cause, number and arguments in the window, and the 65816 kernel runs the *same* marshalling and `COP` path as the interpreter** before resuming it ([VM1.39](sec_ai_vm1#vm139)). **The cost is high** — halting, waking the host, marshalling, resuming — **and it is one of the strongest arguments for Argon 2**, where the same trap becomes a mode switch inside one core at tens of cycles instead of thousands ([NV1.16](sec_ai_nv1#nv116)).
- VM3.18 — **Device interrupts remain the 65816's.** The core is a coprocessor: it is stopped and resumed, never interrupted. Both executors therefore share one rule — **the outside world interrupts the host, never the VM** ([VM2.36](sec_ai_vm2#vm236)).
- VM3.19 — **Debug reuses what exists rather than adding infrastructure**: breakpoints through `BKPT` plus address-match comparators, single-step as a run-one-instruction FSM mode, and trace export through Helium's existing Debug Agent ([R.16](sec_ai_r#r16)).

## Verification — one golden model for three executors.

- VM3.20 — **The C11 reference interpreter is the oracle for all three executors**, and Verilator co-simulation runs the core against it with per-instruction trace comparison on {R0–R15, PC, memory writes}. **Differential testing across the C reference, the 65816 interpreter and the core is the acceptance gate** — the pattern already proven on the file system ([Y2.22](sec_ai_y2#y222)).
- VM3.21 — **Cheap formal checks where they pay for themselves**: the decoder's length and field extraction, and the ALU edge cases — shift masking, divide by zero, `INT_MIN / −1` — are small and self-contained enough for bounded model checking against [VM1.32](sec_ai_vm1#vm132)'s truth tables.
- VM3.22 — **`MEMCPY` is the one instruction that needs explicit attention, and it is the likeliest place for a silent divergence.** The interpreter maps it onto `MVN`/`MVP`, which is bank-confined and segmented per 64 KB; the core has a flat 24-bit datapath. **Both must produce identical memory-write order and identical restart behaviour** ([VM2.13](sec_ai_vm2#vm213), → [Q160](sec_ai_q#q160)).

## Standalone mode — reserved, sketched, and explicitly not the design.

- VM3.23 — **The reserved opcode block `$F0–$FE` hosts a privileged extension**: a supervisor-mode bit, a handful of control registers (status, trap cause, trap PC, page-table base, ASID), trap vectors, and MMU control operations such as TLB flush and page-table-base load. In standalone mode the core is the only CPU and boots a kernel compiled to NVM32, with user and supervisor separation replacing the 65816 kernel's role.
- VM3.24 — **This is explicitly not the primary design.** The encoding space is reserved so that it can be added without breaking v1 binaries; **v1 defines only user-mode NVM32 plus the coprocessor model above.** If a softcore-only machine ever becomes the goal, this is the path — and it requires a bytecode kernel, which is a decision with consequences well beyond this sheet (→ [Q164](sec_ai_q#q164)).

## Abandonment conditions and what is left open.

- VM3.25 — **The success threshold that justifies building the core at all: a measured ≥20× speedup over the interpreter on graphics-mode workloads.** Below that, the coprocessor does not earn its FPGA, and the effort belongs in the cache instead ([E.10](sec_ai_e#e10), [Q13](sec_ai_q#q13)).
- VM3.26 — **If the core plus a minimum viable cache cannot fit HX8K within timing at ≥40 MHz**, then either keep the core on the larger ECP5 in the product, or move multiply, divide and block operations to the reserved trap-to-software path to shrink it ([VM3.9](sec_ai_vm3#vm39)), or narrow the datapath to 16 bits with 32-bit operations as multi-cycle.
- VM3.27 — **If noVa128 and Argon 2 are adopted directly, this sheet becomes a stepping stone rather than an endpoint**: build the core on the Phase 1 ECP5 for the verification harness and the ISA validation, and spend no effort on iCE40 fitting ([P2.11](sec_ai_p2#p211)).
  NOTE: Build it on the Phase 1 ECP5 first either way. The harness and the ISA validation transfer; the fitting work does not.
- VM3.28 — **Five things stay open**, and the first two decide the others: **instruction-cache coherence**, because a core caching guest `.text` while the kernel remaps pages must be flushed on the relevant TLB and ASID events (→ [Q161](sec_ai_q#q161)); **cache organisation and size**, which matters more than any core parameter ([VM3.12](sec_ai_vm3#vm312)); whether the core instantiates multiply and divide or shares Helium's unit ([VM2.41](sec_ai_vm2#vm241)); the arbitration policy between the 65816 and the core — fixed priority, round robin or quota — which affects both worst-case interpreter latency and core throughput; and whether the core joins the Debug Agent's existing command set or needs new commands ([R.16](sec_ai_r#r16)).
