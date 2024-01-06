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

FPGA reads 0x77 from time to time via a watchdog. If I keep everything partially powered on...

When OFF
    any push = power on

When ON
    click           = soft reset
    double click    = hard reset
    pressed         = power off

Power stages has    
    IP5108 board. with battery ,button, charger, regulators for pcb
    Input
        i2c from fpga
        reset to 

During power off, only power stage and norbridge need power and RTC!
https://www.mouser.es/ProductDetail/onsemi/NCV8114ASN180T1G?qs=5aG0NVq1C4yUww71t88ZYw%3D%3D&mgh=1&vip=1&gclid=CjwKCAiA_vKeBhAdEiwAFb_nrelQdB7bG4c2qsudjc-aMqG-thg4oxCiP913Bn5oqT-sX2LtngNQ0RoCu6cQAvD_BwE
    

Exploring power delivery.

Primera fase del schematic

Circuito con 5108. boton, i2c expuesto. pads para bateria

Vout alimenta dos reguladores. Uno para el Helium, otro para el resto. Helium estará siempre activo por tanto.
En un mundo ideal, tendría algo ultralowpower alimentado siempre, pero no quiero meter más capas.
Lo más sencillo.... un arduino? Puedo ponerlo en un deepsleep, aunque quizas puedo hacerlo con la fpga?

El arudino habilitaria los LDO

https://oshwlab.com/leonidy85/charger-ip5106

```
int8_t getBatteryLevel()
{
  Wire.beginTransmission(0x75);
  Wire.write(0x78);
  if (Wire.endTransmission(false) == 0
   && Wire.requestFrom(0x75, 1)) {
    switch (Wire.read() & 0xF0) {
    case 0xE0: return 25;
    case 0xC0: return 50;
    case 0x80: return 75;
    case 0x00: return 100;
    default: return 0;
    }
  }
  return -1;
}

#include <M5Stack.h>

int8_t getBatteryLevel()
{
  Wire.beginTransmission(0x75);
  Wire.write(0x78);
  if (Wire.endTransmission(false) == 0
   && Wire.requestFrom(0x75, 1)) {
    switch (Wire.read() & 0xF0) {
    case 0xE0: return 25;
    case 0xC0: return 50;
    case 0x80: return 75;
    case 0x00: return 100;
    default: return 0;
    }
  }
  return -1;
}

void setup() {
    M5.begin();
    Wire.begin();
  }

void loop() {
  delay(1000);
  M5.Lcd.clear();
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.setCursor(0, 20);
  M5.Lcd.setTextSize(2);
  M5.Lcd.print("Battery Level: ");
  M5.Lcd.print(getBatteryLevel());
  M5.Lcd.print("%");
}
```
    



https://www.tindie.com/products/manuat/18650-lipo-battery-manager/

charger, gauge, with i2c exposed and source code.

let's start here


