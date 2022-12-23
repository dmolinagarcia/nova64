iverilog -g2012 -o cache *.v
vvp cache
gtkwave cache.vcd
del cache cache.vcd