# Appendix — index of figures
> every figure · the sheet it lives in · what it shows

The thirteen figures are numbered across the whole document rather than per sheet ([A.13](sec_ai_a#a13)), and each one lives in the sheet it illustrates — so this is the only place they can be seen as a set. **Fig. 7 is the exception and is one drawing in three states**: the same three build phases appear on [P1](sec_ai_p1), [P2](sec_ai_p2) and [P3](sec_ai_p3), each time with that sheet's own phase picked out and the other two held back, which is why it takes a letter suffix rather than three numbers. The glossary is [Z3](sec_ai_z3).

| Fig. | Sheet | What it shows |
|---|---|---|
| Fig. 1 | [B — Global architecture](sec_ai_b) | The four inhabitants of the shared bus, with the RP2354B governing configuration and reset from outside it |
| Fig. 2 | [C — Power supply](sec_ai_c) | Power tree REV C: USB-C and CH224K into the BQ25896 power-path, SYS, and the five converters hanging off it |
| Fig. 3 | [D1 — Embedded controller](sec_ai_d1) | The boot state machine `S0`–`S7` — `S0`–`S6` the EC's, from `S6` the 65816's, and nothing outside the EC's own package read before the CPU runs |
| Fig. 4 | [F — Physical memory](sec_ai_f) | Memory structure: the SRAM on the CPU's own nets, Helium driving the translated half and owning the SDRAM alone |
| Fig. 5 | [K — Virtual memory concepts](sec_ai_k) | Field-width asymmetry — the virtual split falls out of the CPU's 24 bits, the physical one is the MMU's choice |
| Fig. 6 | [L — Virtual memory management](sec_ai_l) | Address translation: TLB with ASID, the hardware walker over the flat table in SRAM, and the ABORTB fault path |
| Fig. 7a | [P1 — Phase 0 · the training rig](sec_ai_p1) | The three build phases — training rig above, prototype carrier in the middle, target board below with both milestones — with Phase 0 highlighted |
| Fig. 7b | [P2 — Phase 1 · the prototype carrier](sec_ai_p2) | The same three phases, with Phase 1 highlighted |
| Fig. 7c | [P3 — Phase 2 · the target board](sec_ai_p3) | The same three phases, with Phase 2 highlighted |
| Fig. 8 | [R — Debug agent](sec_ai_r) | The debug agent as a requester inside Helium: the EC commands over SPI and never drives the bus |
| Fig. 9 | [S — Power control](sec_ai_s) | The power control path — two level pins, the SPI link, and telemetry latched into atomic snapshots |
| Fig. 10 | [T1 — Neon](sec_ai_t1) | Neon: the text path that depends on nothing but the bitstream, and the graphics path fed by commands rather than pixels |
| Fig. 11 | [T3 — Blitter and compositor](sec_ai_t3) | The blitter datapath and the bank partition that is worth a factor of two in delivered bandwidth |
| Fig. 12 | [V — The windowing OS](sec_ai_v) | The windowing stack, and the privilege boundary falling on command emission rather than on the framebuffer |
| Fig. 13 | [D2 — Debug and programming port](sec_ai_d2) | The RP2040 probe on its own supply domain, the six nets that cross to the EC, and the ground that does not |
