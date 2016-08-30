#!/bin/sh

#set -x

if [ -d "utils" ]; then
  . utils/setup_local.sh
else
  . ./setup_local.sh
fi

# Check for symbol names in the library.  All symbol names should begin with a
# the prefix crt_ however slightly different ways of testing are needed on OS X
# and Linux.
RC=0
echo Checking for symbol names.
if [ "$os" = "Darwin" ]
then
    nm -g ${SL_PREFIX}/lib/libcrt.so |
        grep -v " U " |  grep -v " _crt"
else
    echo "checking libcrt.so"
    nm -g ${SL_PREFIX}/lib/libcrt.so |
        grep -v " U " |  grep -v " w " |  grep -v " crt_" |
        grep -v " D _edata" | grep -v " T _fini" | grep -v " T _init" |
        grep -v " B __bss_start" | grep -v " B _end" |
        grep -v " B addr_lookup_table" | grep -v " D DMF_" | grep -v " D DQF_";
    if [ $? -ne 1 ]; then RC=1; fi
    echo "checking libcrt_util.so"
    nm -g ${SL_PREFIX}/lib/libcrt_util.so |
        grep -v " U " |  grep -v " w " |  grep -v " crt_" |
        grep -v " D _edata" | grep -v " T _fini" | grep -v " T _init" |
        grep -v " B __bss_start" | grep -v " B _end" |
        grep -v " T CP_UUID" | grep -v " T dhash_";
    if [ $? -ne 1 ]; then RC=1; fi
fi
if [ ${RC} -ne 0 ]
then
    echo Fail: Incorrect symbols exist
    exit 1
fi
echo Pass: No incorrect symbols exist
exit 0
