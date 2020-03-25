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
"""SConstruct to build all components"""

import sys
import os
sys.path.insert(0, os.path.realpath("."))
from prereq_tools import PreReqComponent

def scons():
    """Build requested prerequisite components"""
    env = DefaultEnvironment()

    opts = Variables()
    reqs = PreReqComponent(env, opts)
    reqs.load_definitions()

    opts.Add(ListVariable("REQUIRES",
                          "List of libraries to build",
                          'mercury',
                          reqs.get_defined_components()))
    opts.Update(env)

    reqs.require(env, *env.get("REQUIRES"))

    build_info = reqs.get_build_info()

    build_info.gen_script(".build_vars.sh")

    try:
        Help(opts.GenerateHelpText(env), append=True)
    except TypeError:
        Help(opts.GenerateHelpText(env))

if __name__ == 'SCons.Script':
    scons()
