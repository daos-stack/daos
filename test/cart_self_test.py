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
cart self test

"""

import os
import time
import commontestsuite

class SelfTest(commontestsuite.CommonTestSuite):
    """ Execute self_test tests """

    def setUp(self):
        """setup the test"""
        self.get_test_info()
        log_mask = os.getenv("D_LOG_MASK", "WARN")
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

    def test_self_test(self):
        """Run self test over two nodes"""

        # Ensure that DVM is running, as this test requires launching two jobs
        # under the same environment.
        if not os.getenv('TR_USE_URI', ""):
            self.skipTest('requires DVM to run.')

        testmsg = self.shortDescription()

        self_test_dir = os.getenv("CRT_PREFIX_BIN", None)
        if self_test_dir:
            self_test_binary = os.path.join(self_test_dir, 'self_test')
        else:
            self_test_binary = 'self_test'

        servers = self.get_server_list()
        if not servers:
            self.skipTest('Server list is empty.')

        client = self.get_client_list()
        if not client:
            self.skipTest('Client list is empty.')

        # First launch a test_group instance to act as a target for the self
        # test.  This doesn't need to do anything other than be there and
        # remain running for the duration.
        srv_args = "tests/test_group" + \
            " --name target --hold --is_service"
        server = ''.join([' -H ', servers.pop(0)])
        server_proc = self.launch_bg(testmsg, '1', self.pass_env, \
                                     server, srv_args)

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

        # Now define the self_test options.
        message_sizes = "b2000,b2000 0,0 b2000,b2000 i1000,i1000 b2000," + \
            "i1000,i1000 0,0 i1000,1,0"

        rpcs_in_flight = 16
        repetitions = 100

        client_args = [self_test_binary]
        client_args.extend(['--group-name', 'target',
                            '--endpoint', '0:0',
                            '--message-sizes', message_sizes,
                            '--max-inflight-rpcs', str(rpcs_in_flight),
                            '--repetitions', str(repetitions)])

        # Launch, and wait for the self-test itself.  This is where the actual
        # code gets run.  Launch and keep the return code, but do not check it
        # until after the target has been stopped.
        procrtn = self.launch_test(testmsg, '1', self.pass_env, \
                                   cli=''.join([' -H ', client.pop(0)]), \
                                   cli_arg=' '.join(client_args))

        # Stop the server.  This will normally run forever because of the hold
        # option, so allow stop_process() to kill it but do not check the
        # return code.
        self.stop_process(testmsg, server_proc)

        if procrtn:
            self.fail("Self test failed with %d" % procrtn)
