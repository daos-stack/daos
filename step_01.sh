#!/bin/bash

rm /mnt/pmem0/engine0/*

# UPDATE
export VOS_PERF_CMD='U;p'

# Note: script is used to record both stderr and stdout and to collect
# as much of the output as possible before the immediate power off.
script -c './run_vos_perf.sh' step_01_output.txt
