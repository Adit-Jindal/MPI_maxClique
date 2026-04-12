#!/bin/bash

EXEC=./main
INPUT=input.txt
OUTPUT=out.txt
SEQ_TIME=120.0

# number of processes to test
PROCS=(2 4 6 8 10 12 14 16)

# output data file
DATAFILE=results.txt

# clear old data
echo "#procs time speedup" > $DATAFILE

echo "Running np=1 as baseline..."

OUT=$(time mpirun -np 1 $EXEC $INPUT $OUTPUT)

TIME1=$(echo "$OUT" | grep "Time : " | awk '{print $2}')

echo "1 $TIME1 1.0" >> $DATAFILE   # speedup for baseline = 1

echo "Running experiments..."

for p in "${PROCS[@]}"
do
    echo "Running with $p processes..."

    OUT=$(time mpirun -np $p $EXEC $INPUT $OUTPUT)

    TIME=$(echo "$OUT" | grep "Time : " | awk '{print $2}')

    # compute speedup = TIME1 / TIME
    SPEEDUP=$(awk -v t1="$TIME1" -v t="$TIME" 'BEGIN {print t1/t}')

    echo "$p $TIME $SPEEDUP" >> $DATAFILE
done

echo "Done. Results saved in $DATAFILE"

python3 plotting.py