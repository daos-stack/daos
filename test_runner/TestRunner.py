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
main test runner class

"""


import os
import sys
import logging
from datetime import datetime
#pylint: disable=import-error
import PostRunner
from TestInfoRunner import TestInfoRunner
from UnitTestRunner import UnitTestRunner
from ScriptsRunner import ScriptsRunner

#pylint: enable=import-error

from yaml import dump
try:
    from yaml import CDumper as Dumper
except ImportError:
    from yaml import Dumper


class TestRunner(PostRunner.PostRunner):
    """Simple test runner"""
    logdir = ""
    test_list = []
    info = None
    test_info = None
    logger = None

    def __init__(self, info, test_list=None):
        self.info = info
        self.test_list = test_list
        self.log_dir_base = self.info.get_config('log_base_path')
        self.logger = logging.getLogger("TestRunnerLogger")
        self.now = "_{}".format(datetime.now().isoformat().replace(':', '.'))

    def dump_subtest_results(self, subtest_results):
        """ dump the test results to the log directory """
        if os.path.exists(self.logdir):
            name = "%s/subtest_results.yml" % self.logdir
            with open(name, 'w') as fd:
                dump(subtest_results, fd, Dumper=Dumper, indent=4,
                     default_flow_style=False)

    def rename_output_directory(self):
        """ rename the output directory """
        test_directives = self.test_info.get_directives()
        if os.path.exists(self.logdir):
            rename = str(test_directives.get('renameTestRun', "no")).lower()
            if rename == "no":
                newname = self.logdir
            else:
                if rename == "yes":
                    newname = "{}{}".format(self.logdir, self.now)
                else:
                    newdir = str(test_directives.get('renameTestRun'))
                    logdir = os.path.dirname(self.logdir)
                    newname = os.path.join(logdir, newdir)
                os.rename(self.logdir, newname)
            self.logger.info("TestRunner: test log directory\n %s", \
                             os.path.abspath(newname))

            dowhat = str(test_directives.get('printTestLogPath', "no")).lower()
            if dowhat == "yes":
                self.top_logdir(newname)
            elif dowhat == "dump":
                self.top_logdir(newname, dumpLogs=True)

    def post_testcase(self, subtest_results):
        """ post testcase run processing """
        self.logger.info("TestRunner: tearDown begin")
        self.test_info.dump_test_info()
        self.dump_subtest_results(subtest_results)
        self.rename_output_directory()
        self.logger.info("TestRunner: tearDown end\n\n")

    def run_testcases(self):
        """ execute test scripts """

        rtn = 0
        sys.path.append("scripts")
        file_hdlr = logging.FileHandler(
            os.path.join(self.log_dir_base + "TestRunner.log"))
        file_hdlr.setLevel(logging.DEBUG)
        self.logger.addHandler(file_hdlr)
        self.test_info = TestInfoRunner(self.info)
        for test_module_name in self.test_list:
            if self.test_info.load_testcases(test_module_name):
                return 1
            # All the log files need to be in the directory with a
            # subtest_results file for mapping to Maloo entries.
            # 'node' key is set by MultiRunner
            if self.info.get_config('node'):
                self.logdir = self.log_dir_base
            else:
                self.logdir = os.path.join(self.log_dir_base, \
                                  str(self.test_info.get_test_info('testName')))
                try:
                    os.makedirs(self.logdir)
                except OSError:
                    newname = "{}{}".format(self.logdir, self.now)
                    os.rename(self.logdir, newname)
                    os.makedirs(self.logdir)
            testMode = self.test_info.get_test_info('directives', 'testMode')
            if testMode == "scripts":
                runner = ScriptsRunner(self.test_info, self.logdir)
            else:
                runner = UnitTestRunner(self.test_info, self.logdir)
            self.test_info.add_default_env()
            self.logger.info("****************************************\n " + \
                             "TestRunner: " + \
                             str(self.test_info.get_test_info('testName')) + \
                             "\n***************************************"
                            )
            (rc, rtn_info) = runner.execute_strategy()
            rtn |= rc
            self.post_testcase(rtn_info)
            del runner
        self.logger.info(
            "\n********************************************************")
        file_hdlr.close()
        self.logger.removeHandler(file_hdlr)

        return rtn
