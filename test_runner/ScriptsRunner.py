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
import time
#pylint: disable=import-error
import PostRunner
#pylint: enable=import-error

#pylint: disable=too-many-locals

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
        self.logger = logging.getLogger("TestRunnerLogger")

    def execute_testcase(self, cmd, parms, logname, waittime=1800):
        """ Launch test command """
        self.logger.info("TestRunner: start command %s ", cmd)
        testcaseout = os.path.join(self.logdir, logname)
        cmdstr = "%s %s " % (cmd, parms)
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

    def execute_setup(self, test, module, logname):
        """ execute test strategy """
        rtn = 0
        path = module.get('path', "")
        setup_items = self.test_info.get_test_info(test['setup'])
        for item in setup_items:
            if item.get('type', "exe") == 'shell':
                cmd = item.get('exe', item['name']) + ".sh"
                cmdstr = os.path.join(path, cmd)
            else:
                cmdstr = item.get('exe', item['name'])
            parameters = item.get('parameters', "")
            rtn |= self.execute_testcase(cmdstr, parameters, logname)

        return rtn

    def execute_strategy(self):
        """ execute test strategy """
        module = {}
        results = []
        rtn = 0
        module = self.test_info.get_module()
        file_hdlr = logging.FileHandler(self.logdir + "/" + \
                                        str(module['name']) + ".log")
        self.logger.addHandler(file_hdlr)
        file_hdlr.setLevel(logging.DEBUG)
        self.logger.info("***************** " + \
                         str(module['name']) + \
                         " *********************************"
                        )
        path = module.get('path', "")
        hostname = self.info.get_config('host_list')[0]
        toexit = self.test_info.get_test_info('directives',
                                              'exitLoopOnError', "no").lower()
        for item in self.test_info.get_test_info('execStrategy'):
            rc = 0
            info = {}
            info['name'] = item['name']
            log_name = "{baseName}.{subTest}.{logType}.{hostName}.log".format(
                baseName=module.get('logBaseName', module['name']),
                subTest=info['name'],
                logType=module.get('logType', "testlog"),
                hostName=hostname)

            if item.get('setup'):
                rtn |= self.execute_setup(item, module, log_name)
            if item.get('type', "exe") == 'shell':
                cmd = item.get('exe', item['name']) + ".sh"
                cmdstr = os.path.join(path, cmd)
            else:
                cmdstr = item.get('exe', item['name'])
            parameters = item.get('parameters', "")
            start_time = time.time()
            rc = self.execute_testcase(cmdstr, parameters, log_name)
            rtn |= rc
            info['duration'] = '{:.2f}'.format(time.time() - start_time)
            info['return_code'] = rc
            if rc == 0:
                info['status'] = "PASS"
            else:
                info['status'] = "FAIL"
            info['error'] = ""
            results.append(info)
            if rc and toexit == "yes":
                break

        file_hdlr.close()
        self.logger.removeHandler(file_hdlr)

        return (rtn, results)
