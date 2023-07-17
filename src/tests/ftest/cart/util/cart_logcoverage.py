# (C) Copyright 2023 Intel Corporation
#
# SPDX-License-Identifier: BSD-2-Clause-Patent

"""Mark code coverage from daos logs

Registered as a callback for all log tracing but saves results across the entire run.
"""

import os


class CoverageTracer():
    """Save what lines are executed"""

    def __init__(self, output_file):
        self._data = {}
        self._files = {}
        self.outfile = output_file

        # Unlike the xml test results do not keep this up-to-date on a per log basis but rather
        # remove the old file on start and replace it on close.  This keeps down I/O and the
        # coverage data wouldn't be useful on failure anyway.
        try:
            os.unlink(self.outfile)
        except FileNotFoundError:
            pass

    def report(self):
        """Report per log"""
        return

    def report_all(self):
        """Report on everything"""
        if not self._files:
            return
        with open(self.outfile, 'w') as fd:
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
                fd.write(f'   <line number = "{lineno}" hits="{data[lineno]}" branch="false"/>\n')
            fd.write(' </lines>\n')
            fd.write(' </class>\n')
        fd.write("""</classes>
</package>
</packages>
</coverage>\n""")

    def add_line(self, line):
        """Register a line"""
        fname = line.filename
        if fname not in self._files:
            self._files[fname] = {}
        lineno = line.lineno
        try:
            self._files[fname][lineno] += 1
        except KeyError:
            self._files[fname][lineno] = 1
