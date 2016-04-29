#!/usr/bin/env python
"""Simple script wrapper for checking SCons files"""
import re
import os
import sys
import argparse

class WrapScript(object):
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

            match = re.search(r'Import\(.(.*).\)', line)
            if match:
                if not scons_header:
                    scons_header = True
                    self.write_header(outfile)
                variables = match.group(1).split()
                new_lineno += self.write_variables(outfile, variables)

            if not scons_header:
                if re.search(r"^\"\"\"", line):
                    scons_header = True
                elif not re.search(r'^#', line):
                    scons_header = True
                if scons_header:
                    new_lineno += self.write_header(outfile)

    @staticmethod
    def write_variables(outfile, variables):
        """Add code to define fake variables for pylint"""
        newlines = 0
        if "PREREQS" in variables:
            newlines += 4
            outfile.write("""from prereq_tools import PreReqComponent
ENV = DefaultEnvironment()
OPTS = Variables()
PREREQS = PreReqComponent(ENV, OPTS)\n""")
            variables.remove("PREREQS")
            if "ENV" in variables:
                variables.remove("ENV")
            if "OPTS" in variables:
                variables.remove("OPTS")
        for variable in variables:
            if "ENV" in variable:
                newlines += 1
                outfile.write("%s = DefaultEnvironment()\n"%variable)
            if "OPTS" in variable:
                newlines += 1
                outfile.write("""%s = Variables()\n"""%variable)
            if "PREFIX" in variable:
                newlines += 1
                outfile.write("""%s = ''\n"""%variable)
            if "TARGETS" in variable:
                newlines += 1
                outfile.write("""%s = ['fake']\n"""%variable)
        return newlines

    @staticmethod
    def write_header(outfile):
        """write the header"""
        outfile.write("""# pylint: disable=wildcard-import
from SCons.Script import *
from SCons.Variables import *
# pylint: enable=wildcard-import\n""")
        return 4

    def fix_log(self, fname):
        """Get the line number"""
        os.unlink("script")
        output = open("tmp2.log", "w")
        with open("tmp.log", "r") as log:
            for line in log.readlines():
                match = re.search(r"^\w+: *(\d+),", line)
                if match:
                    lineno = int(match.group(1))
                    if int(lineno) in self.line_map.keys():
                        line = line.replace(str(lineno),
                                            str(self.line_map[lineno]),
                                            1)
                match = re.search("^(.*)Module script(.*)$", line)
                if match:
                    line = "%sModule %s%s\n"%(match.group(1),
                                              fname,
                                              match.group(2))
                output.write(line)
        output.close()
        os.rename("tmp2.log", "tmp.log")

def parse_report():
    """Create the report"""
    pylint = open("pylint.log", "a")
    with open("tmp.log", "r") as log:
        for line in log.readlines():
            if re.search("rated", line):
                sys.stdout.write(line)
            if re.search("^[WECR]:", line):
                sys.stdout.write(line)
            pylint.write(line)
    pylint.close()
    os.unlink("tmp.log")

def check_script(fname, *args, **kw):
    """Check a python script for errors"""
    tmp_fname = fname
    wrapper = None
    wrap = kw.get("wrap", False)
    if wrap:
        wrapper = WrapScript(fname)
        tmp_fname = "script"

    cmd = "pylint %s -d star-args -d wrong-import-order " \
          "-d unused-wildcard-import %s > tmp.log 2>&1"% \
          (" ".join(args), tmp_fname)
    if os.environ.get("DEBUG_CHECK_SCRIPT", 0):
        print cmd
    os.system(cmd)
    if wrap:
        wrapper.fix_log(fname)
    parse_report()
    print ""

PARSER = argparse.ArgumentParser("Check a Python script for errors")
PARSER.add_argument("fname", metavar='FILENAME', type=str, nargs='?',
                    default=None, help="Filename of script to check")
PARSER.add_argument("-w", dest='wrap', action='store_true',
                    help='Wrap the SCons script before checking')
PARSER.add_argument("-s", dest='self_check', action='store_true',
                    help='Perform a self check')

ARGS = PARSER.parse_args()

if ARGS.self_check:
    print "Checking SCons"
    check_script("SCons",
                 "-d", "too-few-public-methods",
                 "-d", "too-many-public-methods",
                 "-d", "invalid-name",
                 "-d", "unused-argument",
                 "-d", "no-self-use")
    print "Checking prereq_tools"
    check_script("prereq_tools",
                 "-d", "too-many-lines",
                 "-d", "unused-argument")
    print "Checking build_info"
    check_script("build_info")
    print "Checking test/build_info validation.py"
    check_script("test/validate_build_info.py",
                 "-d", "wrong-import-position")
    print "Checking check_script.py"
    check_script("check_script.py")

if ARGS.fname:
    check_script(ARGS.fname, wrap=ARGS.wrap)
