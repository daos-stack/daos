#!/usr/bin/env python3
# Copyright (C) 2017-2018 Intel Corporation
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
RPC test

Usage:

Execute from the install/$arch/TESTING directory. The results are placed in the
testLogs/testRun/cart_rpc_test directory. Any echo_test output is under
<file yaml>_loop#/<module.name.execStrategy.id>/1(process set)/rank<number>.
There you will find anything written to stdout and stderr. The output from
memcheck and callgrind are in the echo_test directory. At the end of a test run,
the last testRun directory is renamed to testRun_<date stamp>

python3 test_runner config=<path to config file>scripts/cart_rpc_test.yml

To use valgrind memory checking
set TR_USE_VALGRIND in cart_rpc_test.yml to memcheck

To use valgrind call (callgrind) profiling
set TR_USE_VALGRIND in cart_rpc_test.yml to callgrind

"""

import os
import tempfile
import shutil
import commontestsuite

class TestRpc(commontestsuite.CommonTestSuite):
    """ Execute rpc tests """
    tempdir = ""
    cli_args = ""
    srv_args = ""
    srv2_args = ""
    def setUp(self):
        """setup the test"""
        self.get_test_info()
        self.tempdir = tempfile.mkdtemp(dir=os.getenv("CRT_TESTLOG"))
        log_mask = os.getenv("D_LOG_MASK", "INFO")
        log_file = self.get_cart_long_log_name()
        crt_phy_addr = os.getenv("CRT_PHY_ADDR_STR", "ofi+sockets")
        ofi_interface = os.getenv("OFI_INTERFACE", "eth0")
        ofi_share_addr = os.getenv("CRT_CTX_SHARE_ADDR", "0")
        ofi_ctx_num = os.getenv("CRT_CTX_NUM", "0")
        self.pass_env = ' -x D_LOG_MASK={!s} -x D_LOG_FILE={!s}' \
                        ' -x CRT_PHY_ADDR_STR={!s}' \
                        ' -x OFI_INTERFACE={!s}' \
                        ' -x CRT_CTX_SHARE_ADDR={!s} -x CRT_CTX_NUM={!s}' \
                            .format(log_mask, log_file, crt_phy_addr, \
                                    ofi_interface, ofi_share_addr, ofi_ctx_num)

    def tearDown(self):
        """tear down the test"""
        self.logger.info("tearDown begin")
        self.logger.info("tearDown removing temp directory")
        shutil.rmtree(self.tempdir)
        self.logger.info("tearDown end\n")

    def test_rpc_node(self):
        """rpc test """

        if not os.getenv('TR_USE_URI', ""):
            self.skipTest('requires two or more nodes.')

        testmsg = self.shortDescription()

        clients = self.get_client_list()
        if not clients:
            self.skipTest('Client list is empty.')


        servers = self.get_server_list()
        if not servers:
            self.skipTest('Server list is empty.')

        server2 = ''.join([' -H ', servers.pop(1)])
        self.srv2_args = 'tests/rpc_test_srv2' + ' -c ' + self.tempdir

        # Launch a rpc_test_srv2 instance to act as a target in the background.
        # This will remain running for the duration.

        proc_srv2 = self.launch_bg(testmsg, '1', self.pass_env, \
                                  server2, self.srv2_args)

        if proc_srv2 is None:
            self.fail("Server launch failed, return code %s" \
                       % proc_srv2.returncode)

        # Verify the server2 is still running.
        if not self.check_process(proc_srv2):
            procrtn2 = self.stop_process(testmsg, proc_srv2)
            self.fail("Server did not launch, return code %s" \
                       % procrtn2)
        self.logger.info("Server2 running")

        server = ''.join([' -H ', servers.pop(0)])
        self.srv_args = 'tests/rpc_test_srv' + ' -c ' + self.tempdir

        # Launch a rpc_test_srv instance to act as a target in the background.
        # This will remain running for the duration.
        self.logger.info("server:=%s", server)
        proc_srv = self.launch_bg(testmsg, '1', self.pass_env, \
                                  server, self.srv_args)

        if proc_srv is None:
            self.fail("Server launch failed, return code %s" \
                       % proc_srv.returncode)

        # Verify the server is still running.
        if not self.check_process(proc_srv):
            procrtn = self.stop_process(testmsg, proc_srv)
            self.fail("Server did not launch, return code %s" \
                       % procrtn)
        self.logger.info("Server1 running")

        # Launch, and wait for the test itself.  This is where the actual
        # code gets run.  Launch and keep the return code, but do not check it
        # until after the target has been stopped.
        self.cli_args = 'tests/rpc_test_cli'+ ' -c ' + self.tempdir + \
                        ' -t ' + str(5)
        cli_rtn = self.launch_test(testmsg, '1', self.pass_env, \
                                   cli=''.join([' -H ', clients.pop(0)]), \
                                   cli_arg=self.cli_args)

        # Stop the server.
        srv_rtn = self.stop_process(testmsg, proc_srv)
        srv_rtn2 = self.stop_process(testmsg, proc_srv2)

        if cli_rtn or srv_rtn or srv_rtn2:
            self.fail("Failed, return codes client %d " % cli_rtn + \
                       "server %d " % srv_rtn + "server2 %d" % srv_rtn2)
