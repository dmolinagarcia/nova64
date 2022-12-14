# noVa64

![noVa64 logo](https://raw.githubusercontent.com/dmolinagarcia/nova64/main/docs/img/logo_nova64_big.png)

** What it is noVa64? **
The noVa64 (With an uppercase V) is a new computer built around the 65816 CPU. Inspired by the age of 8-bit computers, it's envisioned as a alternate-universe succesor to the Commodore 8-bit series of computers. Instead of adquiring the Amiga, could've Commodore created a 16-bit machine around this CPU? The noVa64 ss designed and built by a hobbyist (me) who will need to learn pretty much everything on the go.

** What it isn't noVa64? **
It's not a professional or commercially viable product.  It's not meant to be mass produced. It's not a recreation of any pre-existing computer, although it will be inspired by many.

** When will it be ready? **
Hopefully, at some point. That's all I can say.


## Principles

- Not here for the money. This is not meant to be a commercial product
- Hence, no restrictions on parts (Used, obsolete, questionable sources)
- Learn from scratch. No off-the-shelf solution
- Inspiration on others is encouraged though
- Give credit to all sources of information
- Everything will be open sourced, under the GNU v3 License
- Log all the steps
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
- Maybe bigger and faster DRAM, via an FPGA acting as MMU, as ASYNC RAM

### Video

- At least 640x400 (Twice a C64). 256 colors
- FPGA is allowed here
- Built in Laptop Screen
- External monitor an option (mirror, or extend if possible)
- VGA or HDMI for external monitor
- Brightness controlled via software

### Audio

- Stereo audio and built-in speakers
- External audio an option (Through audio jack, or, if possible HDMI)
- Volumen control through software

### Storage

- Dual SD. One acting as internal hd, one acting as eternal storage

### Peripherals

- USB Support for storage, and HID
- Internal keyboard and trackpad are USB
- External ports too

### Connectivity

- Is TCP/IP Possible? Wifi? Ethernet?

### Boot sequence

- Small ROM to handle booting. Then, SO from "INTERNAL" sd is loaded into meemory