#!/usr/bin/env python3
"""Wrapper script for calling pylint"""

from collections import Counter
import subprocess  # nosec
import argparse
import os
from pylint.lint import Run
from pylint.reporters.collecting_reporter import CollectingReporter
from pylint.lint import pylinter
import sl.check_script
# Atom uses %p/venv/bin/pylint
# %p/site_scons:%p/utils/sl/fake_scons:%p/src/test/ftest/util


class FileTypeList():
    """Class for sorting files

    Consumes a list of file/module names and sorts them into categories so that later on each
    category can be run in parallel.
    """
    def __init__(self):
        self.ftest_files = []
        self.scons_files = []
        self.files = []

    def add(self, file):
        """Add a filename to the correct list"""
        if is_scons_file(file):
            self.scons_files.append(file)
            return
        if not file.endswith('.py'):
            return
        if file.startswith('src/control/vendor'):
            return

        # If files are in a subdir under ftest then they need to by treated differently.
        if file.startswith('src/tests/ftest/'):
            if os.path.dirname(file) != 'src/tests/ftest':
                self.ftest_files.append(file)
                return
        self.files.append(file)

    def __str__(self):
        desc = "FileTypeList\n"
        if self.files:
            desc += f'files: {",".join(self.files)}\n'
        if self.ftest_files:
            desc += f'ftest files: {",".join(self.ftest_files)}\n'
        if self.scons_files:
            desc += f'scons files: {",".join(self.scons_files)}\n'
        return desc

    def run(self, args):
        """Run pylint against all files"""
        print(self)
        if self.files:
            parse_file(args, self.files)
        if self.ftest_files:
            parse_file(args, self.ftest_files, ftest=True)
        if self.scons_files:
            for file in self.scons_files:
                parse_file(args, file, scons=True)


def is_scons_file(filename):
    """Returns true if file is used by Scons and needs annotations"""

    if filename == 'SConstruct':
        return True
    if filename.endswith('SConscript'):
        return True
    if filename == 'site_scons/site_tools/protoc/__init__.py':
        return True
    # TODO: Some files here do, some don't.
    # if filename.startswith('site_scons'):
    #    return True
    return False


def parse_file(args, target_file, ftest=False, scons=False):
    """Main program"""

    rep = CollectingReporter()

    wrapper = None

    if isinstance(target_file, list):
        target = list(target_file)
        target.extend(['--jobs', str(min(len(target_file), 20))])
    elif scons:
        wrapper = sl.check_script.WrapScript(target_file, output=f'{target_file}.pycheck')
        target = [wrapper.wrap_file]
        # Do not warn on module name for SConstruct files, we don't get to pick their name.
        target.extend(['--disable', 'invalid-name'])
    else:
        target = [target_file]

    if ftest:
        target.extend(['--disable', 'consider-using-f-string'])

    target.extend(['--persistent', 'n'])

    if args.rcfile:
        target.extend(['--rcfile', args.rcfile])

    results = Run(target, reporter=rep, do_exit=False)

    types = Counter()
    symbols = Counter()

    for msg in results.linter.reporter.messages:
        vals = {}
        # Spelling mistake, do not complain about message tags.
        if msg.msg_id in ('C0401', 'C0402'):
            if ":avocado:" in msg.msg:
                continue

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
        vals['category'] = msg.category

        print(args.msg_template.format(**vals))

        if args.format == 'github':
            if msg.category in ('convention', 'refactor'):
                continue
            if msg.category == 'warning':
                continue
            # pylint: disable-next=line-too-long,consider-using-f-string
            print('::{category} file={path},line={line},col={column},::{symbol}, {msg}'.format(**vals))  # noqa: E501

        types[msg.category] += 1
        symbols[msg.symbol] += 1

    if not types or args.reports == 'n':
        return
    for (mtype, count) in types.most_common():
        print(f'{mtype}:{count}')

    for (mtype, count) in symbols.most_common():
        print(f'{mtype}:{count}')


def run_git_files(args):
    """Run pylint on contents of 'git ls-files'"""

    all_files = FileTypeList()

    ret = subprocess.run(['git', 'ls-files'], check=True, capture_output=True)
    stdout = ret.stdout.decode('utf-8')
    for file in stdout.splitlines():
        all_files.add(file)
    all_files.run(args)


def run_input_file(args, input_file):
    """Run from a input file"""

    all_files = FileTypeList()

    with open(input_file, encoding='utf-8') as fd:
        for file in fd.readlines():
            all_files.add(file.strip())

    all_files.run(args)


def main():
    """Main program"""

    pylinter.MANAGER.clear_cache()
    parser = argparse.ArgumentParser()

    # Basic options.
    parser.add_argument('--git', action='store_true')
    parser.add_argument('--from-file')

    # Args that atom uses.
    parser.add_argument('--msg-template',
                        default='{path}:{line}:{column}: {message-id}: {message} ({symbol})')
    parser.add_argument('--reports', choices=['y', 'n'], default='y')
    parser.add_argument('--output-format', choices=['text'])
    parser.add_argument('--rcfile')

    # A --format github option as yamllint uses.
    parser.add_argument('--format', choices=['text', 'github'], default='text')

    # File list, zero or more.
    parser.add_argument('files', nargs='*')

    args = parser.parse_args()

    if args.git:
        run_git_files(args, )
        return
    if args.from_file:
        run_input_file(args, args.from_file)
        return
    all_files = FileTypeList()
    for file in args.files:
        all_files.add(file)
    all_files.run(args)


if __name__ == "__main__":
    main()
