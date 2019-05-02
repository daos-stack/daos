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

cart EP credits test

Usage:

Execute from the install/$arch/TESTING directory.

python3 test_runner scripts/cart_test_ep_credits.yml

To use valgrind memory checking
set TR_USE_VALGRIND in cart_test_ep_credits.yml to memcheck

To use valgrind call (callgrind) profiling
set TR_USE_VALGRIND in cart_test_ep_credits.yml to callgrind

"""

import os
from socket import gethostname
import time
import commontestsuite

class TestEpCredits(commontestsuite.CommonTestSuite):
    """ Execute EP credits tests """

    def setUp(self):
        """setup the test"""
        self.get_test_info()
        log_mask = os.getenv("D_LOG_MASK", "INFO")
        log_file = self.get_cart_long_log_name()
        crt_phy_addr = os.getenv("CRT_PHY_ADDR_STR", "ofi+sockets")
        ofi_interface = os.getenv("OFI_INTERFACE", "eth0")
        ofi_share_addr = os.getenv("CRT_CTX_SHARE_ADDR", "0")
        ofi_ctx_num = os.getenv("CRT_CTX_NUM", "0")
        self.pass_env = ' -x D_LOG_MASK={!s} -x D_LOG_FILE={!s}' \
                        ' -x CRT_PHY_ADDR_STR={!s}' \
                        ' -x OFI_INTERFACE={!s} -x CRT_CTX_SHARE_ADDR={!s}' \
                        ' -x CRT_CTX_NUM={!s}'.format(
                            log_mask, log_file, crt_phy_addr, ofi_interface,
                            ofi_share_addr, ofi_ctx_num)

    def tearDown(self):
        """tear down the test"""
        self.logger.info("tearDown begin")
        self.logger.info("tearDown end\n")

    def test_ep_credits(self):
        """ep credits test"""
        testmsg = self.shortDescription()

        servers = self.get_server_list()
        #run test even if server list is not provided.
        if servers:
            all_servers = ','.join(servers)
            hosts = ''.join([' -H ', all_servers])
        else:
            hosts = ''.join([' -H ', gethostname().split('.')[0]])

        srv_args = 'tests/test_ep_cred_server' + \
            ' -n cred_group -s'

        srv_proc = self.launch_bg(testmsg, '1', self.pass_env, hosts, srv_args)

        time.sleep(2)

        if srv_proc is None:
            self.fail("Failed to launch cred server; return code %s" \
                       % srv_proc.returncode)

        # Important: 0 should be last in tuple, as client shuts down server
        # for credits=0 case
        # For credits=1 case, we force 'front of queue' rpc via -f flag
        credits_to_test = (1, 5, 10, 20, 255, 0)

        for credit in credits_to_test:
            cli_cmd = 'tests/test_ep_cred_client -a cred_group -c %d -b 20' \
                       % credit
            self.logger.info(cli_cmd)

            if credit == 0:
                cli_cmd = cli_cmd + " -q"

            # Force special rpc to front of the queue via -f flag. We do
            # this for credit=1 to make sure enough of rpcs get queued up
            if credit == 1:
                cli_cmd = cli_cmd + " -f"

            cli_rtn = self.launch_test(testmsg, '1', self.pass_env,
                                       cli=hosts, cli_arg=cli_cmd)
            if cli_rtn:
                self.fail("ep credit client filed with %d" % cli_rtn)

        return cli_rtn
