#!/bin/sh
# Copyright (C) 2017 Intel Corporation
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

. ./setup_local.sh

prefixes=iof_
syms=
weak=

function check_library()
{
    syms_copy="${syms} _init _fini __bss_start _end _edata"

    nm -g ${SL_PREFIX}/lib/$1 > symbols.txt
    cmd="cat symbols.txt | grep -v \" [Uw] \""
    for prefix in $prefixes; do
        cmd+=" | grep -v \"[WTBD] $prefix\""
    done
    for sym in $syms_copy; do
        cmd+=" | grep -v \"[WTBD] $sym\$\""
    done

    echo Checking $1
    eval $cmd
    if [ $? -ne 1 ]; then
        echo "Leaked symbols in $1"
        RC=1;
    fi

    # Check that expected symbols are in library
    for sym in $syms; do
       cat symbols.txt | grep $sym >> /dev/null
       if [ $? -ne 0 ]; then
         echo "Missing symbol $sym"
         RC=1
       fi
    done

    # Check that expected symbols are weak
    for sym in $weak; do
       cat symbols.txt | grep $sym | grep " W " >> /dev/null
       if [ $? -ne 0 ]; then
         echo "Symbol $sym should be weak"
         RC=1
       fi
    done

    rm symbols.txt
}

# Check for symbol names in the library.  All symbol names should begin with
# the prefix iof_
RC=0
echo Checking for symbol names.
check_library libiof.so
source ${SL_PREFIX}/TESTING/scripts/check_ioil_syms
check_library libioil.so

if [ ${RC} -ne 0 ]
then
    echo Fail: Incorrect symbols exist
    exit 1
fi
echo Pass: No incorrect symbols exist
exit 0
