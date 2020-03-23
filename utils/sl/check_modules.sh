#!/bin/sh
# Copyright (c) 2016-2018 Intel Corporation
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

if [ $# -ne 0 ]; then
  ./check_python.sh "$@"
  exit $?
fi

./check_python.sh -c -s "SConstruct" \
                  -s "test/SConstruct.utest" \
                  -s "test/SConstruct" \
                  -s "test/sl_test/SConscript" \
                  -s "test/utest/SConscript" \
                  -s "utils/SConstruct_info"\
                  -s "utils/docker/SConstruct_info"\
                  -s "test/tool/SConstruct"\
                  -s "test/tool/src/SConscript"\
                  "env_modules.py" \
                  site_tools/extra/extra.py

exit $?
