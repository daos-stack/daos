#!/usr/bin/env python
# Copyright (c) 2018 Intel Corporation

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
"""SCons extra features"""

import os

# pylint: disable=no-name-in-module
# pylint: disable=import-error
from distutils.spawn import find_executable
from SCons.Builder import Builder
from SCons.Errors import StopError
from SCons.Script import Dir
# pylint: enable=no-name-in-module
# pylint: enable=import-error

# pylint: disable=unused-argument
def preprocess_generator(source, target, env, for_signature):
    """generate commands for preprocessor builder"""
    action = []
    style = "Mozilla" # fallback
    root = Dir("#").abspath
    while root != "/":
        if os.path.exists(os.path.join(root, ".clang-format")):
            style = "file"
        root = os.path.dirname(root)
    nenv = env.Clone()
    cccom = nenv.subst("$CCCOM").replace(" -o ", " ")
    for src, tgt in zip(source, target):
        action.append("%s -E -P %s > %s_raw" % (cccom, src, tgt))
        action.append("clang-format --style=%s %s_raw > %s" % (style, tgt, tgt))
    return action

def preprocess_emitter(source, target, env):
    """generate target list for preprocessor builder"""
    target = []
    for src in source:
        basename = os.path.basename(src.abspath)
        (base, _ext) = os.path.splitext(basename)
        target.append(base + "_pp.c")
    return target, source
# pylint: enable=unused-argument

def generate(env):
    """Setup the our custom tools"""
    #Only handle C for now
    if find_executable("clang-format") is None:
        raise StopError("clang-format must be installed for Preprocess builder")

    preprocess = Builder(generator=preprocess_generator, suffix="_pp.c",
                         emitter=preprocess_emitter, src_suffix=".c")

    env.Append(BUILDERS={"Preprocess":preprocess})

def exists(_env):
    """assert existence of tool"""
    return True
