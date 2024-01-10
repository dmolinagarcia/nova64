---
categories: power
---

## Exploring power delivery

Aqui hablamos del proceso de descubrimiento

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
https://d3s5r33r268y59.cloudfront.net/datasheets/22652/2021-02-20-23-09-06/18650_Manager_schematic.pdf

charger, gauge, with i2c exposed and source code.

let's start here


https://www.lcsc.com/product-detail/Battery-Management-ICs_Texas-Instruments-BQ24075RGTR_C15464.html
bq24075 - battery charger
   input from usb
   carga bateria
   output es regulado a 5.5
   no tiene i2c

https://www.lcsc.com/product-detail/DC-DC-Converters_XI-AN-Aerosemi-Tech-MT3608_C84817.html
mt3608  - 5v regulator  for lcd

https://www.lcsc.com/product-detail/DC-DC-Converters_XI-AN-Aerosemi-Tech-MT3420B_C88169.html
mt3420c - 3v3 regulator for logic
el del enlace es el B. pero... vale?

https://www.lcsc.com/product-detail/Battery-Management-ICs_Texas-Instruments-BQ27441DRZR-G1A_C473397.html
bq27441 - fuel gauge con i2c
  
Con esto tengo, carga, monitor con salida i2c, 5v y 3v3

los reguladores tienen enable input.... para que?

Puedo tener un FF, activado con el boton (set) y que ademas, el boton sea output hacia el PC. hay un input que es apagado (reset al ff) -- Descartado


inputs del power pack
  usb
  poweroff from computer

outputs del powerpack
  5v regulated
  3v3 regulated
  button press
  i2c del fuel gauge

MCU
  input - button
  output -  5v enable 
            3v3 enable
            button press to computer



al pulsar el boton, se habilitan los reguladores y se enciende el noVa
el off tiene que venir desde el nova
hard power off? long press? como? un RC?
Puedo usar un MCU de estos de 3 cts????







