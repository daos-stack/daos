#!/usr/bin/env python3
"""Code to handle clang-format when used in the build.

This is used by scons to reformat automatically generated header files to be readable, but also
outside of scons by the clang-format commit hook to check the version.
"""

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

import subprocess  # nosec
import re
import os
import sys

from SCons.Builder import Builder
from SCons.Script import WhereIs

# Minimum version of clang-format that we use the configuration file for.  With clang-format
# versions older than this it's still used, but without loading our config.
MIN_FORMAT_VERSION = 12


def _supports_custom_format(clang_exe):
    """Checks if the version of clang-format is new enough to parse the settings used by
    the config file"""

    try:
        rawbytes = subprocess.check_output([clang_exe, "-version"])
        output = rawbytes.decode('utf-8')
    except subprocess.CalledProcessError:
        print("Unsupported clang-format for custom style.  Using Mozilla style.")
        return False

    match = re.search(r"version (\d+)\.", output)
    if match and int(match.group(1)) >= MIN_FORMAT_VERSION:
        return True

    print(f'Custom .clang-format wants version {MIN_FORMAT_VERSION}+. Using Mozilla style.')
    return False


def _supports_correct_style(clang_exe):
    """Checks if the version of clang-format is 14.0.5 or newer which is required to correctly
    reformat code for landing"""

    try:
        rawbytes = subprocess.check_output([clang_exe, "-version"])
        output = rawbytes.decode('utf-8')
    except subprocess.CalledProcessError:
        return False

    match = re.search(r'version ([\d+\.]+) ', output)
    if match:
        parts = match.group(1).split('.')
        if int(parts[0]) < 14:
            return False
        if int(parts[1]) > 0:
            return True
        if int(parts[2]) < 5:
            return False
        return True

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
    return f'{indent} --style={style}'


def _preprocess_emitter(source, target, env):
    """generate target list for preprocessor builder"""
    target = []
    for src in source:
        dirname = os.path.dirname(src.abspath)
        basename = os.path.basename(src.abspath)
        (base, ext) = os.path.splitext(basename)
        prefix = ""
        for var in ["OBJPREFIX", "OBJSUFFIX", "SHOBJPREFIX", "SHOBJSUFFIX"]:
            mod = env.subst(f'${var}')
            if var == "OBJSUFFIX" and mod == ".o":
                continue
            if var == "SHOBJSUFFIX" and mod == ".os":
                continue
            if mod != "":
                prefix = prefix + "_" + mod
        newtarget = os.path.join(dirname, f'{prefix}{base}_pp{ext}')
        target.append(newtarget)
    return target, source


def _ch_emitter(source, target, **_kw):
    """generate target list for check header builder"""
    target = []
    for src in source:
        (base, _ext) = os.path.splitext(src.abspath)
        target.append(f"{base}_check_header$OBJSUFFIX")
    return target, source


def main():
    """Check for a supported version of clang-format"""
    supported = _supports_correct_style(WhereIs('clang-format'))
    if not supported:
        print('Install clang-format version 14.0.5 or newer to reformat code')
        sys.exit(1)
    sys.exit(0)


def generate(env):
    """Setup the our custom tools"""

    indent = _find_indent()

    # pylint: disable-next=unused-argument
    def __pp_gen(source, target, env, for_signature):
        """generate commands for preprocessor builder"""
        action = []
        cccom = env.subst("$CCCOM").replace(" -o ", " ")
        for src, tgt in zip(source, target):
            if indent:
                action.append(f'{cccom} -E -P {src} | {indent} > {tgt}')
            else:
                action.append(f'{cccom} -E -P {src} > {tgt}')
        return action

    # Only handle C for now
    preprocess = Builder(generator=__pp_gen, emitter=_preprocess_emitter)
    # Workaround for SCons issue #2757.   Avoid using Configure for internal headers
    check_header = Builder(action='$CCCOM', emitter=_ch_emitter)

    if not env["BUILDERS"].get("Preprocess", False):
        env.Append(BUILDERS={"Preprocess": preprocess})

    if not env["BUILDERS"].get("CheckHeader", False):
        env.Append(BUILDERS={"CheckHeader": check_header})


def exists(_env):
    """assert existence of tool"""
    return True


if __name__ == '__main__':
    main()
