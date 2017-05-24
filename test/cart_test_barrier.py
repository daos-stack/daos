#!/usr/bin/env python3
# Copyright (C) 2017 Intel Corporation
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
cart barrier test

Usage:

Execute from the install/$arch/TESTING directory.

python3 test_runner srcipts/cart_test_barrier.yml

To use valgrind memory checking
set TR_USE_VALGRIND in cart_test_barrier.yml to memcheck

To use valgrind call (callgrind) profiling
set TR_USE_VALGRIND in cart_test_barrier.yml to callgrind

"""

import os
import time
import commontestsuite

class TestBarrier(commontestsuite.CommonTestSuite):
    """ Execute process set tests """

    def setUp(self):
        """setup the test"""
        self.get_test_info()
        log_mask = os.getenv("CRT_LOG_MASK", "INFO")
        crt_phy_addr = os.getenv("CRT_PHY_ADDR_STR", "ofi+sockets")
        ofi_interface = os.getenv("OFI_INTERFACE", "eth0")
        baseport = self.generate_port_numbers(ofi_interface)
        self.pass_env = ' -x CRT_LOG_MASK={!s} -x CRT_PHY_ADDR_STR={!s}' \
                        ' -x OFI_INTERFACE={!s} -x OFI_PORT={!s}'.format(
                            log_mask, crt_phy_addr, ofi_interface, baseport)

    def tearDown(self):
        """tear down the test"""
        self.logger.info("tearDown begin")
        os.environ.pop("CRT_PHY_ADDR_STR", "")
        os.environ.pop("OFI_INTERFACE", "")
        os.environ.pop("CRT_LOG_MASK", "")
        self.logger.info("tearDown end\n")

    def test_barrier_test(self):
        """Simple barrier test"""
        testmsg = self.shortDescription()

        servers = self.get_server_list()
        if not servers:
            self.skipTest('Server list is empty.')

        all_servers = ','.join(servers)
        hosts = ''.join([' -H ', all_servers])

        # Launch a test_crt_barrier in the background.
        # This will remain running for the duration.
        proc_srv = self.launch_bg(testmsg, '1', self.pass_env, \
                                  hosts, 'tests/test_crt_barrier')

        if proc_srv is None:
            self.fail("Server launch failed, return code %s" \
                       % proc_srv.returncode)

        time.sleep(2)

        # Verify the server is still running.
        if not self.check_process(proc_srv):
            procrtn = self.stop_process(testmsg, proc_srv)
            self.fail("Server did not launch, return code %s" \
                       % procrtn)
        self.logger.info("Server running")

        # Stop the server.  This will normally run forever because of the hold
        # option, so allow stop_process() to kill it.
        srv_rtn = self.stop_process(testmsg, proc_srv)

        if srv_rtn:
            self.fail("Barrier test Failed, return code %d" % srv_rtn)
        return srv_rtn
