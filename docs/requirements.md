---
layout: page
title: Requirements
permalink: /requirements
menu: main
---

## Principles

- Not here for the money. This is not meant to be a commercial product
- Hence, no restrictions on parts (Used, obsolete, questionable sources)
- Learn from scratch. No off-the-shelf solution
- Inspiration on others is encouraged though
- Give credit to all sources of information
- Everything will be open sourced, under the GNU v3 License
- Log all the steps, right here on this blog
- No deadline. 
- Learn
- Have fun

## Requirements

### Form factor

- Computer should be a laptop, fully portable

### Power

- Single and low voltage. 5v is very obsolete. At least 3.3v, maybe even lower
- Battery powered
- USB-C Charger, allowing for fast charge if possible
- Battery pack reports to the computer. Charge status, capacity, etc
- Computer then display this

### CPU

- Based around a 65816. Real CPU, no FPGA here
- DRAM. Maximum memory at 16MB for the CPU

### Chipset

- Set of sustaining chips
- FPGA acting as MMU, handling DRAM, with potential for bigger RAM
- IO/TIMER. Could use a 6526 in FPGA maybe. TIMERs need to be driven by independent clock
- RTC. Used to keep time. Can act as TIMERS too for interrupts

### Video

- At least 640x400 (Twice a C64). 256 colors
- FPGA is allowed here
- Built in Laptop Screen
- External monitor an option (mirror, or extend if possible)
- VGA or HDMI for external monitor
- Brightness controlled via software
- Still not sure about dedicated video RAM

### Audio

- Stereo audio and built-in speakers
- FPGA is allowed here too
- External audio an option (Through audio jack, or, if possible HDMI)
- Volumen control through software

### Peripherals

- USB Support for storage, and HID
- Internal keyboard and trackpad are USB
- External ports too

### Storage

- Dual SD. One acting as internal hd, one acting as eternal storage

### Connectivity

- Is TCP/IP Possible? Wifi? Ethernet?

### Boot sequence

- Small ROM to handle booting. Then, SO from "INTERNAL" sd is loaded into memory