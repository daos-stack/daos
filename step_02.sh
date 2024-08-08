#!/bin/bash

# REOPEN & VERIFY
export VOS_PERF_CMD='V;p'
export VOS_PERF_OPEN_OPT='-u bdc2a992-5bba-4e86-a8a8-030e422502ad -X 85ece417-608a-405d-8049-1a1f8fec9bd7'

# Note: script is used to record both stderr and stdout
script -c './run_vos_perf.sh' step_02_output.txt
