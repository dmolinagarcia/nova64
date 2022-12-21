 `timescale 10ns/10ns

module test;

// Simulation
  initial begin
     $dumpfile("/tmp/cache.vcd");
     $dumpvars(0,test);

     #      5000 $finish;
  end

/* FPGA CLOCK */
  // Required for the 6502. At 10x PHI2, already fails
  reg fpga=0;
  always #1 fpga = !fpga;

  wire phi2;

  cache cache1 ( , , phi2, fpga);           

endmodule // test

