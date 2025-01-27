#!/usr/bin/env python3
"""
(C) Copyright 2018-2022 Intel Corporation.
(C) Copyright 2025 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent

Code to handle clang-format when used in the build.

This is used by scons to reformat automatically generated header files to be readable, but also
outside of scons by the clang-format commit hook to check the version.
"""
import os
import re
import subprocess  # nosec
import sys

from SCons.Builder import Builder
from SCons.Script import WhereIs

# Minimum version of clang-format that we use the configuration file for.  With clang-format
# versions older than this it's still used, but without loading our config.
MIN_FORMAT_VERSION = 12


def errprint(*args, **kwargs):
    """Print message on stderr.
    """
    print(*args, file=sys.stderr, **kwargs)


def _get_version_string():
    clang_exe = WhereIs('clang-format')
    if clang_exe is None:
        return None
    try:
        rawbytes = subprocess.check_output([clang_exe, "-version"])
        match = re.search(r"version ((\d+)(\.\d+)+)", rawbytes.decode('utf-8'))
        return match.group(1)
    except subprocess.CalledProcessError:
        return None


def _supports_custom_format(version):
    """Checks if the version of clang-format is new enough.

    Older versions complain about some of the options used so enforce a minimum version.
    """
    if version is None:
        errprint("Unsupported clang-format for custom style.  Using Mozilla style.")
        return False

    match = re.search(r"(\d+)\.", version)
    if match and int(match.group(1)) >= MIN_FORMAT_VERSION:
        return True

    errprint(f'Custom .clang-format wants version {MIN_FORMAT_VERSION}+. Using Mozilla style.')
    return False


def _supports_correct_style(version):
    """Checks if the version of clang-format is 14.0.5 or newer.

    Older versions contain bugs so will generate incorrectly formatted code on occasion.
    """

    match = re.search(r'([\d+\.]+)', version)
    if match:
        parts = match.group(1).split('.')
        if int(parts[0]) != 14:
            return int(parts[0]) > 14
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
    if _supports_custom_format(_get_version_string()):
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


def main():
    """Check for a supported version of clang-format"""
    version = _get_version_string()
    if (version is None) or (not _supports_correct_style(version)):
        errprint('Install clang-format version 14.0.5 or newer to reformat code')
        sys.exit(1)
    print(f"Clang-format version {version} installed")
    sys.exit(0)


def generate(env):
    """Setup the our custom tools"""
    indent = _find_indent()

    # pylint: disable-next=unused-argument
    def _pp_gen(source, target, env, for_signature):
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
    preprocess = Builder(generator=_pp_gen, emitter=_preprocess_emitter)

    if not env["BUILDERS"].get("Preprocess", False):
        env.Append(BUILDERS={"Preprocess": preprocess})


def exists(_env):
    """assert existence of tool"""
    return True


if __name__ == '__main__':
    main()
