# (C) Copyright 2023 Intel Corporation
#
# SPDX-License-Identifier: BSD-2-Clause-Patent

"""Mark code coverage from daos logs

Registered as a callback for all log tracing but saves results across the entire run.
"""

import os


class CodeLoc():
    """Logging data for single code location"""

    # pylint: disable=too-few-public-methods
    def __init__(self, line):
        self.lineno = line.lineno
        self.count = 0
        self.allocation = (line.is_calloc() or line.is_realloc())
        self.fault_injected = False
        self.no_fault = False
        self.add(line)

    def add(self, line):
        """Record an extra logging instance for this location"""
        self.count += 1
        if line.is_fi_site():
            self.fault_injected = True
        else:
            self.no_fault = True

    def is_fi_location(self):
        """Return true if a possible fault injection site"""
        return (self.allocation or self.fault_injected)

    def counts(self):
        """Return a tuple of (count, possible-count)"""
        if not self.is_fi_location():
            return (1, 1)
        count = 0
        if self.no_fault:
            count += 1
        if self.fault_injected:
            count += 1
        return (count, 2)

    def xml_str(self):
        """Return a xml string for this line"""
        if not self.is_fi_location():
            return f'   <line number="{self.lineno}" hits="{self.count}" branch="false"/>\n'

        condc = 'condition-coverage="50% (1/2)"'

        taken = '0%'
        not_taken = '0%'
        if self.no_fault:
            taken = '100%'
        if self.fault_injected:
            not_taken = '100%'
            if self.no_fault:
                condc = 'condition-coverage="100% (2/2)"'

        return f"""   <line number="{self.lineno}" hits="{self.count}" branch="true" {condc}>
      <conditions>
        <condition number="0" type="jump" coverage="{taken}"/>
        <condition number="1" type="jump" coverage="{not_taken}"/>
      </conditions>
    </line>
"""


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
        fd.write('<coverage version="daos 0.1">\n')
        fd.write("""<sources>
<source>.</source>
</sources>
<packages>\n""")

        for (dname, bname) in self._files.items():
            fd.write(f'<package name="{dname}">\n')
            fd.write('<classes>\n')

            for data in bname:
                taken = 0
                possible = 0
                xml = ''
                for loc in bname[data].values():
                    print(bname)
                    print(data)
                    print(loc)
                    (ttt, ptt) = loc.counts()
                    taken += ttt
                    possible += ptt
                    xml += loc.xml_str()
                rate = taken / possible
                fd.write(
                    f' <class name="{data}" filename="{dname}/{data}" branch-rate="{rate:.2f}">\n')
                fd.write('  <methods/>\n')
                fd.write('  <lines>\n')
                fd.write(xml)
                fd.write('  </lines>\n')
                fd.write(' </class>\n')
            fd.write('</classes></package>\n')
        fd.write('</packages>\n')
        fd.write('</coverage>\n')

    def add_line(self, line):
        """Register a line"""
        try:
            fname = line.filename
        except AttributeError:
            return

        dname = os.path.dirname(fname)
        bname = os.path.basename(fname)
        if dname not in self._files:
            self._files[dname] = {}
        if bname not in self._files[dname]:
            self._files[dname][bname] = {}
        lineno = line.lineno
        if lineno in self._files[dname][bname]:
            self._files[dname][bname][lineno].add(line)
        else:
            self._files[dname][bname][lineno] = CodeLoc(line)
