#!/bin/bash
# Copyright (c) 2016 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

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
