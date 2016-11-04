#!/usr/bin/env python3
# Copyright (C) 2016 Intel Corporation
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
mcl processs set test

Usage:

Execute from the install/$arch/TESTING directory. The results are placed in the
testLogs/testRun/echo_test directory. Any echo_test output is under
<file yaml>_loop#/<module.name.execStrategy.id>/1(process set)/rank<number>.
There you will find anything written to stdout and stderr. The output from
memcheck and callgrind are in the echo_test directory. At the end of a test run,
the last testRun directory is renamed to testRun_<date stamp>

python3 test_runner srcipts/cart_echo_test.yml

To use valgrind memory checking
set TR_USE_VALGRIND in cart_echo_test.yml to memcheck

To use valgrind call (callgrind) profiling
set TR_USE_VALGRIND in cart_echo_test.yml to callgrind

"""

import os
import commontestsuite

#pylint: disable=broad-except

NPROC = "1"
testsuite = "Test Echo"
testprocess = "crt_echo"

def setUpModule():
    """ set up test environment """
    commontestsuite.commonSetUpModule(testsuite)
    print("os.environ['CCI_CONFIG'] = %s" % os.environ.get("CCI_CONFIG"))

def tearDownModule():
    """teardown module for test"""
    commontestsuite.commonTearDownModule(testsuite, testprocess)

class TestEcho(commontestsuite.CommonTestSuite):
    """ Execute process set tests """
    pass_env = " -x PATH -x LD_LIBRARY_PATH -x CCI_CONFIG "

    def one_node_echo_test(self):
        """Simple process set test 1"""
        testmsg = self.shortDescription()
        (cmd, prefix) = self.common_add_prefix_logdir(self.id(), testprocess)
        (server, client) = self.common_add_server_client()
        cmdstr = cmd + \
          "%s-np %s %s%s tests/crt_echo_srv :" % \
          (server, NPROC, self.pass_env, prefix) + \
          "%s-np %s %s%s tests/crt_echo_cli" % \
          (client, NPROC, self.pass_env, prefix)
        procrtn = self.common_launch_test(testsuite, testmsg, cmdstr)
        return procrtn

    def two_node_echo_test(self):
        """Simple process set test 1"""
        testmsg = self.shortDescription()
        self.logger.info("test name: %s", self.id())
        (cmd, prefix) = self.common_add_prefix_logdir(self.id() + \
          "_server_node", testprocess)
        (server, client) = self.common_add_server_client()
        cmdstr = cmd + \
          "%s-np %s %s%s tests/crt_echo_srv :" % \
          (server, NPROC, self.pass_env, prefix)
        proc_srv = self.common_launch_process(testsuite, \
          testmsg, cmdstr)
        (cmd, prefix) = self.common_add_prefix_logdir(self.id() + \
          "_client_node", testprocess)
        cmdstr = cmd + \
          "%s-np %s %s%s tests/crt_echo_cli" % \
          (client, NPROC, self.pass_env, prefix)
        procrtn = self.common_launch_test(testsuite, testmsg, cmdstr)
        procrtn |= self.common_stop_process(testsuite, testmsg, proc_srv)
        return procrtn

    def test_echo_test(self):
        """Simple process set test 1"""
        if os.getenv('TR_USE_URI', ""):
            self.assertFalse(self.two_node_echo_test())
        else:
            self.assertFalse(self.one_node_echo_test())

    def setUp(self):
        """teardown module for test"""
        self.logger.info("**************************************************")
        self.logger.info("TestEcho: begin %s ", self.shortDescription())

    def tearDown(self):
        """teardown module for test"""
        self.logger.info("TestEcho: tearDown begin")
        testmsg = "terminate any crt_echo processes"
        cmdstr = "pkill crt_echo"
        self.common_launch_test(testsuite, testmsg, cmdstr)
        self.logger.info("TestEcho: end  %s", self.shortDescription())
        self.logger.info("**************************************************")
