 `timescale 10ns/10ns

module test;

// Simulation
  initial begin
    $dumpfile("cache.vcd");
    $dumpvars(0,test);
    for (int idx = 0; idx < 4; idx = idx + 1)
      $dumpvars(0,cache1.cachePages[idx]);
      #      500000 $finish;
  end


/* FPGA CLOCK */
  reg fpga=1;
  always #1 fpga = !fpga;

  reg  [23:0] Addr = 24'h000000;
  
  wire phi2;
  wire [15:0] a;
  wire [ 7:0] d;

  always @(negedge phi2) Addr <= Addr + 24'b001010001010010010010010;
  assign d = phi2 ? 8'bz : Addr[23:16];
  assign a = Addr [15:0];

  cache cache1 ( a, d , phi2, fpga, );           

endmodule // test

