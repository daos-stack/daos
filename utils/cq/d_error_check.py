#!/usr/bin/env python

"""Re-write code to preferred style"""

import argparse
import sys
import io
import os

ARGS = None


class FileLine():
    """One line from a file"""

    def __init__(self, file_object, line, lineno):
        self._fo = file_object
        self._lineno = lineno

        # Text as it appears in the source
        self._line = line
        # Striped text
        self._code = line.strip()
        self.modified = False
        self.corrected = False

    def startswith(self, string):
        """Starts-with method"""
        return self._code.startswith(string)

    def endswith(self, string):
        """Ends-with method"""
        return self._code.endswith(string)

    def __contains__(self, string):
        return string in self._code

    def __str__(self):
        if self.modified or self.corrected:
            return f'{self._code}\n'
        return self._line

    def expand(self):
        """Expand line to end"""
        while not self._code.endswith(';'):
            to_add = str(next(self._fo))
            self._code += ' '
            self._code += to_add.strip()
            self._line += to_add

    def write(self, flo):
        """Write line to file"""
        flo.write(str(self))

    def correct(self, new_code):
        """Apply corrections to a line.

        These will only be applied if fixes are also being applied.
        """
        self._code = new_code
        self.corrected = True
        self.note('Optional fixes')

    def fix(self, new_code):
        """Mark a line as updated"""
        self.correct(new_code)
        self.modified = True

    def raw(self):
        """Returns the code"""
        return self._code

    def warning(self, msg):
        """Show a warning"""
        print(f'{self._fo.fname}:{self._lineno} {msg}')
        if ARGS.github:
            print(f'::warning file={self._fo.fname},line={self._lineno},::newline-check, {msg}')

    def note(self, msg):
        """Show a note"""
        print(f'{self._fo.fname}:{self._lineno} {msg}')


class FileParser:
    """One source file"""

    def __init__(self, fname):
        self.fname = fname
        self.lines = []
        self._index = None
        with open(fname, 'r') as fd:
            lineno = 1
            for line in fd:
                self.lines.append(FileLine(self, line, lineno))
                lineno += 1

    def __iter__(self):
        self._index = -1
        return self

    def __next__(self):
        self._index += 1
        try:
            return self.lines[self._index]
        except IndexError as exc:
            raise StopIteration from exc


# Logging macros that expect a new-line.
PREFIXES = ['D_ERROR', 'D_WARN', 'D_INFO', 'D_NOTE', 'D_ALERT', 'D_CRIT', 'D_FATAT', 'D_EMIT',
            'D_TRACE_INFO', 'D_TRACE_NOTE', 'D_TRACE_WARN', 'D_TRACE_ERROR', 'D_TRACE_ALERT',
            'D_TRACE_CRIT', 'D_TRACE_FATAL', 'D_TRACE_EMIT', 'RPC_TRACE', 'RPC_ERROR']

# Logging macros that do not expect a new-line.
PREFIXES_NNL = ['DFUSE_LOG_WARNING', 'DFUSE_LOG_ERROR', 'DFUSE_LOG_DEBUG', 'DFUSE_LOG_INFO',
                'DFUSE_TRA_WARNING', 'DFUSE_TRA_ERROR', 'DFUSE_TRA_DEBUG', 'DFUSE_TRA_INFO']


PREFIXES_ALL = PREFIXES.copy()
PREFIXES_ALL.extend(PREFIXES_NNL)


class AllChecks():
    """All the checks in one class"""

    def __init__(self, file_object):
        self._fo = file_object
        self.line = ''
        self._output = io.StringIO()
        self.modified = False
        self.corrected = True

    def run_all_checks(self):
        """Run everything"""
        for line in self._fo:
            if not any(map(line.startswith, PREFIXES_ALL)):
                line.write(self._output)
                continue

            if line.endswith('\\'):
                line.note('Part of macro, not checking')
                line.write(self._output)
                continue

            line.expand()
            self.check_quote(line)
            self.check_return(line)
            line.write(self._output)
            if line.modified:
                self.modified = True
            if line.corrected:
                self.corrected = True

    def save(self, fname):
        """Save new file to file"""
        if not self.modified and not self.corrected:
            return
        with open(fname, 'w') as fd:
            fd.write(self._output.getvalue())

    def check_quote(self, line):
        """Check for double quotes in message"""
        if '""' not in line:
            return
        line.correct(line.raw().replace('""', ''))

    def check_return(self, line):
        """Check for one return character"""
        expected_newlines = 1
        code = line.raw()
        if any(map(code.startswith, PREFIXES_NNL)):
            expected_newlines = 0

        count = code.count('\\n')
        if count < expected_newlines:
            parts = code.split('"')
            if len(parts) == 3:
                new_line = f'{parts[0]}"{parts[1]}\\n"{parts[2]}'
                line.fix(new_line)
                line.warning("Line does not contain newline (autofixable)")
            else:
                line.warning("Line does not contain newline")
        elif count > expected_newlines:
            line.warning("Line contains too many newlines")


def one_entry(fname):
    """Process one path entry"""
    if not any(map(fname.endswith, ['.c', '.h'])):
        return

    if any(map(fname.endswith, ['pb-c.c', 'pb-c..h'])):
        return

    filep = FileParser(fname)

    checks = AllChecks(filep)

    checks.run_all_checks()

    if (ARGS.fix and checks.modified) or (ARGS.correct and checks.corrected):
        print(f'Saving updates to {fname}')
        checks.save(fname)


def main():
    """Do something"""
    parser = argparse.ArgumentParser(description='Verify DAOS logging in source tree')
    parser.add_argument('--fix', action='store_true', help='Apply required fixes')
    parser.add_argument('--correct', action='store_true', help='Apply optional fixes')
    parser.add_argument('--github', action='store_true')
    parser.add_argument('files', nargs='*')

    global ARGS

    ARGS = parser.parse_args()

    for fname in ARGS.files:
        if os.path.isfile(fname):
            one_entry(fname)
        else:
            for root, dirs, files in os.walk(fname):
                for name in files:
                    one_entry(os.path.join(root, name))
                if '.git' in dirs:
                    dirs.remove('.git')
                if root == 'src/control' and 'vendor' in dirs:
                    dirs.remove('vendor')


def old_main():
    """Old code"""
    replace = True

    output = io.StringIO()

    fname = sys.argv[1]

    with open(fname, 'r') as fd:
        for line in fd:
            raw = line.strip()
            if not raw.startswith('D_ERROR'):
                output.write(line)

                continue

            err_cmd = raw
            while not raw.endswith(';'):
                raw = next(fd).strip()
                err_cmd += raw
            err_cmd = err_cmd.replace('""', '')
            if 'DF_RC' in err_cmd:
                err_cmd = err_cmd.replace('DF_RC ', 'DF_RC')
                if 'DF_RC"\\n"' in err_cmd:
                    err_cmd = err_cmd.replace(' DP_RC', 'DP_RC')
                    parts = err_cmd[:-3].split('(')
                    assert parts[-2].endswith('DP_RC')
                    df_name = parts.pop()
                    last_part = parts.pop()
                    assert last_part.endswith(',DP_RC')
                    parts.append(last_part[:-6])
                    new_err_cmd = '('.join(parts)
                    new_err_cmd = new_err_cmd.replace('DF_RC"\\n"', '')
                    new_err_cmd = new_err_cmd.rstrip()
                    if new_err_cmd.endswith('"'):
                        new_err_cmd = new_err_cmd[:-1]  # Strip off "
                        if new_err_cmd.endswith(' '):
                            new_err_cmd = new_err_cmd[:-1]
                        if new_err_cmd.endswith(':'):
                            new_err_cmd = new_err_cmd[:-1]
                        if new_err_cmd.endswith(df_name):
                            new_err_cmd = new_err_cmd[:-len(df_name)]
                        if new_err_cmd.endswith(' '):
                            new_err_cmd = new_err_cmd[:-1]
                        if new_err_cmd.endswith(','):
                            new_err_cmd = new_err_cmd[:-1]
                        new_err_cmd += '"'
                    err_cmd = f'DL_ERROR({df_name}, {new_err_cmd[8:]});'

            output.write(f'{err_cmd}\n')

    if replace:
        with open(fname, 'w') as fd:
            fd.write(output.getvalue())
    else:
        print(output.getvalue())


main()
