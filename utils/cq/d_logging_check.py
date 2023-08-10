#!/usr/bin/env python

"""Check and re-write DAOS logging macros to preferred coding style

Apply some checks for the macros, partly for correctness but also for consistency.  Some errors
can be fixed however not all can.  Code formatting will be incorrect after fixing so clang-format
(or git-clang-format -f) should be used after this.

Use --fix to apply any correctness fixes that can be applied.
Use --correct to apply any style changes that can be applied.   Style changes will be applied
if fixes are being applied.
"""

import argparse
import inspect
import sys
import re
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

    def count(self, string):
        """Returns count of substring"""
        return self._code.count(string)

    def __contains__(self, string):
        return string in self._code

    def __str__(self):
        if self.modified or (self.corrected and ARGS.correct):
            return f'{self._code}\n'
        return self._line

    def expand(self):
        """Expand line to end"""
        while not self._code.endswith(';'):
            to_add = str(next(self._fo))
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
        self.note('Required fixes')

    def raw(self):
        """Returns the code"""
        return self._code

    def warning(self, msg):
        """Show a warning"""
        print(f'{self._fo.fname}:{self._lineno} {msg}')
        if ARGS.github:
            fn_name = inspect.stack()[1].function
            fn_name = fn_name.replace('_', '-')
            print(f'::warning file={self._fo.fname},line={self._lineno},::{fn_name}, {msg}')

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


# Logging macros where the new-line is added if missing.
PREFIXES = ['D_ERROR', 'D_WARN', 'D_INFO', 'D_NOTE', 'D_ALERT', 'D_CRIT', 'D_FATAT', 'D_EMIT',
            'D_TRACE_INFO', 'D_TRACE_NOTE', 'D_TRACE_WARN', 'D_TRACE_ERROR', 'D_TRACE_ALERT',
            'D_TRACE_CRIT', 'D_TRACE_FATAL', 'D_TRACE_EMIT', 'RPC_TRACE', 'RPC_ERROR',
            'VOS_TX_LOG_FAIL', 'VOS_TX_TRACE_FAIL', 'D_DEBUG']

# Logging macros where a new-line is always added.
PREFIXES_NNL = ['DFUSE_LOG_WARNING', 'DFUSE_LOG_ERROR', 'DFUSE_LOG_DEBUG', 'DFUSE_LOG_INFO',
                'DFUSE_TRA_WARNING', 'DFUSE_TRA_ERROR', 'DFUSE_TRA_DEBUG', 'DFUSE_TRA_INFO',
                'DH_PERROR_SYS', 'DH_PERROR_DER',
                'DL_ERROR', 'DHL_ERROR', 'DHL_WARN', 'DL_WARN', 'DL_INFO', 'DHL_INFO']


PREFIXES_ALL = PREFIXES.copy()
PREFIXES_ALL.extend(PREFIXES_NNL)


class AllChecks():
    """All the checks in one class"""

    def __init__(self, file_object):
        self._fo = file_object
        self.line = ''
        self._output = io.StringIO()
        self.modified = False
        self.corrected = False

    def run_all_checks(self):
        """Run everything

        Iterate over the input file line by line checking for logging use and run checks on lines
        where the macros are used.  Ignore lines within macro definitions.
        """
        prev_macro = False
        for line in self._fo:
            if line.endswith('\\'):
                prev_macro = True
                # line.note('Part of macro, not checking')
                line.write(self._output)
                continue

            if prev_macro:
                prev_macro = False
                line.write(self._output)
                continue

            if not any(map(line.startswith, PREFIXES_ALL)):
                line.write(self._output)
                continue

            line.expand()

            self.check_print_string(line)
            self.check_quote(line)
            self.check_return(line)
            self.check_df_rc_dot(line)
            self.check_for_newline(line)
            self.check_df_rc(line)
            self.remove_trailing_period(line)
            self.check_quote(line)

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
        print('Changes saved, run git-clang-format to format them.')

    def check_print_string(self, line):
        """Check for %s in message"""
        if line.startswith('DH_PERROR'):
            return
        count = line.count('%s')
        if count == 0:
            return
        if count == 1 and line.count('strerror') == 1:
            return
        if '?' in line:
            line.note('Message uses %s with trigraph')
        else:
            line.note('Message uses %s without trigraph')

    def check_quote(self, line):
        """Check for double quotes in message"""
        if '""' not in line:
            return

        # Do not remove if used in tri-graphs.
        if ': ""' in line or '? ""' in line or '\\""' in line:
            return

        line.correct(line.raw().replace('""', ''))

    def check_return(self, line):
        """Check for one return character"""
        max_newlines = 1
        code = line.raw()
        if any(map(code.startswith, PREFIXES_NNL)):
            max_newlines = 0

        if '"%s",' in code:
            line.note('Use of %s at end of log-line, unable to check')
            return

        count = code.count('\\n')
        # Some logging calls contain multiple new-line characters and that's OK, as long as one of
        # them isn't at the end of a line.
        if max_newlines == 0 and count > max_newlines:
            line.warning("Line contains too many newlines")

    def check_df_rc_dot(self, line):
        """Check there is no . after DF_RC"""
        code = line.raw()
        count = code.count('DF_RC')
        if count == 0:
            return
        code = code.replace('DF_RC ', 'DF_RC')
        if 'DF_RC".\\n"' not in code:
            return
        code = code.replace('DF_RC".\\n"', 'DF_RC"\\n"')
        line.warning('Extra . after DP_RC macro')
        line.fix(code)

    def check_df_rc(self, line):
        r"""Check for text before DF_RC macro

        Re-flow lines that use DF_RC so that they are of the form '...: " DF_RC,'
        There should be a ": " before the DF_RC.
        There should not be a space before the :
        There should not be other special characters used.
        The variable name should not be printed

        """
        code = line.raw()
        count = code.count('DF_RC')
        if count == 0:
            return
        if count != 1:
            line.note('Cannot check lines with multiple DF_RC')
            return
        if not code.endswith('));'):
            line.note('Unable to check DF_RC')
            return

        # Remove any spaces around macros as these may or may not be present.  This updated input
        # is used for the update check at the end so white-space differences here will not cause
        # code to be re-written.
        code = re.sub(r' ?DF_RC ?', 'DF_RC', code)
        code = re.sub(r' ?DP_RC ?', 'DP_RC', code)

        # Check that DF_RC is at the end of the line, it should be.

        if '\\n' in code:
            if 'DF_RC"\\n"' not in code:
                line.note('DF_RC is not at end of line')
                return
        else:
            if 'DF_RC,' not in code:
                line.note('DF_RC is not at end of line')
                return

        # Extract the variable name
        parts = code[:-3].split('(')
        if not parts[-2].endswith('DP_RC'):
            line.note('Function in DF_RC call')
            return
        var_name = parts.pop()
        new_code = '('.join(parts)
        assert new_code.endswith('DP_RC')
        new_code = new_code[:-5]

        # Strip out the string formatting message
        parts = code.split('DF_RC')
        assert len(parts) == 2
        msg = parts[0]

        assert msg.endswith('"')
        msg = msg[:-1]

        # Check what comes before DF_RC in the message, and strip any trailing punctuation.
        imsg = None
        while imsg != msg:
            imsg = msg
            if any(map(msg.endswith, [' ', '=', '.', ',', ':', ';'])):
                msg = msg[:-1]
            if msg.endswith(var_name):
                msg = msg[:-len(var_name)]
            if msg.endswith('rc'):
                msg = msg[:-2]

        # Put it all back together with consistent style.
        new_code = f'{msg}: "DF_RC{parts[1]}'

        if any(map(line.startswith, ['D_ERROR', 'D_WARN', 'D_INFO'])):
            new_code = f'{msg}"DF_RC{parts[1]}'
            if line.startswith('D_ERROR'):
                new_code = f'DL_ERROR({var_name}, {new_code[8:]}'
            elif line.startswith('D_WARN'):
                new_code = f'DL_WARN({var_name}, {new_code[7:]}'
            else:
                new_code = f'DL_INFO({var_name}, {new_code[7:]}'
            new_code = new_code.replace('DF_RC', '')
            new_code = new_code.replace(f',DP_RC({var_name})', '')

        if new_code != code:
            line.correct(new_code)

    def check_for_newline(self, line):
        """Remove optional new-lines"""
        code = line.raw()

        if any(map(code.startswith, PREFIXES_NNL)):
            return

        if '\\"\\n' in code:
            line.note('Unable to check for newlines')
            return

        new_code = code.replace('"\\n",', ',')
        new_code = new_code.replace('\\n",', '",')
        new_code = new_code.replace('"\\n")', ')')
        new_code = new_code.replace('\\n")', '")')
        if new_code != code:
            line.correct(new_code)

    def remove_trailing_period(self, line):
        """Remove . from the end of a line"""
        code = line.raw()

        new_code = code
        before_code = None
        while new_code != before_code:
            before_code = new_code
            new_code = re.sub(r'\.",', '",', new_code)
            new_code = re.sub(r'\."\)', '")', new_code)

        if new_code != code:
            line.correct(new_code)


def one_entry(fname):
    """Process one path entry

    Returns true if there are un-fixed errors.
    """
    if not any(map(fname.endswith, ['.c', '.h'])):
        return False

    if any(map(fname.endswith, ['pb-c.c', 'pb-c..h'])):
        return False

    if ARGS.verbose:
        print(f'Checking {fname}')
    filep = FileParser(fname)

    checks = AllChecks(filep)

    checks.run_all_checks()

    if (ARGS.fix and checks.modified) or (ARGS.correct and checks.corrected):
        print(f'Saving updates to {fname}')
        checks.save(fname)
        return False

    if checks.modified and not ARGS.fix:
        return True
    return False


def main():
    """Do something"""
    parser = argparse.ArgumentParser(description='Verify DAOS logging in source tree')
    parser.add_argument('--fix', action='store_true', help='Apply required fixes')
    parser.add_argument('--correct', action='store_true', help='Apply optional fixes')
    parser.add_argument('--github', action='store_true')
    parser.add_argument('-v', '--verbose', action='store_true')
    parser.add_argument('files', nargs='*')

    global ARGS

    ARGS = parser.parse_args()
    unfixed_errors = False

    for fname in ARGS.files:
        if os.path.isfile(fname):
            one_entry(fname)
        else:
            for root, dirs, files in os.walk(fname):
                for name in files:
                    if one_entry(os.path.join(root, name)):
                        unfixed_errors = True
                if '.git' in dirs:
                    dirs.remove('.git')
                if root == 'src/control' and 'vendor' in dirs:
                    dirs.remove('vendor')
    if unfixed_errors:
        print('Required fixes not applied')
        sys.exit(1)


if __name__ == '__main__':
    main()
