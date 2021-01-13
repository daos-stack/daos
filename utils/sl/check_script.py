#!/usr/bin/env python
# Copyright (c) 2016-2021 Intel Corporation
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
"""Simple script wrapper for checking SCons files"""
from __future__ import print_function

import re
import os
import sys
import argparse
import subprocess
import tempfile
import errno
#pylint: disable=import-error
#pylint: disable=no-name-in-module
from distutils.spawn import find_executable
#pylint: enable=import-error
#pylint: enable=no-name-in-module

class WrapScript():
    """Create a wrapper for a scons file and maintain a line mapping"""

    def __init__(self, fname):
        old_lineno = 1
        new_lineno = 1

        self.line_map = {}

        outfile = open("script", "w")
        infile = open(fname, "r")

        scons_header = False

        for line in infile.readlines():
            outfile.write(line)
            self.line_map[new_lineno] = old_lineno
            old_lineno += 1
            new_lineno += 1

            match = re.search(r'^(\s*)Import\(.(.*).\)', line)
            if match:
                if not scons_header:
                    scons_header = True
                    self.write_header(outfile)
                variables = []
                for var in match.group(2).split():
                    newvar = var.strip("\",     '")
                    variables.append(newvar)
                new_lineno += self.write_variables(outfile, match.group(1),
                                                   variables)

            match = re.search(r'^(\s*)Export\(.(.*).\)', line)
            if not match:
                match = re.search(r'^(\s*).*exports=[.(.*).]', line)
            if match:
                if not scons_header:
                    scons_header = True
                    new_lineno += self.write_header(outfile)
                variables = []
                for var in match.group(2).split():
                    newvar = var.strip("\",     '")
                    variables.append(newvar)
                new_lineno += self.read_variables(outfile, match.group(1),
                                                  variables)

            if not scons_header:
                if re.search(r"^\"\"\"", line):
                    scons_header = True
                elif not re.search(r'^#', line):
                    scons_header = True
                if scons_header:
                    new_lineno += self.write_header(outfile)

    @staticmethod
    def read_variables(outfile, prefix, variables):
        """Add code to define fake variables for pylint"""
        newlines = 2
        outfile.write("# pylint: disable=invalid-name\n")
        for variable in variables:
            outfile.write("%sprint(\"%%s\" %% str(%s))\n" % (prefix, variable))
            newlines += 1
        outfile.write("# pylint: enable=invalid-name\n")
        return newlines

    @staticmethod
    def write_variables(outfile, prefix, variables):
        """Add code to define fake variables for pylint"""
        newlines = 2
        outfile.write("# pylint: disable=invalid-name\n")

        for variable in variables:
            if variable.upper() == 'PREREQS':
                newlines += 4
                outfile.write("%sfrom prereq_tools import PreReqComponent\n"
                              % prefix)
                outfile.write("%sscons_temp_env = DefaultEnvironment()\n"
                              % prefix)
                outfile.write("%sscons_temp_opts = Variables()\n" % prefix)
                outfile.write("%s%s = PreReqComponent(scons_temp_env, "
                              "scons_temp_opts)\n" % (prefix, variable))
                variables.remove(variable)
        for variable in variables:
            if "ENV" in variable.upper():
                newlines += 1
                outfile.write("%s%s = DefaultEnvironment()\n" % (prefix,
                                                                 variable))
            elif "OPTS" in variable.upper():
                newlines += 1
                outfile.write("%s%s = Variables()\n" % (prefix, variable))
            elif "PREFIX" in variable.upper():
                newlines += 1
                outfile.write("%s%s = ''\n" % (prefix, variable))
            elif "TARGETS" in variable.upper() or "TGTS" in variable.upper():
                newlines += 1
                outfile.write("%s%s = ['fake']\n" % (prefix, variable))
            else:
                newlines += 1
                outfile.write("%s%s = None\n" % (prefix, variable))

        outfile.write("# pylint: enable=invalid-name\n")
        return newlines

    @staticmethod
    def write_header(outfile):
        """write the header"""
        outfile.write("""# pylint: disable=wildcard-import
from __future__ import print_function
from SCons.Script import *
from SCons.Variables import *
# pylint: enable=wildcard-import\n""")
        return 5

    def fix_log(self, log_file, fname):
        """Get the line number"""
        os.unlink("script")
        log_file.seek(0)
        try:
            output = tempfile.TemporaryFile(mode='w+', encoding='utf-8')
        except TypeError:
            output = tempfile.TemporaryFile()
        for line in log_file.readlines():
            match = re.search(r":(\d+):", line)
            if match:
                lineno = int(match.group(1))
                if int(lineno) in self.line_map.keys():
                    line = line.replace(str(lineno),
                                        str(self.line_map[lineno]),
                                        1)
            match = re.search("^(.*)Module script(.*)$", line)
            if match:
                line = "%sModule %s%s\n" % (match.group(1),
                                            fname,
                                            match.group(2))
            output.write(line)
        return output

def parse_report(log_file):
    """Create the report"""
    log_file.seek(0)
    with open("pylint.log", "a") as pylint:
        for line in log_file.readlines():
            if re.search("rated", line):
                sys.stdout.write(line)
            elif re.search("^[WECR]:", line):
                sys.stdout.write(line[3:])
                pylint.write(line[3:])
            else:
                sys.stdout.write(line)

def create_rc(src_name):
    """Create a temporary rc file with python path set"""
    root = os.path.dirname(os.path.realpath(__file__))
    src_path = os.path.join(root, src_name)
    name = os.path.join(root, "tmp_%s" % src_name)
    with open(name, "w") as tmp:
        tmp.write("[MASTER]\n")
        tmp.write("init-hook='import sys; ")
        tmp.write("sys.path.insert(0, \"%s\"); " % root)
        tmp.write("sys.path.insert(0, \"%s/fake_scons\")'\n" % root)
        with open(src_path, "r") as src:
            for line in src.readlines():
                tmp.write(line)

    return name

#pylint: disable=too-many-branches
def check_script(fname, *args, **kw):
    """Check a python script for errors"""
    tmp_fname = fname
    print('Checking {}'.format(fname))
    wrapper = None
    wrap = kw.get("wrap", False)
    if wrap:
        wrapper = WrapScript(fname)
        tmp_fname = "script"
        pylint_path = fname
    else:
        pylint_path = "{path}"

    # Python 2 checking is no longer supported
    pycmd = find_executable("pylint-3")
    rc_file = "tmp_pylint3.rc"
    if pycmd is None:
        print("Required pylint isn't installed on this machine")
        return 0

    rc_dir = os.path.dirname(os.path.realpath(__file__))

    cmd = pycmd.split() + \
          list(args) + \
          ["--rcfile=%s/%s" % (rc_dir, rc_file),
           "--msg-template",
           "{C}: %s:{line}: pylint-{symbol}: {msg}" % pylint_path,
           tmp_fname]

    if os.environ.get("DEBUG_CHECK_SCRIPT", 0):
        print(" ".join(cmd))

    try:
        log_file = tempfile.TemporaryFile(mode='w+', encoding='utf-8')
    except TypeError:
        log_file = tempfile.TemporaryFile()

    try:
        subprocess.check_call(cmd, stdout=log_file)
    except OSError as exception:
        if exception.errno == errno.ENOENT:
            print("pylint could not be found")
            return 1
        raise
    except subprocess.CalledProcessError:
        pass

    if wrap:
        log_file = wrapper.fix_log(log_file, fname)
    parse_report(log_file)
#pylint: enable=too-many-branches

def main():
    """Run the actual code in a function"""
    parser = argparse.ArgumentParser("Check a Python script for errors")
    parser.add_argument("fname", metavar='FILENAME', type=str, nargs='*',
                        default=None, help="Filename of script to check")
    parser.add_argument("-w", dest='wrap', action='store_true',
                        help='Wrap the SCons script before checking')
    parser.add_argument('-x', dest='exclude', help='Path to exclude')
    parser.add_argument("-s", dest='self_check', action='store_true',
                        help='Perform a self check')

    args = parser.parse_args()

    pylint3_rc = create_rc("pylint3.rc")

    if args.self_check:
        check_script("SCons",
                     "-d", "too-few-public-methods",
                     "-d", "too-many-public-methods",
                     "-d", "invalid-name",
                     "-d", "unused-argument",
                     "-d", "no-self-use")
        check_script("prereq_tools",
                     "-d", "too-many-lines",
                     "-d", "unused-argument")
        check_script("components")
        check_script("build_info")
        check_script("check_script")

    if args.fname:
        for fname in args.fname:
            if args.exclude and fname.startswith(args.exclude):
                continue
            check_script(fname, wrap=args.wrap)

    os.unlink(pylint3_rc)

if __name__ == '__main__':
    main()
