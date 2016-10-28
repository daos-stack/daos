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
test runner class

"""

#pylint: disable=unused-import
#pylint: disable=too-many-instance-attributes
#pylint: disable=too-many-locals
#pylint: disable=too-many-public-methods

import os
import sys
import shutil
import unittest
import logging
from time import time
from datetime import datetime
#pylint: disable=import-error
import PreRunner
import PostRunner
#pylint: enable=import-error

from yaml import load, dump
try:
    from yaml import CLoader as Loader, CDumper as Dumper
except ImportError:
    from yaml import Loader, Dumper


class TestRunner(PreRunner.PreRunner, PostRunner.PostRunner):
    """Simple test runner"""
    log_dir_base = ""
    loop_name = ""
    last_testlogdir = ""
    loop_number = 0
    test_info = {}
    test_list = []
    subtest_results = []
    test_directives = {}
    info = None
    logger = None
    file_hdlr = None

    def __init__(self, info, test_list=None):
        self.info = info
        self.test_list = test_list
        self.log_dir_base = self.info.get_config('log_base_path')
        self.logger = logging.getLogger("TestRunnerLogger")
        self.logger.setLevel(logging.DEBUG)
        ch = logging.StreamHandler(sys.stdout)
        self.logger.addHandler(ch)

    def dump_subtest_results(self):
        """ dump the test results to the log directory """
        log_dir = os.path.dirname(self.info.get_config('log_base_path'))
        if os.path.exists(log_dir):
            name = "%s/subtest_results.yml" % log_dir
            with open(name, 'w') as fd:
                dump(self.subtest_results, fd, Dumper=Dumper, indent=4,
                     default_flow_style=False)

    def dump_test_info(self):
        """ dump the test info to the output directory """
        if os.path.exists(self.log_dir_base):
            name = "%s/%s_test_info.yml" % (self.log_dir_base, self.loop_name)
            with open(name, 'w') as fd:
                dump(self.test_info, fd, Dumper=Dumper, indent=4,
                     default_flow_style=False)

    def rename_output_directory(self):
        """ rename the output directory """
        if os.path.exists(self.log_dir_base):
            if str(self.test_directives.get('renameTestRun', "yes")).lower() \
               == "yes":
                newname = "%s_%s" % \
                          (self.log_dir_base, datetime.now().isoformat())
            else:
                newdir = str(self.test_directives.get('renameTestRun'))
                logdir = os.path.dirname(self.log_dir_base)
                newname = os.path.join(logdir, newdir)
            os.rename(self.log_dir_base, newname)
            self.logger.info("TestRunner: test log directory\n %s", newname)
            if str(self.test_directives.get('printTestLogPath', "no")).lower() \
               == "yes":
                self.top_logdir(newname)

    def post_run(self):
        """ post run processing """
        self.logger.info("TestRunner: tearDown begin")
        self.dump_subtest_results()
        if self.test_info['module'].get('createTmpDir'):
            envName = self.test_info['module']['createTmpDir']
            shutil.rmtree(self.test_info['defaultENV'][envName])
        if str(self.test_directives.get('renameTestRun', "yes")).lower() \
           != "no":
            self.rename_output_directory()
        self.logger.info("TestRunner: tearDown end\n\n")

    @staticmethod
    def setenv(testcase):
        """ setup testcase environment """
        module_env = testcase['setEnvVars']
        if module_env != None:
            for (key, value) in module_env.items():
                os.environ[str(key)] = value

    def resetenv(self, testcase):
        """ reset testcase environment """
        module_env = testcase['setEnvVars']
        module_default_env = self.test_info['defaultENV']
        if module_env != None:
            for key in module_env.keys():
                value = module_default_env.get(key, "")
                os.environ[str(key)] = value

    def settestlog(self, testcase_id):
        """ setup testcase environment """
        test_module = self.test_info['module']
        value = self.log_dir_base + "/" + \
                self.loop_name + "_loop" + str(self.loop_number) + "/" + \
                test_module['name'] + "_" + str(testcase_id)
        os.environ[test_module['subLogKey']] = value
        self.last_testlogdir = value

    def execute_list(self):
        """ execute test scripts """

        rtn = 0
        test_module = self.test_info['module']
        for testrun in self.test_info['execStrategy']:
            self.logger.info("************** run " + \
                             str(testrun['id']) + \
                             "******************************"
                            )
            if 'setEnvVars' in testrun:
                self.setenv(testrun)
            self.settestlog(testrun['id'])
            suite = \
                unittest.TestLoader().loadTestsFromName(test_module['name'])
            results = unittest.TestResult()
            suite.run(results)

            self.logger.info("***************** Results " + \
                             "*********************************"
                            )
            self.logger.info("Number test run: %s", results.testsRun)
            if results.wasSuccessful() is True:
                self.logger.info("Test was successful\n")
            else:
                rtn |= 1
                self.logger.info("Test failed")
                self.logger.info("\nNumber test errors: %d", \
                                 len(results.errors))
                for error_item in results.errors:
                    self.logger.info(error_item[0])
                    self.logger.info(error_item[1])
                self.logger.info("\nNumber test failures: %d",
                                 len(results.failures))
                for results_item in results.failures:
                    self.logger.info(results_item[0])
                    self.logger.info(results_item[1])
                    test_object_dict = results_item[0].__dict__
                    self.dump_error_messages(
                        test_object_dict['_testMethodName'])

            use_valgrind = os.getenv('TR_USE_VALGRIND', "")
            if use_valgrind == "memcheck" and \
               str(self.test_directives.get('checkXml', "no")).lower() == "yes":
                self.valgrind_memcheck()
            elif use_valgrind == "callgrind":
                self.callgrind_annotate()

            self.logger.info(
                "***********************************************************")
            if 'setEnvVars' in testrun:
                self.resetenv(testrun)

        return rtn

    def execute_strategy(self):
        """ execute test strategy """

        info = {}
        rtn = 0
        info['name'] = self.test_info['module']['name']
        value = self.log_dir_base + "/" + str(info['name']) + ".log"
        self.file_hdlr = logging.FileHandler(value)
        self.file_hdlr.setLevel(logging.DEBUG)
        self.logger.addHandler(self.file_hdlr)
        self.logger.info("***************** " + \
                         str(info['name']) + \
                         " *********************************"
                        )
        loop = str(self.test_directives.get('loop', "no"))
        start_time = time()
        if loop.lower() == "no":
            self.loop_number = 0
            rtn = self.execute_list()
        else:
            for i in range(int(loop)):
                self.logger.info("***************" + \
                                 str(" loop %d " % i) +\
                                 "*************************"
                                )
                self.loop_number = i
                rtn |= self.execute_list()
                toexit = self.test_directives.get('exitLoopOnError', "yes")
                if rtn and toexit.lower() == "yes":
                    break
        info['duration'] = time() - start_time
        info['return_code'] = rtn
        if rtn == 0:
            info['status'] = "PASS"
        else:
            info['status'] = "FAIL"
        info['error'] = ""
        self.file_hdlr.close()
        self.logger.removeHandler(self.file_hdlr)
        return info

    def load_testcases(self, test_module_name):
        """ load and check test description file """

        rtn = 0
        with open(test_module_name, 'r') as fd:
            self.test_info = load(fd, Loader=Loader)

        if 'description' not in self.test_info:
            self.logger.info(" No description defined in file: %s", \
                             test_module_name)
            rtn = 1
        if 'defaultENV' not in self.test_info or \
           self.test_info['defaultENV'] is None:
            self.test_info['defaultENV'] = {}
        if 'module' not in self.test_info:
            self.logger.info(" No module section defined in file: %s", \
                             test_module_name)
            rtn = 1
        if 'directives' not in self.test_info or \
           self.test_info['directives'] is None:
            self.test_info['directives'] = {}
        if 'execStrategy' not in self.test_info:
            self.logger.info(" No execStrategy section defined in file: %s",
                             test_module_name)
            rtn = 1
        return rtn

    def run_testcases(self):
        """ execute test scripts """

        sys.path.append("scripts")
        rtn = 0
        self.logger.info(
            "\n*************************************************************")
        for test_module_name in self.test_list:
            self.loop_name = os.path.splitext(
                os.path.basename(test_module_name))[0]
            self.test_info.clear()
            if self.load_testcases(test_module_name):
                rtn = 1
                continue
            self.test_directives = self.test_info.get('directives', {})
            self.add_default_env()
            self.setup_default_env()
            rtn_info = self.execute_strategy()
            rtn |= rtn_info['return_code']
            self.subtest_results.append(rtn_info)
            self.dump_test_info()
        self.post_run()
        return rtn
