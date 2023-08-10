# (C) Copyright 2023 Intel Corporation
#
# SPDX-License-Identifier: BSD-2-Clause-Patent

"""Mark code usage from daos logs

Track what lines of code has caused logging and use this to generate xml 'coverage' data which
can be rendered in Jenkins and used to identify areas which are not exercised.

Registered as a callback for all log tracing but saves results across the entire run.
"""

import os
import json


class CodeLoc():
    """Logging data for single code location"""

    def __init__(self, line=None):
        self.lineno = 0
        self.count = 0
        self.fault_injected = False
        self.no_fault = False
        self.allocation = False
        if line:
            self.lineno = line.lineno
            self.allocation = (line.is_calloc() or line.is_realloc())
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


class UsageTracer():
    """Save what lines are executed"""

    def __init__(self):
        self._files = {}

    def report(self):
        """Report per log"""
        return

    def load(self, fname):
        """Load intermediate data from file"""
        with open(fname, 'r') as fd:
            idata = json.load(fd)

        data = {}
        for (key, value) in idata.items():
            data[key] = {}
            # Iterate over files.
            for (key2, value2) in value.items():
                data[key][key2] = {}
                # Iterate over line numbers.
                for (key3, value3) in value2.items():
                    new_obj = CodeLoc()
                    new_obj.lineno = key3
                    new_obj.count = value3[0]
                    new_obj.allocation = value3[1]
                    new_obj.fault_injected = value3[2]
                    new_obj.no_fault = value3[3]
                    new_obj.lineno = key3
                    data[key][key2][key3] = new_obj
        self._files = data

    def save(self, fname):
        """Save intermediate data to file"""
        data = {}
        # Iterate over directories.
        for (key, value) in self._files.items():
            data[key] = {}
            # Iterate over files.
            for (key2, value2) in value.items():
                data[key][key2] = {}
                # Iterate over line numbers.
                for (key3, value3) in value2.items():
                    data[key][key2][key3] = [value3.count, value3.allocation,
                                             value3.fault_injected, value3.no_fault]

        with open(fname, 'w') as fd:
            json.dump(data, fd)

    def report_all(self, fname):
        """Report on everything"""
        if not self._files:
            return
        with open(fname, 'w') as fd:
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
            # Patch up two areas of logging in the go code that mis-report source code names.
            if not dname.startswith('src'):
                if dname == '':
                    dname = 'src/control/cmd/daos'
                else:
                    parts = dname.split('/')
                    while parts[0] != 'src':
                        parts.pop(0)
                    dname = '/'.join(parts)

            fd.write(f'<package name="{dname}">\n')
            fd.write('<classes>\n')

            for data in bname:
                taken = 0
                possible = 0
                xml = ''
                for loc in bname[data].values():
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
            self._files[dname][bname][lineno] = CodeLoc(line=line)
