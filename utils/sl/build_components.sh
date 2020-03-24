#!/bin/sh
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

option=
if [ -n "$JOB_LOC" ]; then
    export option="TARGET_PREFIX=${JOB_LOC} ${SRC_PREFIX_OPT}"
elif [ -n "$WORKSPACE" ]; then
    export option="TARGET_PREFIX=\
${CORAL_ARTIFACTS}/${JOB_NAME}/${BUILD_NUMBER} SRC_PREFIX=../"
fi

: ${BUILD_OPTIONS:=""}

scons ${BUILD_OPTIONS} $option --config=force --build-deps=yes $*

if [ -n "$WORKSPACE" ]; then
if [ -z "$JOB_LOC" ]; then
    ln -sfn ${BUILD_NUMBER} ${CORAL_ARTIFACTS}/${JOB_NAME}/latest
fi
fi
