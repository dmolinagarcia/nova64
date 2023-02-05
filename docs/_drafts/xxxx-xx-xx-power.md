---
categories: power
---

Entrada USB-c

>>> Power Delivery

PD . USB power delivery. To negotiate power. Should I use 20V? It's the maximum. Informar del estado de carga?

>>> Charge mannager

Informa del estado de carga, y de carga total. También puede medir la temperatura de pack

>>> BMS. 

Battery management system. Gestiona la carga de las celdas individuales.

https://www.handsontec.com/dataspecs/module/3S-10A-18650-Charger.pdf

SOC!!!

http://www.injoinic.com/product_detail/id/21.html

todo en uno?
http://www.injoinic.com/product.html

IP5328P debe cubrir todo. Aprox 2$ Hay una placa por 4$ que lo incluye. Problema. No es BMS. Es una unica celda. Se pueden poner varias baterias en paralelo, pero no hay BMS.
Con varias en paralelo, tengo que tener fusibles en el lado positivo.

http://www.injoinic.com/wwwroot/uploads/files/20200221/277b7d7f99700b3307be2cda1ca7000f.pdf
Powerbank SOC
I2c y QC. Admite más de 1 celda????


https://github.com/YC-Lammy/IP5328P-powerbank_design




ip5328P
Parece interesante

https://www.eevblog.com/forum/projects/injoinic-ip5328-i2c-register-map/

mapa de registros i2c!

Tengo que hacer una placa con el 5328. Carga de una celda, y lectura desde un arduino mismamente. 
Esta placa irá al puerto USB de carga. Y puede ser la definitiva para el nova




>>> Power sequence

Hard Reset . Repeats cycle.
Soft Reset . Jump to vector. 

Can I use the soft button from the IP????

It seems I can

9. Button control:
Reg_READ2 = 0x77:
•	bit 3 (read; write 1 = reset) flag button double clicked (=1)
•	bit 1 (read; write 1 = reset) flag button pressed (=1)
•	bit 0 (read; write 1 = reset) flag button clicked (=1)

FGA reads 0x77 from time to time via a watchdog. If I keep everything partially powered on...

When OFF
    any push = power on

When ON
    click           = soft reset
    double click    = hard reset
    pressed         = power off