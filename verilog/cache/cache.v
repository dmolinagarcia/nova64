module cache (
    input   [15:0]  a,
    input   [ 7:0]  d,
    output          phi2,
    input           fpgaClk,
    output  [ 5:0]  sram_addr,
    output          sram_ce
);

reg  [ 3:0]   mmuState           = 4'b0000;                 // MMU State Machine current state
reg  [23:0]   Addr               = 24'h000000;              // Address latched from the CPU
wire [13:0]   page               = Addr [23:10];            // Page in memory (1kb pages)
reg  [ 1:0]   oldestPage         = 2'b00;                   // Oldest page in cache, next to be flushed
reg  [13:0]   cachePages  [3:0];                            // Cache table

// We begin by populating the cache. For now, pages 0 - 3
// This won't probably work on real hardware.
initial begin
   for (int i=0; i<=4; i++) begin
      cachePages[i] = 14'b00000000001111 + i;
   end
end

assign sram_addr = cacheHit ? pageHit : 6'bz;               // Upper address lines to select from SRAM
assign sram_ce   = mmuState == 4'b1001;                         // SRAM CE signal. Active high for now
                                                                // OE and WE still missing
assign phi2      = mmuState[3];                                 // PHI2 for the CPU
                                                                // MMU Streches PHI2 by moving into higher states when a refresh happens

wire [3:0] cachePagesHit = { cachePages[3] == page, 
                             cachePages[2] == page, 
                             cachePages[1] == page, 
                             cachePages[0] == page
                           };                                    // Vector indicating if a page is hitted in cache

wire       cacheHit = !(cachePagesHit == 0) & phi2;              // Signal that indicates if there's a hit

// Encoder. Turns the cachePagesHit vector into a pointer into cachePages
reg [1:0] pageHit;
always @* begin
    case (cachePagesHit)
        4'b0001: pageHit <= 2'b00;
        4'b0010: pageHit <= 2'b01;
        4'b0100: pageHit <= 2'b10;
        4'b1000: pageHit <= 2'b11;
    endcase
end

reg success = 1'b0;

// MMU STATE MACHINE

always @(posedge fpgaClk) begin
        begin
            case (mmuState)
                4'b0000 : mmuState <= 4'b0001;                                  
                4'b0001 : begin
                    mmuState <= 4'b1000;
                    Addr <= {d, a};
                end                    
                4'b1000 : if ( cacheHit ) begin
                            mmuState <= 4'b1001;        // Here we enable SRAM if pageHit
                            success  <= 1'b1;
                          end else begin
                            mmuState <= 4'b1100;        // If not, we jump tp page refresh. SRAM will be enbled when doen
                          end
                4'b1001 : mmuState <= 4'b0000;          // Here we disable SRAM

                // Cache Refresh
                4'b1100 : mmuState <= 4'b1101;          // Copy Cache Page to DRAM is modified
                4'b1101 : begin
                    mmuState <= 4'b1110;
                    cachePages[oldestPage] <= page;     // Download pages from RAM, update cache
                    oldestPage <= oldestPage + 1'b1;    // Increase page pointer
                end
                4'b1110 : mmuState <= 4'b1111;
                4'b1111 : mmuState <= 4'b1001;          // Return to SRAM 
            endcase
        end
    end


endmodule
