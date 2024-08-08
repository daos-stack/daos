#!/bin/bash

rm /mnt/pmem0/engine0/*

# UPDATE
export VOS_PERF_CMD='U;p'

./run_vos_perf.sh > step_01_output.txt
