#!/usr/bin/env python3
# Copyright 2016-2022 Intel Corporation
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
import subprocess  # nosec
import tempfile
from distutils.spawn import find_executable


class WrapScript():
    """Create a wrapper for a scons file and maintain a line mapping"""

    def __init__(self, fname, output='script'):

        self.line_map = {}
        self.wrap_file = output

        with open(self.wrap_file, 'w', encoding='utf-8') as outfile:
            with open(fname, 'r', encoding='utf-8') as infile:
                self._read_files(infile, outfile)

    def __del__(self):
        os.unlink(self.wrap_file)

    def _read_files(self, infile, outfile):
        old_lineno = 1
        new_lineno = 1
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
                    new_lineno += self.write_header(outfile)
                variables = []
                for var in match.group(2).split():
                    newvar = var.strip("\",     '")
                    variables.append(newvar)
                new_lineno += self.write_variables(outfile, match.group(1), variables)

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
                new_lineno += self.read_variables(outfile, match.group(1), variables)

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
        newlines = 0
        for variable in variables:
            outfile.write('# pylint: disable-next=invalid-name,consider-using-f-string\n')
            outfile.write(f"{prefix}print('%s' % str({variable}))\n")
            newlines += 2
        return newlines

    @staticmethod
    def write_variables(outfile, prefix, variables):
        """Add code to define fake variables for pylint"""
        newlines = 4
        outfile.write("# pylint: disable=invalid-name\n")
        outfile.write("# pylint: disable=import-outside-toplevel\n")

        for variable in variables:
            if variable.upper() == 'PREREQS':
                newlines += 4
                outfile.write("%sfrom prereq_tools import PreReqComponent\n" % prefix)
                outfile.write("%sscons_temp_env = DefaultEnvironment()\n" % prefix)
                outfile.write("%sscons_temp_opts = Variables()\n" % prefix)
                outfile.write("%s%s = PreReqComponent(scons_temp_env, "
                              "scons_temp_opts)\n" % (prefix, variable))
                variables.remove(variable)
        for variable in variables:
            if "ENV" in variable.upper():
                newlines += 1
                outfile.write("%s%s = DefaultEnvironment()\n" % (prefix, variable))
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
        outfile.write("# pylint: enable=import-outside-toplevel\n")
        return newlines

    @staticmethod
    def write_header(outfile):
        """write the header"""
        outfile.write("""# Autoinserted wrapper header
from SCons.Script import * # pylint: disable=import-outside-toplevel,wildcard-import
from SCons.Variables import * # pylint: disable=import-outside-toplevel,wildcard-import\n""")
        return 3

    def convert_line(self, line):
        """Convert from a line number in the report to a line number in the input file"""
        return self.line_map[line]

    def fix_log(self, log_file, fname):
        """Get the line number"""
        log_file.seek(0)
        output = tempfile.TemporaryFile(mode='w+', encoding='utf-8')
        for line in log_file.readlines():
            match = re.search(r":(\d+):", line)
            if match:
                lineno = int(match.group(1))
                if int(lineno) in self.line_map:
                    line = line.replace(str(lineno), str(self.line_map[lineno]), 1)
            match = re.search("^(.*)Module script(.*)$", line)
            if match:
                line = "%sModule %s%s\n" % (match.group(1), fname, match.group(2))
            output.write(line)
        return output


def parse_report(log_file):
    """Create the report"""
    log_file.seek(0)
    with open('pylint.log', 'a', encoding='utf-8') as pylint:
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
    with open(name, 'w', encoding='utf-8') as tmp:
        tmp.write("[MASTER]\n")
        tmp.write("init-hook='import sys; ")
        tmp.write("sys.path.insert(0, \"%s/fake_scons\"); " % root)
        tmp.write("sys.path.insert(0, \"%s/../../site_scons\")'\n" % root)
        with open(src_path, 'r', encoding='utf-8') as src:
            for line in src.readlines():
                tmp.write(line)

    return name


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
    pycmd = find_executable('pylint')
    if pycmd is None:
        pycmd = find_executable('pylint-3')
        if pycmd is None:
            print("Required pylint isn't installed on this machine")
            return

    rc_file = "tmp_pylint3.rc"
    rc_dir = os.path.dirname(os.path.realpath(__file__))

    cmd = pycmd.split() + list(args) + \
        ["--rcfile=%s/%s" % (rc_dir, rc_file),
         "--unsafe-load-any-extension=y",
         "--msg-template",
         "{C}: %s:{line}: pylint-{symbol}: {msg}" % pylint_path, tmp_fname]

    log_file = tempfile.TemporaryFile(mode='w+', encoding='utf-8')

    try:
        subprocess.check_call(cmd, stdout=log_file)
    except subprocess.CalledProcessError:
        pass

    if wrap:
        log_file = wrapper.fix_log(log_file, fname)
    parse_report(log_file)


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

    if args.fname:
        for fname in args.fname:
            if args.exclude and fname.startswith(args.exclude):
                continue
            if not os.path.exists(fname):
                continue
            check_script(fname, wrap=args.wrap)

    os.unlink(pylint3_rc)


if __name__ == '__main__':
    main()
