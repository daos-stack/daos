"""init some daos specific functions functions"""
import re
import SCons
from SCons.Scanner import FindPathDirs
from SCons.Scanner import Scanner
from SCons.Script import SourceFileScanner

include_re = re.compile(r'^#include[ \t].(\S+).$', re.M)

def c_scan(node, _env, path):
    """For whatever reason the default scanner isn't working in some cases."""
    contents = node.get_text_contents()
    includes = include_re.findall(contents)
    results = []
    for include in includes:
        result = SCons.Node.FS.find_file(include, path)
        if result is None:
            continue
        results.append(result)
    return results


cscan = Scanner(c_scan, path_function=FindPathDirs('CPPPATH'))

SourceFileScanner.add_scanner('.cpp', cscan)
SourceFileScanner.add_scanner('.c', cscan)
SourceFileScanner.add_scanner('.h', cscan)
