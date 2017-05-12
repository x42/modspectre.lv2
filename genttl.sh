#!/bin/bash
BINS=$1
IDX=3

n=0; while ((n++ < $BINS)); do
	sed "s/@IDX@/$IDX/;s/@BIN@/$n/" << EOF
	] , [
		a lv2:OutputPort, lv2:ControlPort;
		lv2:index @IDX@;
		lv2:symbol "bin@BIN@";
		lv2:name "Freq Bin @BIN@";
		lv2:minimum 0;
		lv2:maximum 1;
EOF
	IDX=$(($IDX + 1))
done

echo "]; ."
