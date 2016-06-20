#!/bin/sh

set -e
set -x

if [ -z "$WORKSPACE" ]; then
  WORKSPACE=`readlink -f ..`
fi

echo `pwd`
source `pwd`/.build_vars.sh

os=`uname`
if [ "$os" = "Darwin" ]; then
export DYLD_LIBRARY_PATH=${SL_LD_LIBRARY_PATH}:${DYLD_LIBRARY_PATH}
else
export LD_LIBRARY_PATH=${SL_LD_LIBRARY_PATH}:${LD_LIBRARY_PATH}
fi
export PATH=${SL_PATH}:${PATH}

export OPAL_PREFIX=${SL_OMPI_PREFIX}

#Allow overcommit of CPUs
export OMPI_MCA_rmaps_base_oversubscribe=1

echo Trying to run pmix tests.
orterun -np 2 ${WORKSPACE}/pmix/examples/client
