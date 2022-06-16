#!/bin/sh
# Copyright 2016-2022 Intel Corporation
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
    nm -g "${SL_PREFIX}/lib/libcart.so" |
        grep -v " U " |  grep -v " _crt"
else
    echo "checking libcart.so"
    nm -g "${SL_PREFIX}/lib/libcart.so" |
        grep -v " U " |  grep -v " w " |  grep -v " crt_" | grep -v " swim_" |
        grep -v " D CQF_" |
        grep -v " D _edata" | grep -v " T _fini" | grep -v " T _init" |
        grep -v " B __bss_start" | grep -v " B _end";
    if [ $? -ne 1 ]; then RC=1; fi
    echo "checking libgurt.so"
    nm -g "${SL_PREFIX}/lib/libgurt.so" |
        grep -v " U " |  grep -v " w " |  grep -v " d_" | grep -v " DB_" |
        grep -v " D _edata" | grep -v " T _fini" | grep -v " T _init" |
        grep -v " B __bss_start" | grep -v " B _end" |
        grep -v " T chash_" | grep -v " B __drand48_seed";
    if [ $? -ne 1 ]; then RC=1; fi
fi
if [ ${RC} -ne 0 ]
then
    echo Fail: Incorrect symbols exist
    exit 1
fi
echo Pass: No incorrect symbols exist
exit 0
