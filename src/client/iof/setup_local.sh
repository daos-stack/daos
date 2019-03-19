# Copyright (C) 2016 Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted for any purpose (including commercial purposes)
# provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions, and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions, and the following disclaimer in the
#    documentation and/or materials provided with the distribution.
#
# 3. In addition, redistributions of modified forms of the source or binary
#    code must carry prominent notices stating that the original code was
#    changed and the date of the change.
#
#  4. All publications or advertising materials mentioning features or use of
#     this software are asked, but not required, to acknowledge that it was
#     developed by Intel Corporation and credit the contributors.
#
# 5. Neither the name of Intel Corporation, nor the name of any Contributor
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
VARS_FILE=./.build_vars-`uname -s`.sh

if [ ! -f $VARS_FILE ]
then
    echo Build vars file $VARS_FILE does not exist
    echo Cannot continue
    exit 1
fi

. $VARS_FILE

os=`uname`
if [ "$os" = "Darwin" ]; then
    if [ -n "$DYLD_LIBRARY_PATH" ]; then
	export DYLD_LIBRARY_PATH=${SL_LD_LIBRARY_PATH}:$DYLD_LIBRARY_PATH
    else
	export DYLD_LIBRARY_PATH=${SL_LD_LIBRARY_PATH}
    fi
fi

if [ -z "$SL_PREFIX" ]
then
    SL_PREFIX=`pwd`/install
fi

export PATH=$SL_PREFIX/bin:$PATH
export PATH=$SL_OMPI_PREFIX/bin:$PATH
export PATH=$SL_CART_PREFIX/bin:$PATH

# Set this so that the test code knows where to find the CaRT valgrind
# suppression files.
export IOF_CART_PREFIX=$SL_CART_PREFIX

# Set some ORTE runtime variables, use "orte-info --param all all" for help on
# individual options.

# Allow overcommit of CPUs.
export OMPI_MCA_rmaps_base_oversubscribe=1

# Prevent orte from killing the job on single process failure
# export OMPI_MCA_orte_abort_on_non_zero_status=0

# Use /tmp for temporary files.  This is required for OS X.
export OMPI_MCA_orte_tmpdir_base=/tmp
