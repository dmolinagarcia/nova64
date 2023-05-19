---
categories: hid
---

Before we can see anything coming out from the noVa64, a very basic IO is needed. As an FPGA will be the nervous system of the computer, and as I have a couple Tang Nano 1k boards lying around, and some cheap FTDI Serial2USB adapters, so I can implement a serial console inside the FPGA and begin some testing with the components I have.

In this website : https://learn.lushaylabs.com/tang-nano-9k-debugging/ someone iplementes exactly this with a Tang nano 9k

there, two pins of the FPGA go to pins 29 and 30 in the BL702 which then, moves serial comunitation over its usb port. i need to check if I have this on the 1k.

Pins 29 and 30 are unconnected but exposed over the pins. on the FPGA 9k, the FPGA pins are not specially marked so seems this is doable. I not sure if the BL702 (Being a microcontroller) on the TANG NANO 1K has the UART enabled, as it could have a different program when compared to the 9k

According to that website, the BL702 presents itself as 2 serial devices. one for JTAG, one for UART. check this.

As this https://github.com/sipeed/RV-Debugger-BL702 seems to be what Sipeed puts on the Bl702. So maybe.... I can do this.

Segun la pagina de sipeed, solo el tang 9k tiene el dual. :(

Entonces, ftdi + fpga dentrá que ser.

Explorar la posibilidad de reprogramar el BL702. Comparar el schematic del tang, con el del modulo bl702, con mi schematic

google: sipeed bl702

Una vez la fpga reciba y emita... esto será el puente con dos dispositivos para la cpu. 
- El teclado aparece como un dispositivo mapeado en IO que emite caracteres y puede generar IRQs.
- La consola, que pinta caracteres que se envien a un dispositivo.

La consola es independiente del VIDEO. Habrá un dispositivo al que pintar que será la consola y se puede usar  como debug. El video es otro tema.

La consola escupirá cosas durante el arranque, por ejemplo.

