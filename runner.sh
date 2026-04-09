#!/bin/bash

EXEC=./a.out
INPUT=input.txt
OUTPUT=out.txt
SEQ_TIME=120.0

# number of processes to test
PROCS=(2 4 6 8 10 12 14 16)

# output data file
DATAFILE=results.txt

# clear old data
echo "#procs time" > $DATAFILE

echo "Running experiments..."

for p in "${PROCS[@]}"
do
    echo "Running with $p processes..."

    # run MPI program and capture output
    OUT=$(mpirun -np $p $EXEC $INPUT $OUTPUT)

    # extract time (assuming "Time: X sec")
    TIME=$(echo "$OUT" | grep "Time:" | awk '{print $2}')

    echo "$p $TIME" >> $DATAFILE
done

echo "Done. Results saved in $DATAFILE"