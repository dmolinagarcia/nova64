
- Lecturas
  - High speed digital design
    - impedance matching on pcbs
    - high speed pcb design - https://www.youtube.com/watch?v=VRJI0X-6yTg
  - The art of electronics
  
- CPu 65816 - basics
  - Prototype board
    - Draft post and publish. 
    - It needs to have, cpu, sram, mmu, main dram, video, video dram, plus charging.
    - spi AND i2c exposed in FPGA
    - USB controller?
    - Draft in Kicad
  - FPGA
      - Gowin Osciloscope
      - Boot sequence. Capture BA, A, D
      - NOP TEST with FPGA only (Feed NOP in DATABUS always)
      - Start from FPGA/ROM and SRAM
      - Simple Serial - https://nandland.com/uart-serial-port-module/
      - Clock strecthing
      - SPI - all peripherals can be SPI?
          - Screen controller (brigthness)
          - Battery management pack
          - USB - HID          - CH376
          - USB - Storage      - CH376
          - RTC
          - Boot ROM
          - Network  
      - Fpga + DRAM (PSRAM?)
          - Sin SRAM/CACHE
      - FPGA - cache algorithm
          - Enable CACHE
      - Video generation
          - Test with no CPU
          - Text mode with no CPU
          - Graphics with no CPU
          - Add CPU
          - Text mode with CPU
          - Graphics
          - Isolate video RAM. Implement DMA
      - FPGA - Audio Generation

- Compilador!
  - https://github.com/andrew-jacobs/w65c816sxb-cdemo/blob/master/w65c22.h
  - https://www.westerndesigncenter.com/wdc/tools.php
  - Relocatable code? Mirar las instrucciones PEr, BRL, BRA. Esto ya existe.



