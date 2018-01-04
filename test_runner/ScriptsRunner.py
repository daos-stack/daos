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
import subprocess
import logging
from time import time
#pylint: disable=import-error
import PostRunner
import ResultsRunner
#pylint: enable=import-error

#pylint: disable=too-many-locals
#pylint: disable=too-many-statements

class ScriptsRunner(PostRunner.PostRunner):
    """Simple test runner"""
    subtest_results = []
    info = None
    test_info = None
    logger = None

    def __init__(self, test_info, log_base_path):
        self.test_info = test_info
        self.info = test_info.info
        self.logdir = log_base_path
        self.logdirbase = log_base_path
        self.globalTimeout = self.test_info.get_test_info(
            'directives', 'globalTimeout', '1800')
        self.logger = logging.getLogger("TestRunnerLogger")

    def execute_testcase(self, cmd, parms, logname, waittime=1800):
        """ Launch test command """
        self.logger.info("TestRunner: start command %s ", cmd)
        testcaseout = os.path.join(self.logdir, logname)
        cmdstr = "{!s} {!s}".format(cmd, parms)
        rtn = 0
        #cmdarg = shlex.split(cmdstr)
        with open(testcaseout, mode='a') as outfile:
            outfile.write("=======================================\n " + \
                          " Command: " + str(cmdstr) + \
                          "\n======================================\n")
            outfile.flush()
            try:
                #                      universal_newlines=True,
                subprocess.check_call(cmdstr, timeout=waittime, shell=True,
                                      stdin=subprocess.DEVNULL,
                                      stdout=outfile,
                                      stderr=subprocess.STDOUT)
            except subprocess.CalledProcessError as e:
                rtn = e.returncode
                outfile.write("=======================================\n " + \
                              " Command failed: " + str(rtn) + \
                              "\n======================================\n")
            except subprocess.TimeoutExpired:
                rtn = 1
                outfile.write("=======================================\n " + \
                              " Command time out" + \
                              "\n======================================\n")
            else:
                outfile.write("=======================================\n " + \
                              " Command returned: " + str(rtn) + \
                              "\n======================================\n")
            finally:
                outfile.flush()

        if not isinstance(rtn, int):
            rtn = 0
        return rtn

    def execute_skip(self, results, item):
        """ skip a test """
        skip = item['skipIf']
        if skip == 'hasVMs':
            if 'vm' not in \
                self.test_info.get_test_info('subList', 'hostlist', ""):
                return False
        info = {}
        info['name'] = item['name']
        info['duration'] = '0'
        info['return_code'] = 0
        info['status'] = "SKIP"
        info['error'] = ""
        results.update_subtest_results(info)
        return True

    def execute_setup(self, test, module, logname):
        """ execute test strategy """
        rtn = 0
        path = module.get('path', "")
        setup_items = self.test_info.get_test_info(test['setup'])
        for item in setup_items:
            rc = 0
            if item.get('type', "exe") == 'shell':
                cmd = item.get('exe', item['name']) + ".sh"
                cmdstr = os.path.join(path, cmd)
            else:
                cmdstr = self.test_info.setup_parameters(
                    item.get('exe', item['name']))
            parameters = self.test_info.setup_parameters(
                item.get('parameters', ""))
            waittime = int(item.get('waittime', self.globalTimeout))
            rc = self.execute_testcase(cmdstr, parameters, logname, waittime)
            rtn |= rc
            if rc:
                self.logger.info("TestRunner: setup %s for %s Failed",
                                 item['name'], test['name'])
        if rtn and test.get('setupFailsTestcase',
                            self.test_info.get_directives('setupFailsTestcase',
                                                          "yes")).lower() == \
                   "yes":
            return rtn
        return 0

    def execute_list(self, results):
        """ execute each item in the execution strategy list """
        module = {}
        rtn = 0
        module = self.test_info.get_module()
        baseName = module.get('logBaseName', module['name'])
        if baseName != results.test_set_name():
            baseName = results.test_set_name()
        path = module.get('path', "")
        toexit = self.test_info.get_directives('exitListOnError', "no").lower()
        for item in self.test_info.get_test_info('execStrategy'):
            rc = 0
            info = {}
            if 'skipIf' in item and self.execute_skip(results, item):
                continue
            info['name'] = item['name']
            log_name = "{baseName}.{subTest}.{logType}.{hostName}.log".format(
                baseName=baseName,
                subTest=info['name'],
                logType=module.get('logType', "testlog"),
                hostName=self.test_info.nodename)

            if item.get('setup'):
                start_time = time()
                rtc = self.execute_setup(item, module, log_name)
                if rtc:
                    info['duration'] = '{:.2f}'.format(time() - start_time)
                    info['return_code'] = rtc
                    info['status'] = "FAIL"
                    info['error'] = "testcase setup failed"
                    rtn |= rtc
                    results.update_subtest_results(info)
                    if toexit == "no":
                        continue
                    else:
                        break
            if item.get('type', "exe") == 'shell':
                cmd = item.get('exe', item['name']) + ".sh"
                cmdstr = os.path.join(path, cmd)
            else:
                cmdstr = self.test_info.setup_parameters(
                    item.get('exe', item['name']))
            parameters = self.test_info.setup_parameters(
                item.get('parameters', ""))
            waittime = int(item.get('waittime', self.globalTimeout))
            start_time = time()
            rc = self.execute_testcase(cmdstr, parameters, log_name, waittime)
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

    def execute_strategy(self):
        """ execute test strategy """
        rtn = 0
        rc = 0
        #module = self.test_info.get_module()
        testsetname = self.test_info.get_test_info('testSetName')
        testname = self.test_info.get_test_info('testName')
        logName = os.path.join(self.logdir,
                               "{!s}.{!s}.test_log.{!s}.log".format(
                                   testsetname, testname,
                                   self.test_info.nodename))
        file_hdlr = logging.FileHandler(logName)
        self.logger.addHandler(file_hdlr)
        file_hdlr.setLevel(logging.DEBUG)
        self.logger.info("***************** %s ***************", testname)
        self.test_info.setup_default_env()
        loop = str(self.test_info.get_directives('loop', "no"))
        results = ResultsRunner.SubTestResults(self.logdir, testsetname)
        if loop.lower() == "no":
            rtn = self.execute_list(results)
        else:
            toexit = self.test_info.get_directives('exitLoopOnError', "yes")
            for i in range(1, int(loop) + 1):
                self.logger.info("*************** loop %d ****************", i)
                self.logdir = os.path.join(self.logdirbase,
                                           ("loop{!s}".format(i)))
                try:
                    os.makedirs(self.logdir)
                except OSError:
                    pass
                results.add_test_set("{!s}_loop{!s}".format(testsetname, i))
                rc = self.execute_list(results)
                rtn |= rc
                if rc == 0:
                    status = "PASS"
                else:
                    status = "FAIL"
                results.update_testset_results(status=status)
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
