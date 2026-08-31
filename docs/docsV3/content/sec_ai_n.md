# Process management
> PCB · context switch · scheduler · binary format

The piece that turns the machine into "an Amiga": several isolated processes taking turns without knowing it, with the MMU eliminating relocation at the root.

- N.1 — PCB per process: registers (A · X · Y · S · D · DBR · PBR · P · PC), ASID, pointer to its page table, state, and queues. [[open]]
  NOTE: The save set is not ours to choose: it must cover exactly what the C compiler treats as live state, direct-page pseudo-registers included. That audit has not been done (→ [Q19](sec_q#q19)).
- N.2 — Context switch: save state to the PCB → write CONTEXT/ASID to the MMU → restore the next one. Tens of µs; no TLB flush thanks to ASID.
  NOTE: Order matters and is not optional: write `CTX_SET_PTBASE` before `CTX_SET_ASID`, or an interrupt landing between the two leaves the new ASID live while the walker still reads the old table (→ [M.8](sec_m#m8)). The bank `$00` question that used to block this is settled in [L.11](sec_l#l11); only sizing remains (→ [Q18](sec_q#q18)).
- N.3 — Scheduler: preemptive round-robin with a 100 Hz timer tick; per-device blocked queues; 2–3 priority levels as a later option. `sleep()` is backed by Helium's free-running counter, never by cycle counting (→ [L.15](sec_l#l15)).
- N.4 — One canonical virtual layout, identical for every process: stack and direct page in virtual bank `$00` (a hardware requirement of the 65816), then crt0 and `.text` from `$02:0000`, then `.data`, then `.bss`, then a heap growing upward — `sbrk` merely moves a pointer and the frames arrive lazily by ABORT.
  NOTE: User code starts at `$02`, not `$01`: the bank map of [L.10](sec_l#l10) is architecture and does not move for the loader. Region addresses within `$02–$FD`, stack size and heap start are still to be frozen (→ [Q17](sec_q#q17)).
- N.5 — Custom binary format: header (magic, entry point) + segments with vaddr · filesz · memsz · R/W/X permissions matching the PTE bits. `.bss` occupies no bytes on disk, only a size in the header.
  NOTE: Field layout, magic value and versioning still to be specified (→ [Q20](sec_q#q20)).
- N.6 — Loading (`exec`): create the page table, copy segments from the card into frames and map them with their permissions, zero `.bss`, map the stack, place the arguments where crt0 expects them, and `RTI` into the entry point **in user mode**. From there crt0 finishes initialisation, calls `main()`, and issues `exit()` when it returns. Every process sees the same addresses: the MMU makes relocation unnecessary.
- N.7 — Lifecycle: exec → ready → running ⇄ blocked → zombie → wait. Isolation is demonstrated in E7: one process dies and the other keeps running.
