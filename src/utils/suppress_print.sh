#! /bin/bash
#
# Simple script to suppress print all but rank 0
# works only with Open MPI.
# Uses OMPI_COMM_WORLD_RANK env variable
# Can be used to suppress Cmocka prints from command-line
# from all processes except 0.
# suppress_print.sh log_file_name ./test_command args.

LOG_FILE=$1

# shift left the $#
shift 1

MY_RANK=$OMPI_COMM_WORLD_RANK

if [ $MY_RANK -eq 0 ]; then
    eval $@ > $LOG_FILE 2>&1
else
    eval $@ > /dev/null 2>&1
fi
