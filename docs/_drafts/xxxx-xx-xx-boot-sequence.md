Boot sequence for the nova

Bootloader, loaded into RAM by the FPGA itself.

Bootloader has to be extremely simple:
    Init video. 
    Test Ram.
    POST (Ram, peripherals?)
    Test for SD
    Load from SD into RAM the SO (In includes reset vectors)

Hard Reset . Repeats cycle.
Soft Reset . Jump to vector. 


