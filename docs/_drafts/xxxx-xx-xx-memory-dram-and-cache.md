---
categories: memory
---

## Memory architecture. Cache and DRAM

DRAM, SRAM

A23-A10 es el identificador de página. Esto se almacena en una tabl de cache pagees
A9-A0   es el lcoalizador dentro de la pagina
        La cache funciona subiendo y bajando páginas completas

Puedo ajustar el tamaño de las páginas, idealmente, adaptandome al burst de las dRAM
  En este caso, aumenta o disminuye el tamaño del localizador (y el identificador de página en concordancia)        

Si tengo una SRAM mayor, puedo tener más paginas en cache. Aumenta el tamaño de la tabla

-- Es posible detectar la SRAM que tengo? para adaptar al vuelo? Estaria bien
-- Es posible detectar la DRAM que tengo? para adaptar todo al vuelo?

PSRAM? low pin count, and maybe fast enough to act as SRAM?

If not... cache??

Table. 64 entries, A23-A10 plus a modtifier flag

On System start

- cacheCounter := 0
   transfer page[cacheCounter] to slot cacheCounter
   inc cache counter
   cache coutner := 0 then exit

Cargamos con esto las primeras 64 paginas en cache
 A23-A10 es el identificador de pagina
 A9-A0 son los 1024 bytes de la página

On read
 - once address is latched
   if page is in the cache table
   a15-a10 = cacheEntryHit! (el numero de slot, coincide con la posicion de la pagina en SRAM
    a9-a0  = a9-a9 from Address
   enable sram output

   if page is not cache
     dump current slot(it's the oldest one) if it is modified, copy to dram
     down full page
     return value from sram

On write
 - once address is latched
    if page is in the cache table
     a15-a10 = cacheEntryHit! (el numero de slot, coincide con la posicion de la pagina en SRAM
     a9-a0  = a9-a9 from Address
    enable sram input

   if not
     dump current slot(it's the oldest one) if it is modified, copy to dram
     down full page
     write to sram

Tendre
    CPU - FPGA - PSRAM from     
               - SRAM ()

LQ100 tiene 79 IOS
16+8 para la cpu        24
6    para la sram       30
8    dos PSRAMs         38

Me quedan 41 para señales de control. DEBERIA VALER

A la SRAM van directos los datos, A9-A10. A15-A10 vienen de la MMU

module mmu
    a [15:0] input
    d [ 7:0] inout
    clock    out

reg [23:10] cachePages [5:0];

cachePages[ 0] = 6'b000000;
cachePages[ 1] = 6'b000001;
...
cachePages[63] = 6'b111111;

wire [0:63] cacheHit;

assign cacheHit[0] = cachePages[0] == A[23:10];

for (page = 0 to 63) {
    if cacheHit[page] assign extAddr = page
}


