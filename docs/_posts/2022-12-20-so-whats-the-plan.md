---
categories: general
redirect_from: /general/2022/12/20/so-whats-the-plan.html
---

## So.. what's the plan?

Awesome, another very legit question! So far, there's none, I'm afraid. I'm only gathering some random thoughts for now. The **feature creep madness** phase seems to be wearing off, as I've barely added anything new to the requirements for a few days.

I'm very much aware of the need for a plan. A complex project like this is bound to fail without one. So, let's draft one!

The first step is to call the **feature creep madness** complete. That's something I'm ready to do right now. Sure, some small new features will pop up at some point, but I'm very much satisfied with my current list.

Except for one **small** thing. As DRAM may not be fast enough to respond to the CPU within a single cycle, I will implement some sort of cache. A smaller SRAM IC, handled again by the MMU, will accomplish this. 

There's a whole new set of skills I need to learn. 

- 65816
- Verilog / VHDL
- DRAM
- Memory Cache
- Video generation
- Screen Controller
- Audio generation
- SD card / Filesystem management
- USB (Storage and HID)
- RTC
- Battery Management

Not bad! In parallel, as I progress with the above list, work on a first prototype will proceed as follows:

- Core block diagram. Extend [these random thoughts](/nova64/architecture/2022/12/16/some-random-thoughts.html) into a proper block diagram for the system core
- Prototype schematic. Will have to include CPU, FPGA, SRAM and DRAM footprints. Video will also have a place. SPI bus from the MMU should be exposed, to use it in the peripherals phase
- Preliminary boot sequence (No SD support yet)
- First run. FPGA will forward all writes to SRAM. DRAM doesn't even need to be present
- Test memory accesses, clock stretching and bus disconnect 
- Add DRAM. No cache, all writes go to DRAM
- Implement cache
- Add video by mirroring upper memory bank
- Implement video DMA

Then, time to work on the peripherals. It's very likely all of them can use a SPI bus, of which the MMU should be the master. 

- Simple serial
- Keyboard support
- Mouse support
- SD and filesystem
- USB Storage
- RTC
- Network
- Battery

OS development will be done in parallel with all the above. As I move on, more and more features will be made available. However, by the time the hardware is complete, I will need to fully focus on the software. This is still too far into the future, so I won't think too much about it. I will probably start with a simple, text-only console, running some kind of BASIC. A fully graphical OS is right now beyond my knowledge. We'll talk about it in a few years.

It seems I now have a plan.



