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
cart group test

Usage:

Execute from the install/$arch/TESTING directory. The results are placed in the
testLogs/testRun/test_group directory. Any test_group output is under <file
yaml>_loop#/<module.name.execStrategy.id>/1(process group)/rank<number>.  There
you will find anything written to stdout and stderr. The output from memcheck
and callgrind are in the test_group directory. At the end of a test run, the
last testRun directory is renamed to testRun_<date stamp>

python3 test_runner scripts/cart_test_group_tiers.yml

To use valgrind memory checking
set TR_USE_VALGRIND in cart_test_group_tiers.yml to memcheck

To use valgrind call (callgrind) profiling
set TR_USE_VALGRIND in cart_test_group_tiers.yml to callgrind

"""

import os
import time
import commontestsuite

NPROC = "1"
testsuite = "Test Group Tiers"
testprocess = "test_group_tiers"

def tearDownModule():
    """teardown module for test"""
    commontestsuite.commonTearDownModule(testsuite, testprocess)

class TestGroup(commontestsuite.CommonTestSuite):
    """ Execute group tests with tiers"""
    pass_env = " -x CCI_CONFIG -x CRT_LOG_MASK "

    def test_group_three_nodes(self):
        """Simple process group test three nodes"""

        if not os.getenv('TR_USE_URI', ""):
            self.skipTest('requires three or more nodes.')

        testmsg = self.shortDescription()
        servers = self.common_get_server_list()
        if not servers or len(servers) < 2:
            self.skipTest('requires three or more nodes.')
        clients = self.common_get_client_list()
        if not clients or len(clients) < 1:
            self.skipTest('requires three or more nodes.')

        (cmd, prefix) = self.common_add_prefix_logdir(testprocess)
        cmdstr = cmd + \
          " -H %s -n %s %s%s tests/test_group " % \
          (servers.pop(0), NPROC, self.pass_env, prefix) + \
          "--name service_group_02 --is_service --holdtime 10"
        proc_srv_02 = self.common_launch_process(testsuite, testmsg, cmdstr)
        time.sleep(3)

        (cmd, prefix) = self.common_add_prefix_logdir(testprocess)
        cmdstr = cmd + \
          " -H %s -n %s %s%s tests/test_group " % \
          (servers.pop(0), NPROC, self.pass_env, prefix) + \
          "--name service_group_01 --attach_to service_group_02 " + \
          "--is_service --holdtime 8"
        proc_srv_01 = self.common_launch_process(testsuite, testmsg, cmdstr)

        (cmd, prefix) = self.common_add_prefix_logdir(testprocess)
        cmdstr = cmd + \
          " -H %s -n %s %s%s tests/test_group " % \
          (clients.pop(), NPROC, self.pass_env, prefix) + \
          "--name client_group --attach_to service_group_01"
        cli_rtn = self.common_launch_test(testsuite, testmsg, cmdstr)

        srv_rtn_02 = self.common_stop_process(testsuite, testmsg, proc_srv_02)
        srv_rtn_01 = self.common_stop_process(testsuite, testmsg, proc_srv_01)
        if cli_rtn or srv_rtn_02 or srv_rtn_01:
            self.fail("Failed, return codes client %d " % cli_rtn +
                      "server_02 %d" % srv_rtn_02 + "server_01 %d" % srv_rtn_01)
