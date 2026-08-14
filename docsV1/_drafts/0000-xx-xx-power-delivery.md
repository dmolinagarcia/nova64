---
categories: power
---

## Exploring power delivery

Disclaimer! No garantizo nada de lo que hay aqui

Aqui hablamos del proceso de descubrimiento. Nuestros requisitos. 

Componentes :

Power input - Entrada USB-c

>>> Power Delivery

PD . USB power delivery. To negotiate power. Should I use 20V? It's the maximum. Informar del estado de carga?

>>> Charge mannager

Informa del estado de carga, y de carga total. También puede medir la temperatura de pack

>>> BMS. 

Battery management system. Gestiona la carga de las celdas individuales.

>>> Power Path management

Aun desconocemos el consumo que tendra el noVa. Aunque sin duda lo que mas trage será el LCD.

14 pulgadas - 15w
10 pulgadas - 7.5w

---- Si usamos una 18650, su capacidad typica es de 1800 mAH. 
---- https://es.aliexpress.com/item/1005005171989409.html?spm=a2g0o.productlist.main.3.5e8a23da9Odfr3&algo_pvid=787df26f-b50d-4ca1-84c9-348d1f057772&aem_p4p_detail=202401100411271381385254820400004335225&algo_exp_id=787df26f-b50d-4ca1-84c9-348d1f057772-1&pdp_npi=4%40dis%21EUR%2123.05%2117.28%21%21%2124.62%21%21%402103890917048886870206690eae42%2112000031965335893%21sea%21ES%210%21AB&curPageLogUid=4I8EQjHfdERy&utparam-url=scene%3Asearch%7Cquery_from%3A&search_p4p_id=202401100411271381385254820400004335225_2 6000mAH, mejor! pero la calidad?

Existen una seria de controladores chinos destinados a la construcción de PowerBanks. Por lo general, integran PD y CM y PP. Usan una unica celda (Pudiendo tener varias baterias en parallelo) por lo que no es necesrio un BMS. La mayoria de los powerbanks están construidos asi.

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

Cons. El reporte de carga es muy limitado, en 4 tramos de 25%

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
    

mas cons. el mapa de registros....

Aqui tenemos un cargador completo.


https://www.tindie.com/products/manuat/18650-lipo-battery-manager/
https://d3s5r33r268y59.cloudfront.net/datasheets/22652/2021-02-20-23-09-06/18650_Manager_schematic.pdf

charger, gauge, with i2c exposed and source code.


Hay alternativas para medir la carga (Fuel gauge) que se conectan directamente a la bateria.

O alterniativas de carga como el bq24075

otro interesante, similar al bq24075, es el BQ25895. Mas actual, con mas potencia, negocia con USB.



