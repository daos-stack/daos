#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test runner

Usage:


Execute from the install/$arch/TESTING directory. The results are placed in the
testLogs/testRun<date> directory. There you will find anything written to
stdout and stderr. The output from memcheck and callgrind are in the testRun
directory. At the end of a test run, the last testRun directory is renamed to
testRun_<date stamp>

python3 test_runner <execution file>

example:
python3 test_runner scripts/mcl_ping.yml

To use valgrind memory checking
set TR_USE_VALGRIND in the yaml file to memcheck

To use valgrind call (callgrind) profiling
set TR_USE_VALGRIND in the yaml file to callgrind

"""

import os
import sys
import json
import time

#pylint: disable=import-error
try:
    from .TestRunner import TestRunner
    from .InfoRunner import InfoRunner
    from .DvmRunner  import DvmRunner
except SystemError:
    from TestRunner import TestRunner
    from InfoRunner import InfoRunner
    from DvmRunner  import DvmRunner


def main():
    """ main for test runner """
    ortedvm = None
    config = {}
    test_list = []
    if len(sys.argv) > 1:
        if "config" in sys.argv[1]:
            start = 2
            config_file = sys.argv[1].split("=", 1)
            with open(config_file[1], "r") as config_fd:
                config = json.load(config_fd)
        elif "client" in sys.argv[1]:
            time.sleep(60)
            sys.exit(0)
        else:
            start = 1
    else:
        print("No tests given\n")
        sys.exit(1)

    if 'build_path' in config:
        testing_dir = os.path.realpath(os.path.join(
            config.get('build_path'), 'TESTING'))
        os.chdir(testing_dir)
    else:
        testing_dir = os.getcwd()
    sys.path.append(testing_dir)
    info = InfoRunner(config)
    info.env_setup()
    # load test list
    for k in range(start, len(sys.argv)):
        test_list.append(sys.argv[k])
    if config and 'use_orte-dvm' in config:
        ortedvm = DvmRunner(info)
        ortedvm.launch_dvm_process()
    tester = TestRunner(info, test_list)
    rc = tester.run_testcases()
    if ortedvm:
        ortedvm.stop_dvm_process()
    if rc == 0:
        print("All tests passed\n")
    else:
        print("This run had test failures\n")
    exit(rc)

if __name__ == "__main__":
    main()
