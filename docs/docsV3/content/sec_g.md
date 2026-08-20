# Persistent storage — microSD
> where it connects · boot · normal use

A single slot, two owners at different times. It's also the only boot path: the dedicated BIOS flash was removed from the design.

- G.1 — Connection: the slot hangs off an **SN74CBTLV3257** mux between the RP2040 and Helium's SPI-SD controller, with a handoff handshake.
  NOTE: **Why the card is switched rather than simply left with the EC**, which is the cheaper arrangement and was examined as a way of recovering pins on Helium. Swap is what forbids it: the page evictor writes a frame out and the fault path reads one back, so the card needs a direct route into Helium, and giving it to the EC instead puts a microcontroller and its link in the middle of the page-fault path. The mux, the handshake and Helium's own SPI-SD controller are what that costs, and they were accepted on those grounds rather than on pin count (→ [L.16](sec_l#l16)).
- G.2 — At boot: the RP2040 owns the SD — it reads `BIOS.BIN` from FAT and preloads it into the pinned SRAM zone (steps 5–6 of Fig. 3). It then switches the mux to Helium before releasing reset.
- G.3 — In normal use: the 65816 accesses it through Helium's SPI-SD via the kernel's block driver. The BIOS mounts FAT read-only to load `KERNEL.BIN`; the OS adds write support and the user filesystem.
- G.4 — Card contents: `/BIOS.BIN` · `/KERNEL.BIN` · user tree (programs, data) · future reserve for swap.
- G.5 — Recovery: the RP2040 can reclaim the SD over USB at any time — update BIOS/kernel without removing the card.
