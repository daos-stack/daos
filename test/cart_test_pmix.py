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
pmix test

Usage:

Execute from the install/$arch/TESTING directory.

To use valgrind memory checking
set TR_USE_VALGRIND in cart_test_pmix.yml to memcheck

To use valgrind call (callgrind) profiling
set TR_USE_VALGRIND in cart_test_pmix.yml to callgrind

"""

import os
import commontestsuite

class TestPMIx(commontestsuite.CommonTestSuite):
    """ Execute pmix tests """
    servers = []
    clients = []

    def setUp(self):
        """setup the test"""
        self.get_test_info()
        log_mask = os.getenv("D_LOG_MASK", "INFO")
        self.pass_env = ' -x D_LOG_MASK={!s}' \
                        .format(log_mask)

    def tearDown(self):
        """tear down the test"""
        self.logger.info("tearDown begin")
        self.logger.info("tearDown end\n")

    def test_pmix_one_node(self):
        """pmix test one node"""

        if os.getenv('TR_USE_VALGRIND', ""):
            self.skipTest('Skip valgrind test')

        testmsg = self.shortDescription()
        clients = self.get_client_list()
        if clients:
            self.skipTest('Client list is not empty.')
        servers = self.get_server_list()
        if servers:
            hosts = ' -H ' + servers.pop(0)
        else:
            hosts = ''

        # Launch two processes on the same node.
        (cmd, prefix) = self.add_prefix_logdir()
        cmdstr = "{!s} {!s} -N 2 {!s} {!s} {!s}".format(
            cmd, hosts, self.pass_env, prefix, 'tests/test_pmix')

        srv_rtn = self.execute_cmd(testmsg, cmdstr)

        if srv_rtn:
            self.fail("PMIx test Failed, return code %d" % srv_rtn)
        return srv_rtn

    def test_pmix_multi_node(self):
        """pmix test multi node"""

        if os.getenv('TR_USE_VALGRIND', ""):
            self.skipTest('Skip valgrind test')

        if not os.getenv('TR_USE_URI', ""):
            self.skipTest('requires DVM to run.')

        testmsg = self.shortDescription()

        servers = self.get_server_list()
        if not servers:
            self.fail("Failed, server list is empty.")

        all_servers = ','.join(servers)
        hosts = ''.join([' -H ', all_servers])
        # Launch one process on each node
        (cmd, prefix) = self.add_prefix_logdir()
        cmdstr = "{!s} {!s} -N 1 {!s} {!s} {!s}".format(
            cmd, hosts, self.pass_env, prefix, 'tests/test_pmix')

        srv_rtn = self.execute_cmd(testmsg, cmdstr)

        if srv_rtn:
            self.fail("Failed, return code %d " % srv_rtn)
