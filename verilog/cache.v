module cache (
    input   [15:0]  a,
    input   [ 7:0]  d,
    output          phi2,
    input           fpgaClk
);

reg [3:0]   mmuState     = 4'b0000;
reg [3:0]   mmuStatePrev = 4'b0000;

// Cache table
reg [13:0]  cachePages   [3:0];

initial begin

   for (int i=0; i<=4; i++) begin
      cachePages[i] = 14'b00000000000000 + i;
   end

end

wire [7:0] cachePages0 = cachePages[0];
wire [7:0] cachePages1 = cachePages[1];
wire [7:0] cachePages2 = cachePages[2];
wire [7:0] cachePages3 = cachePages[3];

wire [3:0] cachePagesHit = { cachePages[3] == a[13:0], 
                             cachePages[2] == a[13:0],
                             cachePages[1] == a[13:0],
                             cachePages[0] == a[13:0]};

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