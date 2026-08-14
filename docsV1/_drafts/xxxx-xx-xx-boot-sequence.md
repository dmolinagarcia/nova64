Boot sequence for the nova

Bootloader is BIOS? Maybe it's the same

Bootloader, loaded into RAM by the FPGA itself.

Hard Reset . Repeats cycle. Clears memory?
Soft Reset . Jump to vector. BIOS needs to remain in memory

Al arrancar el equipo, FPGA carga pequeña ROM en RAM. Esto es la BIOS.
    
    Cuando la fpga recibe corriente... copia con una state machine la BIOS a RAM y libera la CPU

    La Bios es

    *=$F000
        initUart
        initVideo
        TestRAM
        Test for SD
        Load SO from SD into RAM
        Jump to SO Start
        basic functions to be used by SO
            only SO can call bios!
            print char and so on?

    *=$RESET
        :F000

    BIOS se queda en memoria, para el softreset

Y a partir de ahi, el SO se encarga de todo

