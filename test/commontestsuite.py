#!/usr/bin/env python3
# Copyright (C) 2016-2017 Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted for any purpose (including commercial purposes)
# provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions, and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions, and the following disclaimer in the
#    documentation and/or materials provided with the distribution.
#
# 3. In addition, redistributions of modified forms of the source or binary
#    code must carry prominent notices stating that the original code was
#    changed and the date of the change.
#
#  4. All publications or advertising materials mentioning features or use of
#     this software are asked, but not required, to acknowledge that it was
#     developed by Intel Corporation and credit the contributors.
#
# 5. Neither the name of Intel Corporation, nor the name of any Contributor
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# -*- coding: utf-8 -*-
"""
common setUp, teardown and other modules for the testsuites.

Usage:

The common module is imported and executed by the cart test suites.
The module contains the supporting functions for the tests:
setUp, tearDown, create the input/command string and set the log dir.

The results will be placed in the testLogs/testRun directory. There
you will find anything written to stdout and stderr. The output from memcheck
and callgrind are in the test_group directory. At the end of a test run, the
last testRun directory is renamed to testRun_<date stamp>

To use valgrind memory checking
set TR_USE_VALGRIND in cart_echo_test.yml to memcheck

To use valgrind call (callgrind) profiling
set TR_USE_VALGRIND in cart_echo_test.yml to callgrind

To redirect output to the log file,
set TR_REDIRECT_OUTPUT in the respective yaml files.
The test_runner will set it to redirect the output to the log file.
By default the output is displayed on the screen.

"""

import os
import unittest
import subprocess
import shlex
import time
import getpass
import logging

#pylint: disable=broad-except

def commonTearDownModule(suitetitle, testprocess):
    """teardown module for test"""
    print("%s: module tearDown begin" % suitetitle)
    testmsg = "terminate any %s processes" % testprocess
    cmdstr = "pkill %s" % testprocess
    ctsobj = CommonTestSuite()
    ctsobj.common_launch_test(suitetitle, testmsg, cmdstr)
    print("%s: module tearDown end\n\n" % suitetitle)

class CommonTestSuite(unittest.TestCase):
    """Contains the attributes common to the CART tests"""
    logger = logging.getLogger("TestRunnerLogger")

    def common_launch_test(self, suitetitle, msg, cmdstr):
        """Launch process set test"""
        self.logger.info("%s: start %s - input string:\n%s", \
          suitetitle, msg, cmdstr)
        cmdarg = shlex.split(cmdstr)
        start_time = time.time()
        if not os.getenv('TR_REDIRECT_OUTPUT', ""):
            procrtn = subprocess.call(cmdarg, timeout=180)
        else:
            procrtn = subprocess.call(cmdarg, timeout=180,
                                      stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL)
        elapsed = time.time() - start_time
        self.logger.info("%s: %s - return code: %d test duration: %d\n", \
          suitetitle, msg, procrtn, elapsed)
        return procrtn

    def common_launch_process(self, suitetitle, msg, cmdstr):
        """Launch process set """
        self.logger.info("%s: start process %s - input string:\n%s", \
          suitetitle, msg, cmdstr)
        cmdarg = shlex.split(cmdstr)
        if not os.getenv('TR_REDIRECT_OUTPUT', ""):
            proc = subprocess.Popen(cmdarg)
        else:
            proc = subprocess.Popen(cmdarg,
                                    stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL)
        return proc

    def common_stop_process(self, suitetitle, msg, proc):
        """ wait for process to terminate """
        self.logger.info("%s: %s - stopping processes :%s", \
          suitetitle, msg, proc.pid)
        i = 60
        procrtn = None
        while i:
            proc.poll()
            procrtn = proc.returncode
            if procrtn is not None:
                break
            else:
                time.sleep(1)
                i = i - 1

        if procrtn is None:
            self.logger.info("%s: Again stopping processes :%s", \
              suitetitle, proc.pid)
            procrtn = -1
            try:
                proc.terminate()
                proc.wait(2)
            except ProcessLookupError:
                pass
            except Exception:
                self.logger.info("%s: killing processes :%s", \
                  suitetitle, proc.pid)
                proc.kill()

        self.logger.info("%s: %s - return code: %d\n", \
          suitetitle, msg, procrtn)
        return procrtn

    def common_stop_process_now(self, suitetitle, msg, proc):
        """ wait for process to terminate """
        self.logger.info("%s: %s - terminating processes :%s", \
          suitetitle, msg, proc.pid)

        proc.poll()
        procrtn = proc.returncode

        if procrtn is None:
            self.logger.info("%s: stopping processes :%s", \
              suitetitle, proc.pid)
            procrtn = -1
            try:
                proc.terminate()
                proc.wait(2)
            except ProcessLookupError:
                pass
            except Exception:
                self.logger.info("%s: killing processes :%s", \
                  suitetitle, proc.pid)
                proc.kill()

        self.logger.info("%s: %s - return code: %d\n", \
          suitetitle, msg, procrtn)
        return procrtn

    @staticmethod
    def common_logdir_name(fullname):
        """create the log directory name"""
        names = fullname.split('.')
        items = names[-1].split('_', maxsplit=2)
        return items[2]

    def common_add_prefix_logdir(self, testprocess):
        """add the log directory to the prefix"""
        testcase_id = self.id()
        prefix = ""
        ompi_bin = os.getenv('CRT_OMPI_BIN', "")
        log_path = os.path.join(os.getenv("CRT_TESTLOG", testprocess),
                                self.common_logdir_name(testcase_id))
        os.makedirs(log_path, exist_ok=True)
        use_valgrind = os.getenv('TR_USE_VALGRIND', default="")
        if use_valgrind == 'memcheck':
            suppressfile = os.path.join(os.getenv('CRT_PREFIX', ".."), "etc", \
                           "memcheck-cart.supp")
            prefix = "valgrind --xml=yes" + \
                " --xml-file=" + log_path + "/valgrind.%q{PMIX_ID}.xml" + \
                " --leak-check=yes --gen-suppressions=all" + \
                " --suppressions=" + suppressfile + " --show-reachable=yes"
        elif use_valgrind == "callgrind":
            prefix = "valgrind --tool=callgrind --callgrind-out-file=" + \
                     log_path + "/callgrind.%q{PMIX_ID}.out"

        if os.getenv('TR_USE_URI', ""):
            dvmfile = " --hnp file:%s " % os.getenv('TR_USE_URI')
        else:
            dvmfile = " "
        if getpass.getuser() == "root":
            allow_root = " --allow-run-as-root"
        else:
            allow_root = ""
        cmdstr = "%sorterun%s--output-filename %s%s" % \
                 (ompi_bin, dvmfile, log_path, allow_root)

        return (cmdstr, prefix)

    @staticmethod
    def common_add_server_client():
        """create the server and client prefix"""
        server = os.getenv('CRT_TEST_SERVER')
        if server:
            local_server = " -H %s " % server
        else:
            local_server = " "
        client = os.getenv('CRT_TEST_CLIENT')
        if client:
            local_client = " -H %s " % client
        else:
            local_client = " "

        return (local_server, local_client)

    @staticmethod
    def common_get_server_list():
        """obtain the list of server nodes"""
        servers = os.getenv('CRT_TEST_SERVER')
        if servers:
            return servers.split(',')
        else:
            return []

    @staticmethod
    def common_get_client_list():
        """obtain the list of client nodes"""
        clients = os.getenv('CRT_TEST_CLIENT')
        if clients:
            return clients.split(',')
        else:
            return []
