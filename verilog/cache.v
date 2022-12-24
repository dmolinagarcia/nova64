module cache (
    input   [15:0]  a,
    input   [ 7:0]  d,
    output          phi2,
    input           fpgaClk,
    output  [ 5:0]  sram_addr,
    output          sram_ce
);

reg  [ 3:0]   mmuState     = 4'b0000;
reg  [23:0]   Addr         = 24'h000000;
wire [13:0]   page         = Addr [23:10];

// Cache table
reg [13:0]  cachePages   [3:0];

initial begin
   for (int i=0; i<=4; i++) begin
      cachePages[i] = 14'b00000000000000 + i;
   end

end

wire [3:0] cachePagesHit = { cachePages[3] == Addr[23:10], 
                             cachePages[2] == Addr[23:10], 
                             cachePages[1] == Addr[23:10], 
                             cachePages[0] == Addr[23:10]
                           };

wire       cacheHit = !(cachePagesHit == 0) & phi2;

reg [1:0] pageHit;
assign sram_addr = cacheHit ? pageHit : 6'bz;
assign sram_ce   = mmuState == 4'b1001;

// Encoder
always @* begin
    case (cachePagesHit)
        4'b0001: pageHit <= 2'b00;
        4'b0010: pageHit <= 2'b01;
        4'b0100: pageHit <= 2'b10;
        4'b1000: pageHit <= 2'b11;
    endcase
end

assign phi2 = mmuState[3];

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
                          end else begin
                            mmuState <= 4'b1100;        // If not, we jump tp page refresh. SRAM will be enbled when doen
                          end
                4'b1001 : mmuState <= 4'b0000;          // Here we disable SRAM

                // Cache Refresh
                4'b1100 : mmuState <= 4'b1101;
                4'b1101 : mmuState <= 4'b1110;
                4'b1110 : mmuState <= 4'b1111;
                4'b1111 : mmuState <= 4'b1001;          // Return to SRAM 
            endcase
        end
    end


endmodule
