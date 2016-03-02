#!/usr/bin/env python
"""Simple script wrapper for checking SCons files"""
import sys, re

if len(sys.argv) != 2:
    print "Usage: %s <SCons file>"%sys.argv[0]
    sys.exit(-1)

OUTFILE = open("script", "w")
INFILE = open(sys.argv[1], "r")

SCONS_HEADER = False

def write_header(outfile):
    """write the header"""
    outfile.write("""
# pylint: disable=wildcard-import
from SCons.Script import *
from SCons.Variables import *
# pylint: enable=wildcard-import\n""")

for line in INFILE.readlines():
    OUTFILE.write(line)
    match = re.search(r'Import\(.(.*).\)', line)
    if match:
        if not SCONS_HEADER:
            SCONS_HEADER = True
            write_header(OUTFILE)
        variables = match.group(1).split()
        if "PREREQS" in variables:
            OUTFILE.write("""
ENV = DefaultEnvironment()
OPTS = Variables()
from prereq_tools import PreReqComponent
PREREQS = PreReqComponent(ENV, OPTS)\n""")
            variables.remove("PREREQS")
            if "ENV" in variables:
                variables.remove("ENV")
            if "OPTS" in variables:
                variables.remove("OPTS")
        for variable in variables:
            if "ENV" in variable:
                OUTFILE.write("""%s = DefaultEnvironment()\n"""%variable)
            if "OPTS" in variable:
                OUTFILE.write("""%s = Variables()\n"""%variable)
            if "PREFIX" in variable:
                OUTFILE.write("""%s = ''\n"""%variable)
            if "TARGETS" in variable:
                OUTFILE.write("""%s = ['fake']\n"""%variable)

    if not SCONS_HEADER:
        if re.search(r"^\"\"\"", line):
            SCONS_HEADER = True
        elif not re.search(r'^#', line):
            SCONS_HEADER = True
        if SCONS_HEADER:
            write_header(OUTFILE)
