#! /bin/bash
# This script is deprecated as programming fpga frequenctly cause a hung server.

if [ $# != 3 ]; then
	echo "Usage: ./run_bandwidth_profile.sh host_app.exe kernel.xclbin output.csv";
	exit
fi

if [ ! -f $1 ] || [ ! -f $2 ] ; then
    echo "input file does not exist!";
    exit
fi

HOST_APP=$1
KERNEL_BIN=$2
OUTPUT_CSV=$3

echo "Test starts."
echo "Test starts [$HOST_APP ]." >> run.log


for NUM_KERNEL in {1..32}
do
	for IDX_OFFSET in {0..8}
	do
		ACCESS_OFFSET=$(( IDX_OFFSET * 8388608 ))
		ACCESS_OFFSET_MB=$(( IDX_OFFSET * 32 ))

		echo "HBM bandwidth testing with NUM_KERNEL: $NUM_KERNEL, OFFSET: $ACCESS_OFFSET_MB MB."
		echo "HBM bandwidth testing with NUM_KERNEL: $NUM_KERNEL, OFFSET: $ACCESS_OFFSET_MB MB." >> run.log
		exec ./$HOST_APP ./$KERNEL_BIN $NUM_KERNEL $ACCESS_OFFSET | sed -n '5p' >> $OUTPUT_CSV
	done
done

echo "Complished."