#!/usr/bin/env python3
"""Wrapper script for calling pylint"""

from pylint.lint import Run
from pylint.reporters.collecting_reporter import CollectingReporter
from pylint.lint import pylinter
from collections import Counter
import subprocess  # nosec
import sys
import sl.check_script


def parse_file(target_file):
    """Main program"""
    rep = CollectingReporter()

    wrapper = None

    if isinstance(target_file, list):
        target = list(target_file)
        target.extend(['--jobs', '100'])
    elif target_file.endswith('SConstruct') or target_file.endswith('SConscript'):
        wrapper = sl.check_script.WrapScript(target_file, output=f'{target_file}.pycheck')
        target = [wrapper.wrap_file]
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

        print('{path}:{line}:{column}: {message-id}: {message} ({symbol})'.format(**vals))
        types[msg.category] += 1
        symbols[msg.symbol] += 1

    if not types:
        return
    for (mtype, count) in types.most_common():
        print(f'{mtype}:{count}')

    for (mtype, count) in symbols.most_common():
        print(f'{mtype}:{count}')


def run_git_files():
    """Run pylint on contents of 'git ls-files'"""

    ret = subprocess.run(['git', 'ls-files'], check=True, capture_output=True)
    stdout = ret.stdout.decode('utf-8')
    py_files = []
    for file in stdout.splitlines():
        if not file.endswith('.py'):
            continue
        py_files.append(file)
    parse_file(py_files)


def run_input_file():
    """Run from a input file"""

#    py_files = []
    with open('utils/to-check') as fd:
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
#            print(file)
            parse_file(file)
            continue
#            py_files.append(file)
#            if len(py_files) == 10:
#                parse_file(py_files)
#                py_files = []
#    parse_file(py_files)


def main():
    """Main program"""

    pylinter.MANAGER.clear_cache()

    if len(sys.argv) == 2:
        if sys.argv[1] == 'git':
            run_git_files()
            return
        if sys.argv[1] == 'from-file':
            run_input_file()
            return
        parse_file(sys.argv[1])
    else:
        parse_file('SConstruct')
        parse_file('src/SConscript')
        parse_file('utils/node_local_test.py')
        parse_file('ci/gha_helper.py')


if __name__ == "__main__":
    main()
