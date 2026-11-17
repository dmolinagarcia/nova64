# noVa64 console-mode operating system — see README.md
B      = build
CA65   = ca65 -I include -I . -I kernel
KSRC   = $(wildcard kernel/*.s shell/*.s common/*.s include/*.inc)

all: $(B)/nova64 $(B)/bios.bin $(B)/kernel.nx $(B)/card.img

$(B)/nova64: emu/cpu.c emu/machine.c emu/nova.h
	@mkdir -p $(B)
	$(CC) -O2 -Wall -o $@ emu/cpu.c emu/machine.c

$(B)/bios.bin: bios/bios.s bios/bios.cfg common/sdcard.s common/mbr.s $(wildcard include/*.inc)
	@mkdir -p $(B)
	$(CA65) -o $(B)/bios.o bios/bios.s
	ld65 -C bios/bios.cfg -o $@ $(B)/bios.o -Ln $(B)/bios.lbl

$(B)/kernel.bin: $(KSRC) kernel/kernel.cfg
	@mkdir -p $(B)
	$(CA65) -o $(B)/kernel.o kernel/kernel.s
	ld65 -C kernel/kernel.cfg -o $@ $(B)/kernel.o -Ln $(B)/kernel.lbl -m $(B)/kernel.map

# two segments: the kernel's bank, and its far bank zero-filled
$(B)/kernel.nx: $(B)/kernel.bin tools/nv.py
	python3 tools/nv.py image -o $@ --entry 0xFD0000 \
	    --seg 0xFD0000:$(B)/kernel.bin:0xFFFF --seg 0xFC0000::0xFFFF

$(B)/card.img: $(B)/kernel.nx tools/nv.py $(shell find rootfs)
	python3 tools/nv.py card -o $@ --kernel $(B)/kernel.nx --root rootfs --size-mb 16

run: all
	$(B)/nova64 --bios $(B)/bios.bin --card $(B)/card.img

check: all
	python3 tests/run.py

clean:
	rm -rf $(B)

.PHONY: all run check clean
