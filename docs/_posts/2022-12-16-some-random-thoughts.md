---
categories: architecture
---

## Some random thoughts on the noVa architecture

I've stumbled upon one little FPGA, with embedded DRAM. The GOWIN GW1NR-LV4 has 64Mbits (8MB) of DRAM, and offers an integrated SDRAM controler. The price is around 16$. I can also embed the video processor in one of these FPGAS.

8MB is not 16MB, so this won't allow me to meet my memory requirements. One possible way out of this is to have one FPGA acting as a memory bridge, with an external SDRAM controller for the 16MB main memory.

Another FPGA, with it's embedded 8MB would be the video processor. 

The MMU should provide a way to halt the CPU. This will be needed both when DRAM access cannot be fulfilled in a single CPU cycle, or, whenever the Video requests for a block transfer.

This could be the process:
- Whenever the CPU requests a read or write from memory, the MMU, which sits on the FULL BUS catches this and forwards the request to Main RAM
- If requests cannot be completed on a single Cycle, CPU is halted until the MMU fulfills the request
- At any point, the video chip can request for a block transfer from main memory to video memory. This requests goes to the MMU, which, whenever possible, halts the CPU and procceds to the block copy
- This block transfer could be accomplished by busrt transfer. Both MMU and VIDEO sit on the data and address bus. 
- MMU can provide enable signal for other devices. (IO, AUDIO, USB). 

Address Bus is 16 bit, and data is 8bit. This is the 65816 bus. MMU demultiplexes the Bank Address (Upper 8 bits of the bus). From this, it generates the /CS signals for everything else. The BA lines can be present in the MMU and shared to the VIDEO to aid in video Transfers.

Depending on the speed of the DRAM, it may not be possible to execute a read or write within a single CPU cycle. If this is the case, I need to provide some short of cache. Maybe a smaller SRAM outside of the MMU, or even inside. The GOWIN also has some BSRAM or PSRAM. Maybe I can turn this into a cache, fetching pages from DRAM?

```

   CPU                 MMU             MAIN DRAM
+-------+        +-------------+      +-----------+
|       |ADR+DATA|             |      |           |
|       +---+----+             +------+           |
|       |   |    |             |      |           |
|  PHI2 <---+----+             |      |           |
|       |   |    |             |      |           |
+-------+   |    +-^-----------+      +-----------+
            |      |
            |      |  VIDEO            VIDEO DRAM
            |    +-+----------+       +-----------+
            |    |HaltRq      |       |           |
            |    |            |       |           |
            |    |            +-------+           |
            +----+            |       |           |
                 |            |       |           |
                 +------------+       +-----------+
                 
```





