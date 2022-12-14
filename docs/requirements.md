---
layout: page
title: Requirements
permalink: /requirements
menu: main
---

## Form factor

- Computer should be a laptop, fully portable

## Power

- Single and low voltage. 5v is very obsolete. At least 3.3v, maybe even lower
- Battery powered
- USB-C Charger, allowing for fast charge if possible
- Battery pack reports to the computer. Charge status, capacity, etc
- Computer then display this

## CPU

- Based around a 65816. Real CPU, no FPGA here
- DRAM. Maximum memory at 16MB for the CPU
- Maybe bigger and faster DRAM, via an FPGA acting as MMU, as ASYNC RAM

## VIDEO

- At least 640x400 (Twice a C64). 256 colors
- FPGA is allowed here
- Built in Laptop Screen
- External monitor an option (mirror, or extend if possible)
- VGA or HDMI for external monitor

## AUDIO

- Stereo audio and builting speakers
- External audio an option
- Volumen control trhough software

## Storage

- Dual SD. One acting as internal hd, one acting as eternal storage

## Peripherals

- USB Support for storage, and HID
- Internal keyboard and trackpad are USB
- External ports too

## Connectivity

- Is TCP/IP Possible? Wifi? Ethernet?

## Boot sequence

- Small ROM to handle booting. Then, SO from "INTERNAL" sd is loaded into meemory