"""NLT: command-line entry point.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import argparse
import resource
import sys
import traceback

from .fault_injection import server_fi
from .posix_tests import PosixTests
from .reporting import WarningsFactory
from .runner import printable_test_list, run


def _positive_int(value):
    """argparse type that rejects values below 1."""
    ivalue = int(value)
    if ivalue < 1:
        raise argparse.ArgumentTypeError(f'must be >= 1, got {value}')
    return ivalue


def main():
    """Wrap the core function, and catch/report any exceptions

    This allows the junit results to show at least a stack trace and assertion message for
    any failure, regardless of if it's from a test case or not.

    Test names can either be 'pure' or include suffices that encode a particular caching
    regimen (e.g., read_caching_off).
    """
    parser = argparse.ArgumentParser(description='Run DAOS client on local node')
    parser.add_argument('--server-debug', default=None)
    parser.add_argument('--dfuse-debug', default=None)
    parser.add_argument('--client-debug', default=None)
    parser.add_argument('--class-name', default=None, help='class name to use for junit')
    parser.add_argument('--memcheck', default='some', choices=['yes', 'no', 'some'])
    parser.add_argument('--server-valgrind', action='store_true')
    parser.add_argument('--server-fi', action='store_true', help='Run server fault injection test')
    parser.add_argument('--multi-user', action='store_true')
    parser.add_argument('--no-root', action='store_true')
    parser.add_argument('--max-log-size', default=None)
    parser.add_argument('--engine-count', type=int, default=1, help='Number of daos engines to run')
    parser.add_argument('--repeat', type=_positive_int, default=1,
                        help='Repeat the test execution N times (soak/stability testing)')
    parser.add_argument('--failfast', action='store_true',
                        help='With --repeat, stop after the first failing iteration')
    parser.add_argument('--system-ram-reserved', type=int, default=None, help='GiB reserved RAM')
    parser.add_argument('--dfuse-dir', default='/tmp', help='parent directory for all dfuse mounts')
    parser.add_argument('--summary', default=None,
                        help='write a single human-readable results summary here '
                             '(default nlt-summary[-<class-name>].md, "" to disable)')
    parser.add_argument('--suite', default='ci', choices=['ci', 'manual', 'all'],
                        help='which test suite to run: ci (default), manual (long/opt-in), or all')
    parser.add_argument('--keep-logs', action='store_true',
                        help='keep every log; by default only logs with findings are retained')
    parser.add_argument('--perf-check', action='store_true')
    parser.add_argument('--log-usage-import')
    parser.add_argument('--log-usage-export')
    parser.add_argument('--log-usage-save')
    parser.add_argument('--dtx', action='store_true')
    parser.add_argument('--test', action='append', help="Use '--test list' for list")
    parser.add_argument('--exclude-test', action='append',
                        help='space separated list of tests to exclude')
    parser.add_argument('--valgrind_verbose', action='store_true', help='Use --verbose w/ valgrind')
    parser.add_argument('mode', nargs='*')
    args = parser.parse_args()

    # Default the summary name off the class so parallel CI stages (e.g. nlt vs fault-injection)
    # do not archive over each other. "" still disables it.
    if args.summary is None:
        args.summary = f'nlt-summary-{args.class_name}.md' if args.class_name else 'nlt-summary.md'

    # valgrind reduces the hard limit unless we bump the soft limit first
    if args.memcheck != "no":
        (soft, hard) = resource.getrlimit(resource.RLIMIT_NOFILE)
        if soft < hard:
            resource.setrlimit(resource.RLIMIT_NOFILE, (hard, hard))

    if args.server_fi:
        server_fi(args)
        return

    if args.mode:
        mode_list = args.mode
        args.mode = mode_list.pop(0)

        if args.mode != 'launch' and mode_list:
            print(f"unrecognized arguments: {' '.join(mode_list)}")
            sys.exit(1)
        args.launch_cmd = mode_list
    else:
        args.mode = None

    if args.mode and args.test:
        print('Cannot use mode and test')
        sys.exit(1)

    if args.test and 'list' in args.test:
        tests = printable_test_list(PosixTests.generate_test_list())
        manual = [x[len('manual_'):] for x in PosixTests.generate_manual_test_list()]
        print('''
* Tests denoted with a '*' have are run separately with caching on and caching off,
and these may be separately excluded, e.g., --excluded_test read_caching_off

Tests are:
''' + '\n'.join(sorted(tests))
              + '\n\nManual tests (run with --suite manual|all):\n'
              + '\n'.join(sorted(manual)))
        sys.exit(1)

    wf = WarningsFactory('nlt-errors.json',
                         post_error=True,
                         check='Log file errors',
                         class_id=args.class_name,
                         junit=True)

    try:
        fatal_errors = run(wf, args)
        wf.add_test_case('exit_wrapper')
        wf.close()
    except Exception as error:
        print(error)
        print(str(error))
        print(repr(error))
        trace = ''.join(traceback.format_tb(error.__traceback__))
        wf.add_test_case('exit_wrapper', str(error), output=trace)
        # Emit the summary here too: an abnormal or single-test failure never reaches the
        # normal write in run(). arm_summary() made this idempotent, so it is a no-op if run()
        # already wrote it.
        wf.write_summary()
        wf.close()
        raise

    if fatal_errors.errors:
        print("Significant errors encountered")
        sys.exit(1)
