module cache (
    input   [15:0]  a,
    input   [ 7:0]  d,
    output  reg     phi2,
    input           fpgaClk,
    output  [ 5:0]  sram_addr
);

reg [ 3:0]   mmuState     = 4'b0000;
reg [23:0]   Addr         = 24'h000000;

// Cache table
reg [13:0]  cachePages   [3:0];

initial begin
   for (int i=0; i<=4; i++) begin
      cachePages[i] = 14'b00000000000000 + i;
   end
   phi2 <= 1'b0;
end

wire [3:0] cachePagesHit = { cachePages[3] == Addr[23:10], 
                             cachePages[2] == Addr[23:10], 
                             cachePages[1] == Addr[23:10], 
                             cachePages[0] == Addr[23:10]
                           };

wire       cacheHit = !(cachePagesHit == 0);

reg [1:0] pageHit;
wire [5:0] addressInCache;

assign sram_addr = cacheHit ? pageHit : 6'bz;

always @* begin
    case (cachePagesHit)
        4'b0001: pageHit <= 2'b00;
        4'b0010: pageHit <= 2'b01;
        4'b0100: pageHit <= 2'b10;
        4'b1000: pageHit <= 2'b11;
    endcase
end





always @(posedge fpgaClk) begin
        begin
            case (mmuState)
                4'b0000 : mmuState <= 4'b0001;                                  
                4'b0001 : mmuState <= 4'b0010;
                4'b0010 : mmuState <= 4'b0101;
                4'b0101 : begin 
                    mmuState    <= 4'b0110;                                     // PHI2 HIGH    
                    Addr[23:16] <= d;                                           // Latch BA from DB
                    phi2        <= 1'b1;
                end           
                4'b0110 : mmuState <= 4'b0111;                                  // NOP. Wait for Addr
                4'b0111 : begin
                    mmuState   <= 4'b1000;                                  
                    Addr[15:0] <= a;                                             // Latch A
                end
                4'b1000 : if ( cacheHit ) begin
                             mmuState <= 4'b1001;                                                   // Cache Hit. Execute
                          end else begin
                             mmuState <= 4'b1010;                                                   // CACHE REFRESH
                          end
                4'b1001 : begin
                    mmuState <= 4'b0000;                                          // Restart cycle
                    phi2 <= 1'b0;
                end
                4'b1010 : mmuState <= 4'b1011;                                  // CACHE REfresh cycle 1
                4'b1011 : mmuState <= 4'b1100;                                  // CACHE REfresh cycle 1
                4'b1100 : mmuState <= 4'b1101;                                  // CACHE REfresh cycle 1
                4'b1101 : mmuState <= 4'b1001;                                  // Cache Refreshed, Resume.
            endcase
        end
    end


endmodule


/* 



reg [13: 0] cachePages [5:0];

cachePages[ 0] = 6'b000000;
cachePages[ 1] = 6'b000001;
...
cachePages[63] = 6'b111111;

wire [0:63] cacheHit;

assign cacheHit[0] = cachePages[0] == A[23:10];
...

    assign extAddr = cachepages[encoder(cachehit)]

*/