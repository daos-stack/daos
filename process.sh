#!/bin/bash

for output in results/immediate_off/*/step_02_output.txt; do
        echo $output
        cat $output | grep 'Values found and verified' | sort | sed 's/\[\([0-9]\{2\}\)\] Values found and verified=\([0-9]*\), missing=\([0-9]*\)/\1,\2,\3/' > $output.csv
done
