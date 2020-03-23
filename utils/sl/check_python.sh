#!/bin/bash
# Copyright (c) 2016-2019 Intel Corporation
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

SCRIPT_DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" > /dev/null 2>&1 && pwd)
rm -f pylint.log

fail=0
while [ $# != 0 ]; do
    if [ "$1" = "-c" ]; then
        #Run the self check on prereq_tools and various
        #helper scripts
        echo Run self check
        if ! "$SCRIPT_DIR"/check_script.py -s; then
	    fail=1
        fi
    elif [ "$1" = "-s" ]; then
        #Check a SCons file
        shift
        if [ ! -f "$1" ]; then
            echo skipping non-existent file: "$1"
            fail=1
        else
            echo Check "$1"
            if ! "$SCRIPT_DIR"/check_script.py -w "$1"; then
	        fail=1
            fi
        fi
    elif [ "$1" = "-P3" ]; then
        #Check a test file
        shift
        if [ ! -f "$1" ]; then
            echo skipping non-existent file: "$1"
            fail=1
        else
            echo Check "$1"
            if ! "$SCRIPT_DIR"/check_script.py -p3 "$1"; then
	        fail=1
            fi
        fi
    else
        if [ ! -f "$1" ]; then
            echo skipping non-existent file: "$1"
            fail=1
        else
            echo Check "$1"
            if ! "$SCRIPT_DIR"/check_script.py "$1"; then
	        fail=1
            fi
        fi
    fi
    shift
done

echo "See pylint.log report"
list=$(grep rated pylint.log | grep -v "rated at 10" || true)
if [ $fail -eq 1 ] || [ "$list" != "" ]; then
    echo Fail
    exit 1
fi
exit 0
