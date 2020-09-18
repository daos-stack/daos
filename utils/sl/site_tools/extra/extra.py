#!/usr/bin/env python
# Copyright (c) 2018-2019 Intel Corporation

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
from __future__ import print_function

import os

# pylint: disable=no-name-in-module
# pylint: disable=import-error
from distutils.spawn import find_executable
from SCons.Builder import Builder
from SCons.Script import Dir
# pylint: enable=no-name-in-module
# pylint: enable=import-error

def supports_custom_format(clang_exe):
    """Get the version of clang-format"""
    import subprocess
    import re

    try:
        rawbytes = subprocess.check_output([clang_exe, "-version"])
        output = rawbytes.decode('utf-8')
    except subprocess.CalledProcessError:
        print("Unsupported clang-format for custom style.  "
              "Using Mozilla style.")
        return False

    match = re.search(r"version (\d+)\.", output)
    if match and int(match.group(1)) >= 7:
        return True

    print("Custom .clang-format wants version 7+. Using Mozilla style.")
    return False

def find_indent():
    """find clang-format"""
    indent = find_executable("clang-format")
    if indent is not None:
        style = "Mozilla" # fallback
        root = Dir("#").abspath
        while root != "/":
            if os.path.exists(os.path.join(root, ".clang-format")):
                if not supports_custom_format(indent):
                    break
                style = "file"
            root = os.path.dirname(root)
        return "%s --style=%s" % (indent, style)

    return "cat"

def pp_gen(source, target, env, indent):
    """generate commands for preprocessor builder"""
    action = []
    nenv = env.Clone()
    cccom = nenv.subst("$CCCOM").replace(" -o ", " ")
    for src, tgt in zip(source, target):
        action.append("%s -E -P %s | %s > %s" % (cccom, src, indent, tgt))
    return action

def preprocess_emitter(source, target, env):
    """generate target list for preprocessor builder"""
    target = []
    for src in source:
        basename = os.path.basename(src.abspath)
        (base, _ext) = os.path.splitext(basename)
        prefix = ""
        for var in ["OBJPREFIX", "OBJSUFFIX", "SHOBJPREFIX", "SHOBJSUFFIX"]:
            mod = env.subst("$%s" % var)
            if var == "OBJSUFFIX" and mod == ".o":
                continue
            if var == "SHOBJSUFFIX" and mod == ".os":
                continue
            if mod != "":
                prefix = prefix + "_" + mod
        target.append(prefix + base + "_pp.c")
    return target, source

def generate(env):
    """Setup the our custom tools"""

    indent = find_indent()

    generator = lambda source, target, env, for_signature: pp_gen(source,
                                                                  target,
                                                                  env, indent)

    # Only handle C for now
    preprocess = Builder(generator=generator, suffix="_pp.c",
                         emitter=preprocess_emitter, src_suffix=".c")

    env.Append(BUILDERS={"Preprocess":preprocess})

def exists(_env):
    """assert existence of tool"""
    return True
