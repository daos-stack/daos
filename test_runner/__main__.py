#!/usr/bin/env python3
# Copyright (c) 2016-2017 Intel Corporation
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
import logging

from datetime import datetime
from yaml import load
try:
    from yaml import CLoader as Loader
except ImportError:
    from yaml import Loader

from importlib import import_module
#pylint: disable=import-error
from InfoRunner import InfoRunner
from TestRunner import TestRunner
from MultiRunner import MultiRunner


def import_daemon(name, info):
    """ import the daemon module and load the class """
    _class = None
    try:
        _module = import_module(name)
        try:
            _class = getattr(_module, name)(info)
        except AttributeError:
            print("Class does not exist")
    except ImportError:
        print("Module does not exist")
    return _class

def testmain(info=None, start=1, testMode=None):
    """ main for test runner """
    daemon = None
    test_list = []

    # load test list
    if len(sys.argv) <= start:
        testFile = info.get_config('test_list', '', "scripts/test_list.yml")
        print("no test files given, using %s" % testFile)
        with open(testFile, 'r') as fd:
            test_load = load(fd, Loader=Loader)
        test_list = test_load['test_list']
    else:
        for k in range(start, len(sys.argv)):
            test_list.append(sys.argv[k])
    print("Test list: " + str(test_list))
    # add the multi instance module name to the Maloo test set name
    if len(test_list) > 1:
        info.set_config('setDirectiveFromConfig', 'addTestSetName', "yes")
    # load and start daemon if required
    use_daemon = info.get_config("use_daemon", "")
    if use_daemon:
        daemon = import_daemon(use_daemon, info)
        rc = daemon.launch_process()
        if rc:
            return rc
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
    logger = logging.getLogger("TestRunnerLogger")
    logger.setLevel(logging.INFO)
    #logger.setLevel(logging.DEBUG)
    ch = logging.StreamHandler(sys.stdout)
    ch.setLevel(logging.INFO)
    logger.addHandler(ch)

    if len(sys.argv) > 1:
        if "config" in sys.argv[1]:
            start = 2
            config_file = sys.argv[1].split("=", 1)
            with open(config_file[1], "r") as config_fd:
                config = json.load(config_fd)
    else:
        logger.error("No config file given")

    if 'build_path' in config:
        testing_dir = os.path.realpath(os.path.join(
            config.get('build_path'), "TESTING"))
        os.chdir(testing_dir)
    else:
        testing_dir = os.getcwd()
    sys.path.append(testing_dir)
    info = InfoRunner(config)
    # setup log base directory name
    # the log path should end in testRun directory
    if 'log_base_path' not in config:
        log_base = "testLogs/testRun"
        info.set_config('log_base_path', '', log_base)
    else:
        log_base = info.get_config('log_base_path')
        if log_base.find("testRun") < 0:
            log_base = os.path.join(log_base, "testRun")
            info.set_config('log_base_path', '', log_base)
    # This key is set by multi runner and the directory should exist
    if 'node' not in config:
        if os.path.exists(log_base):
            newname = "{}_{}".format(log_base, datetime.now().isoformat(). \
                                     replace(':', '.'))
            os.rename(log_base, newname)
        os.makedirs(log_base)
    # setup default evnironment variables and path
    info.env_setup()
    # in some cases the description file can override this key
    testMode = config.get('test_mode', "TestRunner")
    if testMode and 'client' in config:
        rc = clientmain(info)
    else:
        rc = testmain(info, start, testMode)
    print("test log base directory\n {}\n".format(os.path.abspath(log_base)))
    exit(rc)

if __name__ == "__main__":
    main()
