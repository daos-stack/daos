#!/bin/sh

set -e
set -x

if [ -z "$WORKSPACE" ]; then
  WORKSPACE=`readlink -f ..`
fi

os=`uname`
if [ "$os" = "Darwin" ]; then
export DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH:${WORKSPACE}/scons_local/install/lib
else
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${WORKSPACE}/scons_local/install/lib
fi
export PATH=${WORKSPACE}/scons_local/install/bin:$PATH

export OPAL_PREFIX=${WORKSPACE}/scons_local/install

#Allow overcommit of CPUs
export OMPI_MCA_rmaps_base_oversubscribe=1

echo Trying to run pmix tests.
orterun -np 2 ${WORKSPACE}/pmix/examples/client
