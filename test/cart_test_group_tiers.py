#!/usr/bin/env python3
# Copyright (C) 2016-2018 Intel Corporation
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

class TestGroup(commontestsuite.CommonTestSuite):
    """ Execute group tests with tiers"""

    def setUp(self):
        """setup the test"""
        self.get_test_info()
        log_mask = os.getenv("D_LOG_MASK", "INFO")
        log_file = self.get_cart_long_log_name()
        crt_phy_addr = os.getenv("CRT_PHY_ADDR_STR", "ofi+sockets")
        ofi_interface = os.getenv("OFI_INTERFACE", "eth0")
        self.pass_env = ' -x D_LOG_MASK={!s} -x D_LOG_FILE={!s}' \
                        ' -x CRT_PHY_ADDR_STR={!s}' \
                        ' -x OFI_INTERFACE={!s}'.format(
                            log_mask, log_file, crt_phy_addr, ofi_interface)

    def tearDown(self):
        """tear down the test"""
        self.logger.info("tearDown begin")
        self.logger.info("tearDown end\n")

    def test_group_three_nodes(self):
        """Simple process group test three nodes"""

        #Test requries atleast 2 servers and 1 client.
        servers = self.get_server_list()
        if not servers or len(servers) < 2:
            self.skipTest('requires three or more nodes.')

        client = self.get_client_list()
        if not client:
            self.skipTest('requires three or more nodes.')

        testmsg = self.shortDescription()

        srv2 = ''.join([' -H ', servers.pop(0)])
        srv2_args = 'tests/test_group' + \
            ' --name service_group_02 --is_service'

        srv1 = ''.join([' -H ', servers.pop(0)])
        srv1_args = 'tests/test_group' + \
            ' --name service_group_01 --attach_to service_group_02 ' + \
            '--is_service'

        proc_srv_02 = self.launch_bg(testmsg, '1', self.pass_env, \
                                     srv2, srv2_args)
        time.sleep(8)
        proc_srv_01 = self.launch_bg(testmsg, '1', self.pass_env, \
                                     srv1, srv1_args)

        cli_args = 'tests/test_group' + \
            ' --name client_group --attach_to service_group_01 '

        time.sleep(4)
        cli_rtn = self.launch_test(testmsg, '1', self.pass_env, \
                                   cli=''.join([' -H ', client.pop(0)]), \
                                   cli_arg=cli_args)

        srv_rtn_02 = self.stop_process(testmsg, proc_srv_02)
        srv_rtn_01 = self.stop_process(testmsg, proc_srv_01)
        if cli_rtn or srv_rtn_02 or srv_rtn_01:
            self.fail("Failed, return codes client %d " % cli_rtn +
                      "server_02 %d" % srv_rtn_02 + "server_01 %d" % srv_rtn_01)
