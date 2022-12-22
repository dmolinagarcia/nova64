---
categories: memory
---

## Memory architecture. Cache and DRAM

Memory choice? Sram? dram? psram?

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

always on posedge PHI2

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

``` 
module cache (
    input   [15:0]  a,
    input   [ 7:0]  d,
    output          phi2,
    input           fpgaClk
);

reg [3:0]   mmuState     = 4'b0000;

// Cache table
reg [13:0]  cachePages   [3:0];

initial begin
   for (int i=0; i<=4; i++) begin
      cachePages[i] = 14'b00000000000010 + i;
   end
end

wire [3:0] cachePagesHit = { cachePages[3] == a[13:0], 
                             cachePages[2] == a[13:0],
                             cachePages[1] == a[13:0],
                             cachePages[0] == a[13:0]};

wire       cacheHit = !(cachePagesHit == 0);

reg [1:0] pageHit;
wire [5:0] addressInCache;

assign addressInCache = cacheHit ? pageHit : 6'bz;

always @* begin
    case (cachePagesHit)
        4'b0001: pageHit <= 2'b00;
        4'b0010: pageHit <= 2'b01;
        4'b0100: pageHit <= 2'b10;
        4'b1000: pageHit <= 2'b11;
    endcase
end

assign phi2 = (mmuState < 4'b0101) ? 1'b0 : 1'b1;

always @(posedge fpgaClk) begin
        begin
            mmuStatePrev <= mmuState;
            case (mmuState)
                4'b0000 : mmuState <= 4'b0001;
                4'b0001 : mmuState <= 4'b0010;
                4'b0010 : mmuState <= 4'b0011;
                4'b0011 : mmuState <= 4'b0100;
                4'b0100 : mmuState <= 4'b0101;
                4'b0101 : mmuState <= 4'b0110;
                4'b0110 : mmuState <= 4'b0111;
                4'b0111 : mmuState <= 4'b1000;
                4'b1000 : mmuState <= 4'b1001;
                4'b1001 : mmuState <= 4'b0000;
            endcase
        end
    end
```  

endmodule




      CPU                                               HELIUM (NorthBridge)             PSRAM1            PSRAM2
+---------------------+                               +----------------------+          +------+          +------+
|                     |                               |                      +----------+      +----------+      |
|               PHI2  <-------------------------------+                      | QPI[3:0] |      | QPI[3:0] |      |
|                     |                               |                      +----------+      +----------+      |
|                     |                               |                      |          |      |          |      |
|                     |                               |                      |          +-^--^-+          +-^--^-+
|                     |                               |                      |   RAMCLK   |  |              |  |
|                     +-------------------------------+                      +------------+--+--------------+  |
|                     |           A[15:0]             |                      |   RAMCS1      |                 |
|                     +-+  +-----------+ +------------+                      +---------------+                 |
|                     | |  |    A[9:0] | |            |                      |   RAMCS2                        |
|                     | |  |           | |            |                      +---------------------------------+
|                     +-+--+-----------+ +------------+                      |
|                     |                | |   D[7:0]   |                      |
|                     +-+--+------+  +-+ +------------+                      |
|                     | |  |      |  | | +------------+                      |
|                     | |  |      |  | | |   A[15:10] |                      |
|                     | |  |      |  | | | +----------+                      |
+---------------------+ |  |      |  | | | |          +------+---+---+-------+
                        |  |      |  | | | |                 |   |   |
                        |  |      |  | |   |                 |   |   |
                        |  |      |  | |   |           +-----v---v---v------+
                        |  |      |  | |   +-----------+    /CE /OE /WE     |
                        |  |      |  | |               |                    |
                        |  |      |  | +---------------+                    |
                        |  |      |  +-----------------+                    |
                        |  |      |            D[7:0]  |                    |
                        |  |      |  +-----------------+                    |
                        |  |      |  |                 +--------------------+
                        |  |      |  |                       SRAM  CACHE
                        |  |      |  |
                        |  |      |  |                      NEON  (Video)               PSRAM3
                        |  |      |  |                 +--------------------+          +------+
                        |  |      |  +-----------------+                    +----------+ VRAM |
                        |  |      |                    |                    | QPI[3:0] |      |
                        |  |      +--------------------+                    +----------+      |
                        |  |                           |                    | VRAMCLK  |      |
                        |  |                           |                    +---------->      |
                        |  +---------------------------+                    | VRAMCS   |      |
                        |             A[15:0]          |                    +---------->      |
                        +------------------------------+                    |          |      |
                                                       +--------------------+          +------+



Helium es una state machine

0 PHI2=0
1 WAIT
2 LATCH BANK
3 WAIT
4 PHI2=1
5 LATCH ADDR
6 IF CACHE enble RAM
7 NOT CACHE enter recache
  -
  -
  -
  -
back to 0





// Below is a post in forum6502 about this. Use it as best suits

The more I think about it, the more clear is seems that the MMU will be the core of the nervous system of the noVa64. To further complicate my life, and following fachat idea of adding some SRAM to the system but with a twist. I've added the last of the requirements to the list.

The noVa64 will have 16MB of DRAM as main memory, plus a still to be determined amount of SRAM to act as cache. Following the principles of the project, this is mostly educational, so complexity is welcome.

The easiest solution would be to have direct page on a 64KB RAM, the rest on DRAM. I'm drafting however a cache method. My first draft currently looks like this: The memory will be divided in 1KB pages. Thus, 64 pages on a 64 KB SRAM. It can easily be adapted for bigger SRAMS, but let's stick to this size for now.

Bits A9-A0 from the address bus connect to same pins of the SRAM.
Bits A23-A10 are the page identifier.
Bits A15-A10 for the SRAM come from the MMU. WE, CE, OE also are driven by the MMU.

The MMU will be controled by a state machine that handles the whole timing of the computer. 

[list=1]
[*] PHI2 is driven low.
[*] CPU outputs BA over the databus
[*] MMU drives PHI2 high, latching BA
[*] Wait until the address bus is stable, and latch the rest of the Address
[*] Now that we have the address, check if is the page (A23-A10) exists on the SRAM
[*] If it does, output the corresponding A10-A15 for the SRAM
[*] If it doesn't, halt the MMU state machine, get the 1KB page from DRAM, store in the SRAM, and continue. A page may need to be flushed from SRAM and copied over to DRAM at this point. 
[*] Drive CE, WE and OE for the SRAM as needed
[*] PHI2 is driven low, CE, WE and OE are deasserted, and the cycle begins again [/list]

In the end, all reads and writes are done to SRAM, and I'm halting the CPU by stretching PHI2 whenever a page needs to be moved from or to the DRAM. The cache controller may have some internal registers to keep stats on itself, such as page hits, that can be read back by the CPU. So the cache controller itself will be a device addressable on the IO map, allowing to configure it from running code! Switching algorithms, forcing cache of specific pages, etc. 

Sure, implemeting the cache algorithm is going to be fun. I've just starting toying with verilog!

In order to reduce pin count, and facilitate the PCB design, I will give [url=https://www.mouser.es/ProductDetail/AP-Memory/APS6404L-3SQR-SN?qs=sGAEpiMZZMu4dzSHpcLNgryWTX0PzjALJD77fQPRDmG8F9HqV9KSxw%3D%3D]PSRAM[/url] a try. With a handful of lines, I can have 16MB on 2 ICs. No routing nightmares, and, as this may very well be in the 100MHz speed.... I can have a very tightly packed PCB. I'm prepared to enter some very unknown realm here.

For this idea to be feasible, with a nice CPU clock speed, the MMU itself will need to run at fairly high speed. Probably no less that 4 times the CPU (14Mhz for the CPU, 56Mhz for the FPGA) and even faster if I want to get the max performance out of the DRAM. By using PSRAM I avoid a considerable amount of work at all levels, while maintaining a fairly complex and powerful design.

Finally, as the MMU will be, as I said, the core of the noVa64, I'm giving it a proper name now. Following the stellar theme, I'll use elements created in stars for the components. So, [b][u]Helium[/u][/b] it is.