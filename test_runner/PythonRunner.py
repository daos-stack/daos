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
multi test runner class

"""


import os
import logging
#pylint: disable=import-error
import PostRunner
import ResultsRunner
#pylint: enable=import-error
from importlib import import_module
from time import time

#pylint: disable=too-many-locals
#pylint: disable=broad-except

class PythonRunner(PostRunner.PostRunner):
    """Simple test runner"""
    test_info = None
    testModule = None
    lop_number = 0
    logger = None

    def __init__(self, test_info, log_base_path):
        self.test_info = test_info
        self.logdir = log_base_path
        self.logger = logging.getLogger("TestRunnerLogger")

    #def execute_testcase(self, cmd, parms, waittime=1800):
    def execute_testcase(self, cmd, parms):
        """ Launch test command """
        self.logger.info("TestRunner: start command %s ", cmd)
        cmdstr = "{!s} {!s} ".format(cmd, parms)
        rtn = 0
        self.logger.info("=======================================\n " + \
                      " Command: " + str(cmdstr) + \
                      "\n======================================\n")
        try:
            if parms:
                rtn = getattr(self.testModule, cmd)(parms)
            else:
                rtn = getattr(self.testModule, cmd)()
        except Exception as e:
            rtn = 1
            self.logger.info("=======================================\n " + \
                          " Command failed: " + str(e) + \
                          "\n======================================\n")
        else:
            self.logger.info("=======================================\n " + \
                          " Command returned: " + str(rtn) + \
                          "\n======================================\n")

        if not isinstance(rtn, int):
            rtn = 0
        return rtn

    def execute_setup(self, test):
        """ execute test strategy """
        rtn = 0
        setup_items = self.test_info.get_test_info(test['setup'])
        for item in setup_items:
            cmd = item.get('name')
            parameters = item.get('parameters', "")
            rtn |= self.execute_testcase(cmd, parameters)

        return rtn

    def execute_list(self, results):
        """ execute each item in the execution strategy list """
        rtn = 0
        toexit = self.test_info.get_test_info('directives',
                                              'exitLoopOnError', "no").lower()
        for item in self.test_info.get_test_info('execStrategy'):
            rc = 0
            info = {}
            info['name'] = item['name']
            cmd = item['name']
            parameters = item.get('parameters', "")
            start_time = time()
            #??? this call methods
            rc = self.execute_testcase(cmd, parameters)
            rtn |= rc
            info['duration'] = '{:.2f}'.format(time() - start_time)
            info['return_code'] = rc
            if rc == 0:
                info['status'] = "PASS"
            else:
                info['status'] = "FAIL"
            info['error'] = ""
            results.update_subtest_results(info)
            if rc and toexit == "yes":
                break

        return rtn

    def import_module(self):
        """ import the test module and load the class """
        _class = None
        module = self.test_info.get_module()
        name = module.get('name')
        self.logger.info("Import module: %s", name)
        try:
            _module = import_module(name)
            try:
                print("module: {!s}".format(dir(_module)))
                print("module type: {!s}".format(type(_module)))
                className = module.get('className', name)
                print("class type: {!s}".format(type(
                    getattr(_module, className))))
                self.logger.info("load class: %s", className)
                _class = getattr(_module, className)(self.test_info,
                                                     self.logdir)
                print("class: {!s}".format(dir(_class)))
                print("class type: {!s}".format(type(_class)))
            except AttributeError as e:
                self.logger.error("Class does not exist")
                self.logger.error("%s", str(e))
        except ImportError:
            self.logger.error("Module does not exist")
        return _class

    def execute_strategy(self):
        """ execute test strategy """
        rtn = 0
        rc = 0
        testname = self.test_info.get_test_info('testName')
        logName = os.path.join(self.logdir,
                               "{!s}.{!s}.test_log.{!s}.log".format(
                                   testname, testname, self.test_info.nodename))
        file_hdlr = logging.FileHandler(logName)
        self.logger.addHandler(file_hdlr)
        file_hdlr.setLevel(logging.DEBUG)
        self.logger.info("***************** " + \
                         str(testname) + \
                         " *********************************"
                        )
        #??? load module here
        self.test_info.setup_default_env()
        self.testModule = self.import_module()
        print("testModule type: {!s}".format(type(self.testModule)))
        loop = str(self.test_info.get_test_info('directives', 'loop', "no"))
        results = ResultsRunner.SubTestResults(self.logdir, testname)

        if loop.lower() == "no":
            rtn = self.execute_list(results)
        else:
            for i in range(int(loop)):
                self.logger.info("***************" + \
                                 str(" loop %d " % i) +\
                                 "*************************"
                                )
                #self.logdir = os.path.join(self.logdirbase,
                #                           ("loop{!s}".format(i)))
                #try:
                #    os.makedirs(self.logdir)
                #except OSError:
                #    pass
                results.add_test_set("{!s}_loop{!s}".format(testname, i))
                rc = self.execute_list(results)
                rtn |= rc
                if rc == 0:
                    status = "PASS"
                else:
                    status = "FAIL"
                results.update_testset_results(status=status)
                toexit = self.test_info.get_directives('exitLoopOnError', "yes")
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
