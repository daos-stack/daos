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

            match = re.search(r'^(\s*)Import\(.(.*).\)', line)
            if match:
                if not scons_header:
                    scons_header = True
                    self.write_header(outfile)
                variables = match.group(2).split()
                new_lineno += self.write_variables(outfile, match.group(1),
                                                   variables)

            if not scons_header:
                if re.search(r"^\"\"\"", line):
                    scons_header = True
                elif not re.search(r'^#', line):
                    scons_header = True
                if scons_header:
                    new_lineno += self.write_header(outfile)

    @staticmethod
    def write_variables(outfile, prefix, variables):
        """Add code to define fake variables for pylint"""
        newlines = 2
        outfile.write("# pylint: disable=invalid-name\n")

        if "PREREQS" in variables:
            newlines += 4
            outfile.write("%sfrom prereq_tools import PreReqComponent\n"
                          % prefix)
            outfile.write("%sscons_temp_env = DefaultEnvironment()\n" % prefix)
            outfile.write("%sscons_temp_opts = Variables()\n" % prefix)
            outfile.write("%sPREREQS = PreReqComponent(scons_temp_env, " \
                          "scons_temp_opts)\n" % prefix)
            variables.remove("PREREQS")
        for variable in variables:
            if "ENV" in variable:
                newlines += 1
                outfile.write("%s%s = DefaultEnvironment()\n" % (prefix,
                                                                 variable))
            if "OPTS" in variable:
                newlines += 1
                outfile.write("%s%s = Variables()\n" % (prefix, variable))
            if "PREFIX" in variable:
                newlines += 1
                outfile.write("%s%s = ''\n" % (prefix, variable))
            if "TARGETS" in variable:
                newlines += 1
                outfile.write("%s%s = ['fake']\n" % (prefix % variable))
        outfile.write("# pylint: enable=invalid-name\n")
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
                match = re.search(r":(\d+):", line)
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
    error_count = 0
    pylint = open("pylint.log", "a")
    with open("tmp.log", "r") as log:
        for line in log.readlines():
            if re.search("rated", line):
                sys.stdout.write(line)
            if re.search("^[WECR]:", line):
                sys.stdout.write(line[3:])
                pylint.write(line[3:])
                error_count += 1
            else:
                pylint.write(line)
    pylint.close()
    os.unlink("tmp.log")
    return error_count

def check_script(fname, *args, **kw):
    """Check a python script for errors"""
    tmp_fname = fname
    wrapper = None
    wrap = kw.get("wrap", False)
    if wrap:
        wrapper = WrapScript(fname)
        tmp_fname = "script"
        pylint_path = fname
    else:
        pylint_path = "{path}"

    rc_dir = os.path.dirname(os.path.realpath(__file__))

    cmd = "pylint %s --rcfile=%s/pylint.rc --reports=n " \
          "--msg-template '{C}: %s:{line}: pylint-{symbol}: {msg}' " \
          "-d star-args  -d wrong-import-order " \
          "-d unused-wildcard-import %s > tmp.log 2>&1"% \
          (" ".join(args), rc_dir, pylint_path, tmp_fname)

    if os.environ.get("DEBUG_CHECK_SCRIPT", 0):
        print cmd
    os.system(cmd)
    if wrap:
        wrapper.fix_log(fname)
    error_count = parse_report()
    print ""
    return error_count

def main():
    """Run the actual code in a function"""
    parser = argparse.ArgumentParser("Check a Python script for errors")
    parser.add_argument("fname", metavar='FILENAME', type=str, nargs='?',
                        default=None, help="Filename of script to check")
    parser.add_argument("-w", dest='wrap', action='store_true',
                        help='Wrap the SCons script before checking')
    parser.add_argument("-s", dest='self_check', action='store_true',
                        help='Perform a self check')

    args = parser.parse_args()

    error_count = 0

    if args.self_check:
        print "Checking SCons"
        error_count += check_script("SCons",
                                    "-d", "too-few-public-methods",
                                    "-d", "too-many-public-methods",
                                    "-d", "invalid-name",
                                    "-d", "unused-argument",
                                    "-d", "no-self-use")
        print "Checking prereq_tools"
        error_count += check_script("prereq_tools",
                                    "-d", "too-many-lines",
                                    "-d", "unused-argument")
        print "Checking build_info"
        error_count += check_script("build_info")
        print "Checking test/build_info validation.py"
        error_count += check_script("test/validate_build_info.py",
                                    "-d", "wrong-import-position")
        print "Checking check_script.py"
        error_count += check_script("check_script.py")

    if args.fname:
        error_count += check_script(args.fname, wrap=args.wrap)

    if error_count:
        sys.exit(1)

if __name__ == '__main__':
    main()
