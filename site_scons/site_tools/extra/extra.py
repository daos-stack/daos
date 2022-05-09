#!/usr/bin/env python
# Copyright 2018-2022 Intel Corporation

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

import subprocess #nosec
import re
import os

from SCons.Builder import Builder
from SCons.Script import WhereIs

# Minimum version of clang-format that we use the configuration file for.  With clang-format
# versions older than this it's still used, but without loading our config.
MIN_FORMAT_VERSION = 12

def _supports_custom_format(clang_exe):
    """Get the version of clang-format"""

    try:
        rawbytes = subprocess.check_output([clang_exe, "-version"])
        output = rawbytes.decode('utf-8')
    except subprocess.CalledProcessError:
        print("Unsupported clang-format for custom style.  Using Mozilla style.")
        return False

    match = re.search(r"version (\d+)\.", output)
    if match and int(match.group(1)) >= MIN_FORMAT_VERSION:
        return True

    print('Custom .clang-format wants version {}+. Using Mozilla style.'.format(MIN_FORMAT_VERSION))
    return False

def _find_indent():
    """find clang-format"""
    indent = WhereIs("clang-format")
    if indent is None:
        return None
    if _supports_custom_format(indent):
        style = "file"
    else:
        style = "Mozilla"
    return "%s --style=%s" % (indent, style)

def _pp_gen(source, target, env, indent):
    """generate commands for preprocessor builder"""
    action = []
    nenv = env.Clone()
    cccom = nenv.subst("$CCCOM").replace(" -o ", " ")
    for src, tgt in zip(source, target):
        if indent:
            action.append("%s -E -P %s | %s > %s" % (cccom, src, indent, tgt))
        else:
            action.append("%s -E -P %s > %s" % (cccom, src, tgt))
    return action

def _preprocess_emitter(source, target, env):
    """generate target list for preprocessor builder"""
    target = []
    for src in source:
        dirname = os.path.dirname(src.abspath)
        basename = os.path.basename(src.abspath)
        (base, ext) = os.path.splitext(basename)
        prefix = ""
        for var in ["OBJPREFIX", "OBJSUFFIX", "SHOBJPREFIX", "SHOBJSUFFIX"]:
            mod = env.subst("$%s" % var)
            if var == "OBJSUFFIX" and mod == ".o":
                continue
            if var == "SHOBJSUFFIX" and mod == ".os":
                continue
            if mod != "":
                prefix = prefix + "_" + mod
            newtarget = os.path.join(dirname, '{}{}_pp.{}'.format(prefix, base, ext))
            print("newtarget = %s\n" % newtarget)
            target.append(newtarget)
    return target, source

def generate(env):
    """Setup the our custom tools"""

    indent = _find_indent()

    generator = lambda source, target, env, for_signature: _pp_gen(source, target, env, indent)

    # Only handle C for now
    preprocess = Builder(generator=generator, suffix="_pp.c",
                         emitter=_preprocess_emitter, src_suffix=".c")

    env.Append(BUILDERS={"Preprocess":preprocess})

def exists(_env):
    """assert existence of tool"""
    return True
