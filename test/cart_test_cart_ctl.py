#!/usr/bin/env python3
# Copyright (C) 2018-2019 Intel Corporation
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
cart_ctl test

Usage:

Execute from the install/$arch/TESTING directory.

python3 test_runner scripts/cart_test_cart_ctl.yml

To use valgrind memory checking
set TR_USE_VALGRIND in cart_test_cart_ctl.yml to memcheck

To use valgrind call (callgrind) profiling
set TR_USE_VALGRIND in cart_test_cart_ctl.yml to callgrind

"""

import os
import time
import commontestsuite

class TestCartCtl(commontestsuite.CommonTestSuite):
    """ Execute cart_ctl tests """

    def setUp(self):
        """setup the test"""
        self.get_test_info()
        log_mask = os.getenv("D_LOG_MASK", "DEBUG,MEM=ERR")
        log_file = self.get_cart_long_log_name()
        fault_config = os.getenv("D_FI_CONFIG")
        if not fault_config:
            fault_config = os.path.join(os.getenv('CRT_PREFIX', ".."), "etc", \
                           "fault-inject-cart.yaml")
        crt_phy_addr = os.getenv("CRT_PHY_ADDR_STR", "ofi+sockets")
        ofi_interface = os.getenv("OFI_INTERFACE", "eth0")
        ofi_share_addr = os.getenv("CRT_CTX_SHARE_ADDR", "0")
        ofi_ctx_num = os.getenv("CRT_CTX_NUM", "0")
        self.pass_env = ' -x D_LOG_MASK={!s} -x D_LOG_FILE={!s}' \
                        ' -x CRT_PHY_ADDR_STR={!s}' \
                        ' -x OFI_INTERFACE={!s}' \
                        ' -x CRT_CTX_SHARE_ADDR={!s}' \
                        ' -x CRT_CTX_NUM={!s}' \
                        ' -x D_FI_CONFIG={!s}'.format(
                            log_mask, log_file, crt_phy_addr,
                            ofi_interface, ofi_share_addr, ofi_ctx_num,
                            fault_config)

    def tearDown(self):
        """tear down the test"""
        self.logger.info("tearDown begin")
        self.logger.info("tearDown end\n")

    def test_cart_ctl_five_nodes(self):
        """cart_ctl test five nodes"""

        if not os.getenv('TR_USE_URI', ""):
            self.skipTest('requires DVM to run.')

        testmsg = self.shortDescription()

        hosts = self.get_server_list()
        if len(hosts) < 5:
            self.skipTest("requires at least 5 nodes.")

        # Launch a cart_ctl instance to act as a target in the
        # background.  This will remain running for the duration.
        client = ''.join([' -H ', hosts[0]])
        servers = ','.join(hosts)
        servers = ''.join([' -H ', servers])
        srv_arg = "tests/test_group --name service-group --is_service"
        server_proc = self.launch_bg(testmsg, '1', self.pass_env, \
                                     servers, srv_arg)
        if server_proc is None:
            self.fail("Server launch failed, return code %s" \
                       % server_proc.returncode)

        time.sleep(2)

        # Verify the server is still running.
        if not self.check_process(server_proc):
            procrtn = self.stop_process(testmsg, server_proc)
            self.fail("Server did not launch, return code %s" \
                       % procrtn)
        self.logger.info("Server running")
        procrtn = self.launch_test(testmsg, '1', self.pass_env, \
                                   cli=client, \
                                   cli_arg='../bin/cart_ctl list_ctx' + \
                                           ' --group-name service-group' + \
                                           ' --rank 0,2-3,4')

        self.stop_process(testmsg, server_proc)
        if procrtn:
            self.fail("Failed, return codes server %d" % procrtn)


    def test_cart_ctl_one_node(self):
        """cart_ctl test one node"""

        testmsg = self.shortDescription()

        client = ""
        server = ""
        hosts = self.get_server_list()

        # Launch a test_group instance to act as a target in the
        # background.  This will remain running for the duration.
        if hosts:
            client = ''.join([' -H ', hosts[0]])
            server = ''.join([' -H ', hosts[0]])
        srv_arg = "tests/test_group --name service-group --is_service"
        server_proc = self.launch_bg(testmsg, '1', self.pass_env, \
                                     server, srv_arg)
        if server_proc is None:
            self.fail("Server launch failed, return code %s" \
                       % server_proc.returncode)

        time.sleep(2)

        # Verify the server is still running.
        if not self.check_process(server_proc):
            procrtn = self.stop_process(testmsg, server_proc)
            self.fail("Server did not launch, return code %s" % procrtn)
        self.logger.info("Server running")
        procrtn = self.launch_test(testmsg, '1', self.pass_env, \
                                   cli=client, \
                                   cli_arg='../bin/cart_ctl list_ctx' + \
                                           ' --group-name service-group' + \
                                           ' --rank 0')
        if procrtn:
            self.fail("Failed, return codes %d" % procrtn)

        procrtn = self.launch_test(testmsg, '1', self.pass_env, \
                                   cli=client, \
                                   cli_arg='../bin/cart_ctl enable_fi' + \
                                           ' --group-name service-group' + \
                                           ' --rank 0')
        if procrtn:
            self.fail("crt_ctl enable_fi failed, return codes %d"
                      % procrtn)

        procrtn = self.launch_test(testmsg, '1', self.pass_env, \
                                   cli=client, \
                                   cli_arg='../bin/cart_ctl set_fi_attr' + \
                                           ' --attr 1911,5,0,1,100' + \
                                           ' --group-name service-group' + \
                                           ' --rank 0')
        if procrtn:
            self.fail("crt_ctl enable_fi failed, return codes %d"
                      % procrtn)

        procrtn = self.launch_test(testmsg, '1', self.pass_env, \
                                   cli=client, \
                                   cli_arg='../bin/cart_ctl disable_fi' + \
                                           ' --group-name service-group' + \
                                           ' --rank 0')
        if procrtn:
            self.fail("cart_ctl disable_fi failed, return codes %d"
                      % procrtn)

        # notify the server to shutdown
        procrtn = self.launch_test(testmsg, '1', self.pass_env, \
                                   cli=client, \
                                   cli_arg='tests/test_group' + \
                                           ' --name client-group' + \
                                           ' --attach_to service-group' + \
                                           ' --shut_only')
        if procrtn:
            self.fail("cart_ctl disable_fi failed, return codes %d"
                      % procrtn)
        self.stop_process(testmsg, server_proc)
