#!/bin/bash
# Copyright (C) 2018-2019 Intel Corporation
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

set -e

if [ ! -d "utils/sl" ];then
  cd ..
fi
# Set PYTHONPATH for source files not installed files
PYTHONPATH=$PWD/utils:$PWD/src/tests/ftest/util/
PYTHONPATH=$PYTHONPATH:$PWD/src/tests/ftest/cart/util/
PYTHONPATH=$PYTHONPATH:$PWD/src/tests/ftest/util/apricot/
PYTHONPATH=$PYTHONPATH:$PWD/src/client/
export PYTHONPATH

if [ -z "$*" ]; then
  flist="-c utils/daos_build.py -s SConstruct"
  # Exclude raft and utils/sl
  scripts=$(find . -name SConscript | grep -v -e utils/sl -e raft \
          -e build/external | sort)
  for file in $scripts; do
    flist+=" -s $file "
  done
  # the functional test code
  flist+=" $(find src/tests/ftest/ -name \*.py | sort)"
  flist+=" $(find src/client/pydaos/ -name \*.py | sort)"
  flist+=" $(find src/client/dfuse/test/ -name \*.py | sort)"
  flist+=" $(find src/cart/ -name \*.py | sort)"
  flist+=" $(find utils/ -name \*.py | sort)"
else
  flist=$*
fi

# $flist is a list of switches and arguments; quoting will make it a
# single argument
# shellcheck disable=SC2086
if ! ./utils/sl/check_python.sh $flist; then
  exit 1
fi
exit 0
