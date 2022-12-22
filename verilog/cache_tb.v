 `timescale 10ns/10ns

module test;

// Simulation
  initial begin
    $dumpfile("/tmp/cache.vcd");
    $dumpvars(0,cache1);
    for (int idx = 0; idx < 4; idx = idx + 1)
      $dumpvars(0,cache1.cachePages[idx]);
   

     #      5000 $finish;
  end


/* FPGA CLOCK */
  // Required for the 6502. At 10x PHI2, already fails
  reg fpga=1;
  always #1 fpga = !fpga;

  wire phi2;
  reg  [15:0] a = 16'h0000;

  always @(posedge phi2) a <= a + 1'b1;

  cache cache1 ( a, , phi2, fpga);           

endmodule // test

