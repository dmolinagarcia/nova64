---
categories: power
---

## noVa64 Power Pack

Aqui hablamos de nuestro power pack.

https://www.tindie.com/products/manuat/18650-lipo-battery-manager/
https://d3s5r33r268y59.cloudfront.net/datasheets/22652/2021-02-20-23-09-06/18650_Manager_schematic.pdf

charger, gauge, with i2c exposed and source code.

let's start here

Aunque la capacidad de carga del 24075 es bajita.... 7.5wats! no me alimenta una pantalla de 14 pulgadas casi seguro




https://www.lcsc.com/product-detail/Battery-Management-ICs_Texas-Instruments-BQ24075RGTR_C15464.html
bq24075 - battery charger
   input from usb
   carga bateria
   output es regulado a 5.5
   no tiene i2c

   puedo cambiarlo por un injoinic? Algo similar de BQ con QC?
   BQ25895????? 5v @ 3A = 15w. deberia valer. Si no, ya nos vamos a multiple cells y nos complica todo
   este ultimo parece algo más complejo. Vamos con el bq24075 que tenemos un diseño probado.
   O no.... 25895! Si no hay comandos i2c, se pone en un modo por defecto. Cuando el nova esté encendido, el driver de la bateria puede enviar comandos.
   

   podemos hacer una placa con
    smart button
    circuito de carga ip5328
    circuito de carga bq24075
    circuito de carga bq25895

    todos a la vez no, pero podremos probar todos.

    Sera una PCB interesante

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

Contemplemos el consumo? Puede que necesite algo mejor, ya que el bq de carga solo me da 7.5w!


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
Puedo usar un MCU de estos de 3 cts???? NO, no quiero MCU en el powerpack.

Adjuntar schematics







