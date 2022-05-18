#!/usr/bin/env python3
"""Wrapper script for calling pylint"""

from pylint.lint import Run
from pylint.reporters.collecting_reporter import CollectingReporter
from pylint.lint import pylinter
from collections import Counter
import argparse
import subprocess  # nosec
import sl.check_script
# Atom uses %p/venv/bin/pylint
# %p/site_scons:%p/utils/sl/fake_scons:%p/src/test/ftest/util


def parse_file(args, target_file):
    """Main program"""

    rep = CollectingReporter()

    wrapper = None

    if isinstance(target_file, list):
        target = list(target_file)
        target.extend(['--jobs', '100'])
    elif target_file.endswith('SConstruct') or target_file.endswith('SConscript'):
        wrapper = sl.check_script.WrapScript(target_file, output=f'{target_file}.pycheck')
        target = [wrapper.wrap_file]
        # Do not warn on module name for SConstruct files, we don't get to pick their name.
        target.extend(['--disable', 'invalid-name'])
    else:
        target = [target_file]

    target.extend(['--persistent', 'n'])
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

        if args.msg_template:
            print(args.msg_template.format(**vals))
        else:
            print('{path}:{line}:{column}: {message-id}: {message} ({symbol})'.format(**vals))

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

    ret = subprocess.run(['git', 'ls-files'], check=True, capture_output=True)
    stdout = ret.stdout.decode('utf-8')
    py_files = []
    for file in stdout.splitlines():
        if not file.endswith('.py'):
            continue
        py_files.append(file)
    parse_file(args, py_files)


def run_input_file(args, input_file):
    """Run from a input file"""

    with open(input_file) as fd:
        for file in fd.readlines():
            file = file.strip()
            if file.startswith('src/control/vendor/'):
                continue
            match = False
            if file.endswith('.py'):
                match = True
            if file.endswith('SConstruct'):
                match = True
            if file.endswith('SConscript'):
                match = True
            if not match:
                continue
            parse_file(args, file)


def main():
    """Main program"""

    pylinter.MANAGER.clear_cache()
    parser = argparse.ArgumentParser()

    # Basic options.
    parser.add_argument('--git', action='store_true')
    parser.add_argument('--from-file')

    # Args that atom uses.
    parser.add_argument('--msg-template')
    parser.add_argument('--reports', choices=['y', 'n'], default='y')
    parser.add_argument('--output-format', choices=['text'])
    parser.add_argument('--rcfile')

    # File list, zero or more.
    parser.add_argument('files', nargs='*')

    args = parser.parse_args()

    if args.git:
        run_git_files(args, )
        return
    if args.from_file:
        run_input_file(args, args.from_file)
        return
    for file in args.files:
        parse_file(args, file)


if __name__ == "__main__":
    main()
