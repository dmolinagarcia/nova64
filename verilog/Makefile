simu: simulate clean

simulate:
	iverilog -o cache *.v
	vvp cache
	gtkwave /tmp/cache.vcd

clean:
	rm -f cache cache.vcd