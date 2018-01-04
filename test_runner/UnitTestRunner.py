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
test runner class

"""


import os
import unittest
import logging
from time import time
#pylint: disable=import-error
import PostRunner
import GrindRunner
import ResultsRunner
#pylint: enable=import-error


class UnitTestRunner(PostRunner.PostRunner,
                     GrindRunner.GrindRunner):
    """Simple test runner"""
    log_dir_base = ""
    last_testlogdir = ""
    loop_number = 0
    test_info = {}
    subtest_results = []
    info = None
    logger = None

    def __init__(self, test_info, log_base_path):
        self.test_info = test_info
        self.info = test_info.info
        self.test_directives = test_info.get_directives()
        self.log_dir_base = log_base_path
        self.logger = logging.getLogger("TestRunnerLogger")

    def setenv(self, testcase):
        """ setup testcase environment """
        module_env = testcase.get('setEnvVars')
        if module_env != None:
            for (key, value) in module_env.items():
                os.environ[str(key)] = value
        setTestPhase = testcase.get('type', self.test_info.get_defaultENV(
            'TR_TEST_PHASE', "TEST")).upper()
        os.environ['TR_TEST_PHASE'] = setTestPhase

    def resetenv(self, testcase):
        """ reset testcase environment """
        module_env = testcase.get('setEnvVars')
        module_default_env = self.test_info.get_test_info('defaultENV')
        if module_env != None:
            for key in module_env.keys():
                value = module_default_env.get(key, "")
                os.environ[str(key)] = value

    def settestlog(self, testcase_id):
        """ setup testcase environment """
        test_module = self.test_info.get_module()
        test_name = test_module['name']
        value = os.path.join(self.log_dir_base,
                             ("loop{!s}".format(self.loop_number)),
                             ("{0}_{1!s}".format(test_name, testcase_id)))
        os.environ[test_module['subLogKey']] = value
        try:
            os.makedirs(value)
        except OSError:
            pass
        self.last_testlogdir = value

    def execute_list(self):
        """ execute test scripts """

        rtn = 0
        test_module = self.test_info.get_module()
        for testrun in self.test_info.get_execStrategy():
            self.logger.info("************ run %s *******", testrun['id'])
            self.setenv(testrun)
            self.settestlog(testrun['id'])
            TestLoader = unittest.TestLoader()
            if 'testMethodPrefix' in testrun:
                TestLoader.testMethodPrefix = testrun.get('testMethodPrefix',
                                                          "test")
                self.logger.info("testMethodPrefix %s",
                                 TestLoader.testMethodPrefix)
            suite = TestLoader.loadTestsFromName(test_module['name'])
            results = unittest.TestResult()
            suite.run(results)

            self.logger.info("***************** Results " + \
                             "*********************************"
                            )
            self.logger.info("Number test run: %s", results.testsRun)
            self.logger.info("Number skipped tests: %d",
                             len(results.skipped))
            for results_item in results.skipped:
                self.logger.info(results_item[0])
                self.logger.info(results_item[1])
            if results.wasSuccessful() is True:
                self.logger.info("\nTest was successful\n")
            else:
                rtn |= 1
                self.logger.info("Test failed")
                self.logger.info("\nNumber test errors: %d", \
                                 len(results.errors))
                for error_item in results.errors:
                    self.logger.info(error_item[0])
                    self.logger.info(error_item[1])
                    test_object_dict = error_item[0].__dict__
                    self.dump_log_files(error_item[0].__class__.__name__,
                                        test_object_dict['_testMethodName'])
                self.logger.info("\nNumber test failures: %d",
                                 len(results.failures))
                for results_item in results.failures:
                    self.logger.info(results_item[0])
                    self.logger.info(results_item[1])
                    test_object_dict = results_item[0].__dict__
                    self.dump_log_files(results_item[0].__class__.__name__,
                                        test_object_dict['_testMethodName'])

            use_valgrind = os.getenv('TR_USE_VALGRIND', "")
            if use_valgrind == "memcheck" and \
               str(self.test_directives.get('checkXml', "no")).lower() == "yes":
                self.valgrind_memcheck()
            elif use_valgrind == "callgrind":
                self.callgrind_annotate()

            PostRunner.check_log_mode(self.last_testlogdir)
            self.logger.info(
                "***********************************************************")
            self.resetenv(testrun)

        return rtn

#pylint: disable=too-many-statements
    def execute_strategy(self):
        """ execute test strategy """

        file_hdlr = None
        rtn = 0
        testsetname = self.test_info.get_test_info('testSetName')
        testname = self.test_info.get_test_info('testName')
        testlog = "{!s}.{!s}.test_log.{!s}.log".format(testsetname, testname,
                                                       self.test_info.nodename
                                                      )
        results = ResultsRunner.SubTestResults(self.log_dir_base, testsetname)
        file_hdlr = logging.FileHandler(os.path.join(self.log_dir_base,
                                                     testlog))
        file_hdlr.setLevel(logging.DEBUG)
        self.logger.addHandler(file_hdlr)
        self.logger.info("***************** %s ***************", testname)
        self.test_info.setup_default_env()
        loop = str(self.test_directives.get('loop', "no"))
        start_time = time()
        if loop.lower() == "no":
            results_info = {}
            self.loop_number = 0
            start_time = time()
            rtn = self.execute_list()
            results_info['name'] = testname
            results_info['duration'] = '{:.2f}'.format(time() - start_time)
            results_info['return_code'] = rtn
            if rtn == 0:
                results_info['status'] = "PASS"
            else:
                results_info['status'] = "FAIL"
            results_info['error'] = ""
            results.update_subtest_results(results_info)
        else:
            for i in range(1, int(loop) + 1):
                results_info = {}
                self.logger.info("*************** loop %d ****************", i)
                self.loop_number = i
                results.add_test_set("{!s}_loop{!s}".format(testsetname, i))
                start_time = time()
                rtn |= self.execute_list()
                results_info['name'] = testname
                results_info['duration'] = '{:.2f}'.format(time() - start_time)
                results_info['return_code'] = rtn
                if rtn == 0:
                    results_info['status'] = "PASS"
                else:
                    results_info['status'] = "FAIL"
                results_info['error'] = ""
                results.update_subtest_results(results_info)
                results.update_testset_results(status=results_info['status'])
                toexit = self.test_directives.get('exitLoopOnError', "yes")
                if rtn and toexit.lower() == "yes":
                    break
        if rtn == 0:
            status = "PASS"
        else:
            status = "FAIL"
        results.update_testset_zero(status=status)
        file_hdlr.close()
        self.logger.removeHandler(file_hdlr)
        return (rtn, results)
