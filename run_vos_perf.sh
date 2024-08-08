#!/bin/bash

# UPDATE & VERIFY
# cmd='U;p V;p'

# -i Use integer dkeys. Required if running QUERY test.
# -A [R] Use array value of akey, single value is selected by default. optional parameter 'R' indicates random writes
# -I Use constant akey. Required for QUERY test.
REQ_FOR_QUERY_OPTS='-i -A -I'

# DEBUG_OPT='-w'

seed=1
# -o number Number of objects are used by the utility.
obj_per_cont=256
dkey_per_obj=512
# -n number Number of strides per akey.
recx_per_akey=32

# debug
# obj_per_cont=1
# dkey_per_obj=1
# recx_per_akey=1

MPI_OPTS='-c 10 --cpu-list 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14 --bind-to cpu-list:ordered'
# MPI_DEBUG_OPTS='--report-bindings'

# Necessary when running against side-loaded PMDK libraries (LIBPMEM OBJ and LIBPMEM)
# export LD_LIBRARY_PATH=/opt/daos/prereq/debug/pmdk/lib

/usr/lib64/openmpi/bin/mpirun $MPI_OPTS $MPI_DEBUG_OPTS \
        /opt/daos/bin/vos_perf -D /mnt/pmem0/engine0 -P 2G $REQ_FOR_QUERY_OPTS \
        $DEBUG_OPT $VOS_PERF_OPEN_OPT \
        -R "$VOS_PERF_CMD" \
        -G $seed -o $obj_per_cont -n $recx_per_akey -d $dkey_per_obj
