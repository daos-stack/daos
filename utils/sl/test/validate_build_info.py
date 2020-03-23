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
"""A simple script to exercise the BuildInfo module"""
from __future__ import print_function
import sys
import os


SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, os.path.dirname(SCRIPT_DIR))
from build_info import BuildInfo

FILENAME = os.path.join(SCRIPT_DIR, "sl_test.info")

INFO = BuildInfo(FILENAME)

PREFIX = INFO.get("PREFIX")
if not os.path.exists(PREFIX):
    print("PREFIX doesn't exist")
    os.unlink(FILENAME)
    sys.exit(-1)

if SCRIPT_DIR not in PREFIX:
    print("PREFIX not at expected location")
    os.unlink(FILENAME)
    sys.exit(-1)

SH_SCRIPT = os.path.join(SCRIPT_DIR, "sl_test.sh")
INFO.gen_script(SH_SCRIPT)
os.system("source %s" % SH_SCRIPT)
os.unlink(SH_SCRIPT)

SH_JSON = os.path.join(SCRIPT_DIR, "sl_test.json")
INFO.save(SH_JSON)

os.unlink(FILENAME)
