simu: simulate clean

simulate:
	iverilog -g2012 -o cache *.v
	vvp cache
	gtkwave /tmp/cache.vcd

clean:
	rm -f cache cache.vcd