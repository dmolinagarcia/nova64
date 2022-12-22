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
                4'b0000 : mmuState <= 4'b0001;                                  // PHI2 LOW
                4'b0001 : mmuState <= 4'b0010;
                4'b0010 : mmuState <= 4'b0011;
                4'b0011 : mmuState <= 4'b0100;
                4'b0100 : mmuState <= 4'b0101;
                4'b0101 : mmuState <= 4'b0110;                                  // PHI2 HIGH    
                                                                                // Latch BA                
                4'b0110 : mmuState <= 4'b0111;
                4'b0111 : mmuState <= 4'b1000;                                  // Latch A
                4'b1000 : mmuState <= 4'b1001;                                  // If in cache, enable SRAM
                                                                                // Otherwise, jump to Cache Page Refresh
                4'b1001 : mmuState <= 4'b0000;
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