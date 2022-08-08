#!/usr/bin/env python3
"""Wrapper script for calling pylint"""

import os
import sys
import re
from collections import Counter
import tempfile
import subprocess  # nosec
import argparse
from pylint.lint import Run
from pylint.reporters.collecting_reporter import CollectingReporter

try:
    from pylint.lint import pylinter
except ImportError:
    import pylint.lint as pylinter

# Pylint checking for the DAOS project.

# Wrapper script for pylint that does the following:
#  Wraps scons commands and adjusts report line numbers
#  Sets python-path or similar as required
#  Runs in parallel across whole tree
#  Supports minimum python version
#  Supports python virtual environment usage
#  Can be used by atom.io live
#  Outputs directly to GitHub annotations
# To be added:
#  Can be used in Jenkins to report regressions
#  Can be used as a commit-hook
#  flake8 style --diff option

# For now this splits code into one of three types, build (scons), ftest or other.  For build code
# it enforces all style warnings except f-strings, for ftest it sets PYTHONPATH correctly and
# does not warn about f-strings, for others it runs without any special flags.

# Errors are reported as annotations to PRs and will fail the build, as do warnings in the build
# code.  The next step is to enable warnings elsewhere to be logged, but due to the large number
# that currently exist in the code-base we need to restrict this to modified code.  Spellings can
# also be enabled shortly however we have a number to correct or whitelist before enabling.


class WrapScript():
    """Create a wrapper for a scons file and maintain a line mapping"""

    def __init__(self, fname):

        self.line_map = {}
        # pylint: disable-next=consider-using-with
        self._outfile = tempfile.NamedTemporaryFile(mode='w+', prefix='daos_pylint_')
        self.wrap_file = self._outfile.name
        with open(fname, 'r') as infile:
            self._read_files(infile, self._outfile)
        self._outfile.flush()

    def _read_files(self, infile, outfile):
        old_lineno = 1
        new_lineno = 1
        scons_header = False

        def _remap_count():
            for iline in range(new_lineno, new_lineno + added):
                self.line_map[iline] = old_lineno - 1

        for line in infile.readlines():
            outfile.write(line)
            self.line_map[new_lineno] = old_lineno
            old_lineno += 1
            new_lineno += 1

            match = re.search(r'^(\s*)Import\(.(.*).\)', line)
            if match:
                if not scons_header:
                    scons_header = True
                    new_lineno += self.write_header(outfile)
                variables = []
                for var in match.group(2).split():
                    newvar = var.strip("\",     '")
                    variables.append(newvar)
                added = self.write_variables(outfile, match.group(1), variables)
                _remap_count()
                new_lineno += added

            match = re.search(r'^(\s*)Export\(.(.*).\)', line)
            if not match:
                match = re.search(r'^(\s*).*exports=[.(.*).]', line)
            if match:
                if not scons_header:
                    scons_header = True
                    new_lineno += self.write_header(outfile)
                variables = []
                for var in match.group(2).split():
                    newvar = var.strip("\",     '")
                    variables.append(newvar)
                added = self.read_variables(outfile, match.group(1), variables)
                _remap_count()
                new_lineno += added

            if not scons_header:
                # Insert out header after the first blank line.  It is possible to have valid
                # python files which do not have blank lines until inside the first function
                # which breaks this logic as the inserted code is at the wrong indentation however
                # such code throws flake errors and we trap for that so whilst the logic here is
                # not universally correct it should be correct for all flake clean code.
                if line.strip() == '':
                    added = self.write_header(outfile)
                    _remap_count()
                    new_lineno += added
                    scons_header = True

    @staticmethod
    def read_variables(outfile, prefix, variables):
        """Add code to define fake variables for pylint"""
        newlines = 0
        for variable in variables:
            # pylint: disable-next=consider-using-f-string
            outfile.write("%sprint(f'{%s}')\n" % (prefix, variable))
            newlines += 1
        return newlines

    @staticmethod
    def write_variables(outfile, prefix, variables):
        """Add code to define fake variables for pylint"""
        newlines = 0

        for variable in variables:
            if variable.upper() == 'PREREQS':
                newlines += 1
                outfile.write(
                    f'{prefix}{variable} = PreReqComponent(DefaultEnvironment(), Variables())\n')
                variables.remove(variable)
        for variable in variables:
            if "ENV" in variable.upper():
                newlines += 1
                outfile.write("%s%s = DefaultEnvironment()\n" % (prefix, variable))
            elif "OPTS" in variable.upper():
                newlines += 1
                outfile.write("%s%s = Variables()\n" % (prefix, variable))
            elif "PREFIX" in variable.upper():
                newlines += 1
                outfile.write("%s%s = ''\n" % (prefix, variable))
            elif "TARGETS" in variable.upper() or "TGTS" in variable.upper():
                newlines += 1
                outfile.write("%s%s = ['fake']\n" % (prefix, variable))
            else:
                newlines += 1
                outfile.write("%s%s = None\n" % (prefix, variable))

        return newlines

    @staticmethod
    def write_header(outfile):
        """write the header"""

        # Always import PreReqComponent here, but it'll only be used in some cases.  This causes
        # errors in the toplevel SConstruct which are suppressed, the alternative would be to do
        # two passes and only add the include if needed later.
        outfile.write("""# pylint: disable-next=unused-wildcard-import,wildcard-import
from SCons.Script import * # pylint: disable=import-outside-toplevel
# pylint: disable-next=import-outside-toplevel,unused-wildcard-import,wildcard-import
from SCons.Variables import *
from prereq_tools import PreReqComponent # pylint: disable=unused-import\n""")
        return 5

    def convert_line(self, line):
        """Convert from a line number in the report to a line number in the input file"""
        return self.line_map[line]


class FileTypeList():
    """Class for sorting files

    Consumes a list of file/module names and sorts them into categories so that later on each
    category can be run in parallel.
    """
    def __init__(self):
        self.ftest_files = []
        self.scons_files = []
        self.fake_scons = []
        self.files = []
        self._regions = {}

    def add_regions(self, file, regions):
        """Mark that only some regions for file should be reported"""
        self._regions[file] = regions

    def file_count(self):
        """Return the number of files to be checked"""
        return len(self.ftest_files) + len(self.scons_files) \
            + len(self.files) + len(self.fake_scons)

    def add(self, file):
        """Add a filename to the correct list"""

        def is_scons_file(filename):
            """Returns true if file is used by Scons and needs annotations"""

            if filename == 'SConstruct' or filename.endswith('SConscript'):
                return True

            if not file.endswith('.py'):
                return False

            return 'site_scons' in filename

        if is_scons_file(file):
            self.scons_files.append(file)
            return

        if not file.endswith('.py'):
            return

        # If files are in a subdir under ftest then they need to be treated differently.
        if 'src/tests/ftest/' in file:
            self.ftest_files.append(file)
            return

        if 'fake_scons' in file:
            self.fake_scons.append(file)
            return

        if 'src/control/vendor' in file:
            return
        if 'src/vos/storage_estimator' in file:
            return

        self.files.append(file)

    def __str__(self):
        desc = "FileTypeList\n"
        if self.files:
            desc += f'files: {",".join(self.files)}\n'
        if self.ftest_files:
            desc += f'ftest files: {",".join(self.ftest_files)}\n'
        if self.fake_scons:
            desc += f'fake scons files: {",".join(self.fake_scons)}\n'
        if self.scons_files:
            desc += f'scons files: {",".join(self.scons_files)}\n'
        return desc

    def run(self, args):
        """Run pylint against all files"""
        print(self)
        failed = False
        if self.files:
            if self.parse_file(args, self.files):
                failed = True
        if self.ftest_files:
            if self.parse_file(args, self.ftest_files, ftest=True):
                failed = True
        if self.fake_scons:
            if self.parse_file(args, self.fake_scons, fake_scons=True):
                failed = True
        if self.scons_files:
            for file in self.scons_files:
                if self.parse_file(args, file, scons=True):
                    failed = True
        return failed

    def parse_file(self, args, target_file, ftest=False, scons=False, fake_scons=False):
        """Parse a list of targets.

        Returns True if warnings issued to GitHub."""

        # pylint: disable=too-many-branches

        def word_is_allowed(word, code):
            """Return True is misspelling is permitted"""

            # pylint: disable=too-many-return-statements

            # Skip short words for now to cut down on noise whilst we resolve existing issues.
            if len(word) < 5:
                return True
            # Skip the "Fake" annotations from fake scons.
            if code.startswith(f'Fake {word}'):
                return True
            # Skip things that look like function documentation
            if code.startswith(f'{word} ('):
                return True
            # Skip things that look like command options.
            if f' -{word}' in code or f' --{word}' in code:
                return True
            # Skip things which are quoted
            if f"'{word}'" in code:
                return True
            # Skip things which are quoted the other way
            if f'"{word}"' in code:
                return True
            # Skip things which are in braces
            if f'({word})' in code:
                return True
            # Skip words which appear to be part of a path
            if f'/{word}/' in code:
                return True
            # Skip things are followed by open quotes
            if f'{word}(' in code:
                return True
            # Skip things which look like source files.
            if f'{word}.c' in code:
                return True
            # Skip things are followed by open colon
            if f'{word}:' in code:
                return True
            # Skip test files.
            if f'{word}.txt' in code:
                return True
            return False

        def parse_msg(vals, msg):
            if wrapper:
                vals['path'] = target_file
                vals['line'] = wrapper.convert_line(msg.line)
            else:
                vals['path'] = msg.path
                vals['line'] = msg.line
            vals['column'] = msg.column
            vals['message-id'] = msg.msg_id
            vals['message'] = msg.msg
            vals['symbol'] = msg.symbol

            # Duplicates, needed for message_template.
            vals['msg'] = msg.msg
            vals['msg_id'] = msg.msg_id

            # The build/scons code is mostly clean, so only allow f-string warnings.
            if scons and msg.symbol != 'consider-using-f-string':
                vals['category'] = 'error'

            # Flag some serious warnings as errors
            if msg.symbol in ('condition-evals-to-constant'):
                vals['category'] = 'error'

        failed = False
        rep = CollectingReporter()
        wrapper = None
        init_hook = None
        if isinstance(target_file, list):
            target = list(target_file)
            target.extend(['--jobs', str(min(len(target_file), 20))])
        elif scons:
            # Do not warn on module name for SConstruct files, we don't get to pick their name.
            ignore = ['invalid-name', 'ungrouped-imports']
            if target_file.endswith('__init__.py'):
                ignore.append('relative-beyond-top-level')
            wrapper = WrapScript(target_file)
            target = [wrapper.wrap_file]
            target.extend(['--disable', ','.join(ignore)])
            init_hook = """import sys
sys.path.append('site_scons')
sys.path.insert(0, 'utils/sl/fake_scons')"""
        else:
            target = [target_file]

        if fake_scons:
            # Do not warn on module name for fake_scons files, we don't get to pick their name.
            target.extend(['--disable', 'invalid-name,too-few-public-methods'])

        if ftest:
            target.extend(['--disable', 'consider-using-f-string'])
            init_hook = """import sys
sys.path.append('src/tests/ftest')
sys.path.append('src/tests/ftest/util/apricot')
sys.path.append('src/tests/ftest/cart/util/')
sys.path.append('src/tests/ftest/util')
sys.path.append('src/client')
sys.path.append('site_scons')"""

        target.extend(['--persistent', 'n'])
        if init_hook:
            target.extend(['--init-hook', init_hook])

        if args.rcfile:
            target.extend(['--rcfile', args.rcfile])

        results = Run(target, reporter=rep, do_exit=False)

        types = Counter()
        symbols = Counter()

        # List of errors that are in modified files but not modified code.
        file_warnings = []

        for msg in results.linter.reporter.messages:
            vals = {}
            vals['category'] = msg.category

            # Spelling mistakes. There are a lot of code to silence code blocks and examples
            # in comments.  Be strict for everything but ftest code currently.
            if not scons and msg.msg_id in ('C0401', 'C0402'):
                lines = msg.msg.splitlines()
                header = lines[0]
                code = lines[1].strip()
                components = header.split("'")
                word = components[1]
                # Skip test-tags, these are likely not words.
                if ftest and code.startswith(':avocado: tags='):
                    continue
                if word_is_allowed(word, code):
                    continue

                # Finally, promote any spelling mistakes not silenced above or in ftest to error.
                if not ftest:
                    vals['category'] = 'error'

            # Inserting code can cause wrong-module-order.
            if scons and msg.msg_id == 'C0411' and 'from SCons.Script import' in msg.msg:
                continue

            parse_msg(vals, msg)

            types[vals['category']] += 1
            symbols[msg.symbol] += 1

            if vals['path'] in self._regions:
                line_changed = self._regions[vals['path']].in_region(vals['line'])
                if line_changed:
                    print('Warning is in modified code:')
                    failed = True
                    vals['category'] = 'error'
                else:
                    file_warnings.append(msg)
                    continue

            print(args.msg_template.format(**vals))

            if args.format == 'github':
                if vals['category'] in ('convention', 'refactor'):
                    continue
                if vals['category'] == 'warning':
                    continue
                failed = True
                # pylint: disable-next=consider-using-f-string
                print('::{category} file={path},line={line},col={column},::{symbol}, {msg}'.format(
                    **vals))

        if file_warnings:
            print('Warnings from modified files:')
        for msg in file_warnings:
            vals = {}
            vals['category'] = msg.category
            parse_msg(vals, msg)
            print(args.msg_template.format(**vals))
            if args.format == 'github':
                # Report all messages in modified files to github, but do it at the notice level.
                vals['category'] = 'notice'
                # pylint: disable-next=consider-using-f-string
                print('::{category} file={path},line={line},col={column},::{symbol}, {msg}'.format(
                    **vals))

        if not types or args.reports == 'n':
            return failed
        for (mtype, count) in types.most_common():
            print(f'{mtype}:{count}')

        for (mtype, count) in symbols.most_common():
            print(f'{mtype}:{count}')
        return failed


def get_git_files(directory=None):
    """Run pylint on contents of 'git ls-files'"""

    all_files = FileTypeList()

    cmd = ['git', 'ls-files']
    if directory:
        cmd.append(directory)

    ret = subprocess.run(cmd, check=True, capture_output=True)
    stdout = ret.stdout.decode('utf-8')
    for file in stdout.splitlines():
        all_files.add(file)
    return all_files


def run_input_file(args, input_file):
    """Run from a input file"""

    all_files = FileTypeList()

    with open(input_file, encoding='utf-8') as fd:
        for file in fd.readlines():
            all_files.add(file.strip())

    all_files.run(args)


class OutPutRegion:
    """Class for managing file regions"""

    def __init__(self):
        self.regions = []

    def add_region(self, base, length):
        """Add regions to file"""
        self.regions.append((base, base + length))

    def in_region(self, line):
        """Check if line is in region"""
        for region in self.regions:
            if region[0] < line < region[1]:
                return True
        return False


def main():
    """Main program"""

    pylinter.MANAGER.clear_cache()
    parser = argparse.ArgumentParser()

    # Basic options.
    parser.add_argument('--git', action='store_true')
    parser.add_argument('--from-file')

    spellings = True
    try:
        # pylint: disable-next=import-outside-toplevel,unused-import
        import enchant  # noqa: F401
    except ModuleNotFoundError:
        spellings = False
    except ImportError:
        spellings = False

    rcfile = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pylintrc')

    # Args that atom uses.
    parser.add_argument('--msg-template',
                        default='{path}:{line}:{column}: {message-id}: {message} ({symbol})')
    parser.add_argument('--reports', choices=['y', 'n'], default='y')
    parser.add_argument('--output-format', choices=['text'])
    parser.add_argument('--rcfile', default=rcfile)
    parser.add_argument('--diff', action='store_true')

    # pylint: disable-next=wrong-spelling-in-comment
    # A --format github option as yamllint uses.
    parser.add_argument('--format', choices=['text', 'github'], default='text')

    # File list, zero or more.
    parser.add_argument('files', nargs='*')

    args = parser.parse_args()

    rc_tmp = None

    # If spellings are likely supported and using the default configuration file then enable using
    # a temporary file.
    if spellings and args.rcfile == rcfile:
        # pylint: disable-next=consider-using-with
        rc_tmp = tempfile.NamedTemporaryFile(mode='w+', prefix='pylintrc')
        with open(rcfile) as src_file:
            rc_tmp.write(src_file.read())
        rc_tmp.flush()
        rc_tmp.write('[SPELLING]\n')
        rc_tmp.write('spelling-dict=en_US\n')
        rc_tmp.write('spelling-private-dict-file=utils/cq/words.dict\n')
        rc_tmp.flush()
        args.rcfile = rc_tmp.name

    if args.diff:
        # There are to be two different ways of driving this, for a commit-hook where we only
        # check files changed and in GitHub actions where we check the entire tree and are more
        # strict for files which have changed.
        if args.git:
            all_files = get_git_files()
        else:
            all_files = FileTypeList()
        regions = None
        file = None
        for line in sys.stdin.readlines():
            if line.startswith('diff --git a/'):
                parts = line.split(' ')
                file = parts[3][2:-1]
                regions = OutPutRegion()
                all_files.add_regions(file, regions)
                if not args.git:
                    all_files.add(file)
                continue
            if line.startswith('@@ '):
                parts = line.split(' ')
                (post_start, post_len) = parts[2][1:].split(',')
                regions.add_region(int(post_start), int(post_len))
                continue
        if file and regions:
            all_files.add_regions(file, regions)
        failed = all_files.run(args)
        if failed:
            sys.exit(1)
        return
    if args.git:
        all_files = get_git_files()
        if all_files.run(args):
            sys.exit(1)
        return
    if args.from_file:
        run_input_file(args, args.from_file)
        return
    all_files = FileTypeList()
    all_dirs = []
    for file in args.files:
        if os.path.isfile(file):
            all_files.add(file)
        elif os.path.isdir(file):
            all_dirs.append(file)
        else:
            parser.print_usage()
            sys.exit(1)
    if all_dirs:
        if len(all_dirs) == 1 and all_files.file_count() == 0:
            all_files = get_git_files(directory=all_dirs[0])
            if all_files.run(args):
                sys.exit(1)
        else:
            print('Only one directory can be shown at once')
            parser.print_usage()
            sys.exit(1)
    elif all_files.file_count() == 0:
        print('You must specify at least one input file')
        parser.print_usage()
        sys.exit(1)
    else:
        all_files.run(args)


if __name__ == "__main__":
    main()
