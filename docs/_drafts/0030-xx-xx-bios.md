---
categories: cpu
---

En este paso, vamos a necesitar un pequeño core en la fpga. Y algo de memoria también (podemos usar parte de la FPGA?)

Probaremos el bootloader, que carge la BIOS a la memoria y ceda la ejecución a la CPU.

Usar  un softcore, o incluso, un 6502.

-- nota--- se me va de las manos! El bootloader pueder ser una simple state machine que mueva algo de la SD a memoria. Incluso, que mueva un codigo interno a memoria. El código para leer de una SD puede ser mínimo. Que sea la CPU la que se encargue de bajar el SO a memoria.

