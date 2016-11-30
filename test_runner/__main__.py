#!/usr/bin/env python3
# Copyright (c) 2016 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
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

from yaml import load
try:
    from yaml import CLoader as Loader
except ImportError:
    from yaml import Loader

from importlib import import_module
#pylint: disable=import-error
from TestRunner import TestRunner
from InfoRunner import InfoRunner
from MultiRunner import MultiRunner


def import_daemon(name, info):
    """ import the daemon module and load the class """
    try:
        _module = import_module(name)
        try:
            _class = getattr(_module, name)(info)
        except AttributeError:
            print("Class does not exist")
    except ImportError:
        print("Module does not exist")
    return _class or None

def testmain(info=None, start=1, testMode=None):
    """ main for test runner """
    daemon = None
    test_list = []

    # load test list
    if len(sys.argv) <= start:
        print("No test files given, using test_list")
        with open("scripts/test_list.yml", 'r') as fd:
            test_load = load(fd, Loader=Loader)
        test_list = test_load['test_list'].copy()
    else:
        for k in range(start, len(sys.argv)):
            test_list.append(sys.argv[k])
    print("Test list: " + str(test_list))

    # load and start daemon if required
    use_daemon = info.get_config("use_daemon", "")
    if use_daemon:
        daemon = import_daemon(use_daemon, info)
        daemon.launch_process()
    # load test object and start the testing
    if testMode == "littleChief":
        tester = MultiRunner(info, test_list)
    else:
        tester = TestRunner(info, test_list)
    rc = tester.run_testcases()
    if daemon:
        daemon.stop_process()
    if rc == 0:
        print("All tests passed\n")
    else:
        print("This run had test failures\n")
    return rc

def clientmain(info=None):
    """ main for client runner """
    myname = info.get_config("client", "")
    print("clinet name: %s" % myname)
    time.sleep(60)
    return 0

def main():
    """ main for test runner """
    rc = 1
    start = 1
    config = {}

    if len(sys.argv) > 1:
        if "config" in sys.argv[1]:
            start = 2
            config_file = sys.argv[1].split("=", 1)
            with open(config_file[1], "r") as config_fd:
                config = json.load(config_fd)
    else:
        print("No config file given")

    if 'build_path' in config:
        testing_dir = os.path.realpath(os.path.join(
            config.get('build_path'), "TESTING"))
        os.chdir(testing_dir)
    else:
        testing_dir = os.getcwd()
    sys.path.append(testing_dir)
    info = InfoRunner(config)
    # setup log base directory name
    if 'log_base_path' not in config:
        info.set_config('log_base_path', '', "testLogs/testRun")
    log_base = info.get_config('log_base_path')
    if not os.path.exists(log_base):
        os.makedirs(log_base)
    # setup default evnironment variables and path
    if not info.env_setup():
        exit(1)
    testMode = "unitTest"
    if config and 'test_mode' in config:
        testMode = config.get('test_mode', "unitTest")
    if testMode and 'client' in config:
        rc = clientmain(info)
    else:
        rc = testmain(info, start, testMode)
    exit(rc)

if __name__ == "__main__":
    main()
