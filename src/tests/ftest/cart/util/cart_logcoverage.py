# (C) Copyright 2023 Intel Corporation
#
# SPDX-License-Identifier: BSD-2-Clause-Patent

"""Mark code coverage from daos logs"""

import os


class CoverageTracer():
    """Save what lines are executed"""

    def __init__(self):
        self._data = {}
        self._files = {}

    def report(self):
        """Save a report to file"""
        print('Logs are:')
        for fname in self._files:
            print(fname)
        if not self._files:
            return
        with open('nlt-coverage.xml', 'w') as fd:
            self._save(fd)

    def _save(self, fd):

        fd.write("<?xml version='1.0' encoding='UTF-8'?>\n")
        fd.write('<!DOCTYPE coverage SYSTEM ')
        fd.write("'http://cobertura.sourceforge.net/xml/coverage-04.dtd'>\n")
        fd.write('<coverage line-rate="0.8571428571428571" branch-rate="0.5" lines-covered="6" ')
        fd.write('lines-valid="7" branches-covered="1" branches-valid="2" complexity="0.0" ')
        fd.write('timestamp="1678315055" version="gcovr 6.0">\n')
        fd.write("""<sources>
<source>.</source>
</sources>
<packages>\n""")
        fd.write('<package name="NLT Log coverage" ')
        fd.write('line-rate="0.8571428571428571" branch-rate="0.5" complexity="0.0">\n')
        fd.write('<classes>\n')

        for (fname, data) in self._files.items():
            shortname = os.path.basename(fname)
            fd.write(
                f' <class name = "{shortname}" filename = "{fname}" ')
            fd.write('line-rate = "0.8571428571428571" branch-rate="0.5" complexity="0.0">\n')
            fd.write('  <methods/>\n')
            fd.write('  <lines>\n')
            for lineno in data:
                fd.write(f'   <line number = "{lineno}" hits="1" branch="false"/>\n')
            fd.write(' </lines>\n')
            fd.write(' </class>\n')
        fd.write("""</classes>
</package>
</packages>
</coverage>\n""")

    def add_line(self, line):
        """Register a line"""
        try:
            fname = line.filename
            if fname not in self._files:
                self._files[fname] = {}
            lineno = line.lineno
            self._files[fname][lineno] = True
        except AttributeError:
            pass


def new():
    """Return a new iterator"""
    return CoverageTracer()
