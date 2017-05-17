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
Singleton attach and multi-tier test

Usage:

Execute from the install/$arch/TESTING directory. The results are placed in the
testLogs/testRun/cart_test_singleton directory. Any cart_test_singleton output
<file yaml>_loop#/<module.name.execStrategy.id>/1(process set)/rank<number>.
There you will find anything written to stdout and stderr. The output from
memcheck and callgrind are in the cart_test_singleton directory.
At the end of a test run, the last testRun directory is renamed to
testRun_<date stamp>

python3 test_runner scripts/cart_test_singleton.yml

To use valgrind memory checking
set TR_USE_VALGRIND in cart_test_singleton.yml to memcheck

To use valgrind call (callgrind) profiling
set TR_USE_VALGRIND in cart_test_singleton.yml to callgrind

"""

import os
import time
import commontestsuite
import tempfile
import shutil
import logging

def tearDownModule():
    """ remove environment variables """
    logger = logging.getLogger("TestRunnerLogger")
    logger.info("tearDownModule begin")
    logger.info("tearDownModule removing environment variables")
    os.environ.pop("CRT_PHY_ADDR_STR", "")
    os.environ.pop("OFI_INTERFACE", "")
    os.environ.pop("CRT_LOG_MASK", "")
    logger.info("tearDownModule end\n")

class TestSingleton(commontestsuite.CommonTestSuite):
    """ Execute process set tests """
    tempdir = ""

    def setUp(self):
        """ setup for test """
        self.get_test_info()
        self.tempdir = tempfile.mkdtemp()
        self.log_mask = os.getenv("CRT_LOG_MASK", "INFO")
        self.crt_phy_addr = os.getenv("CRT_PHY_ADDR_STR", "ofi+sockets")
        self.ofi_interface = os.getenv("OFI_INTERFACE", "eth0")
        baseport = self.generate_port_numbers(self.ofi_interface)
        self.pass_env = ' -x CRT_LOG_MASK={!s} -x CRT_PHY_ADDR_STR={!s}' \
                        ' -x OFI_INTERFACE={!s} -x OFI_PORT={!s}'.format(
                            self.log_mask, self.crt_phy_addr,
                            self.ofi_interface, baseport)

    def tearDown(self):
        """ remove tmp directory """
        self.logger.info("tearDown begin")
        self.logger.info("tearDown removing temp directory")
        shutil.rmtree(self.tempdir)
        self.logger.info("tearDown end\n")

    def test_singleton_attach(self):
        """Singleton attach test run on one node"""
        testmsg = self.shortDescription()

        server = ""
        servers = self.get_server_list()
        if servers:
            server = ''.join([' -H ', servers.pop(0)])

        srv_args = []
        srv_args.extend(['tests/crt_echo_srv',
                         '-p', self.tempdir,
                         '-s'])

        # Launch both the client and target instances on the
        # same node.
        proc_srv = self.launch_bg(testmsg, '1', self.pass_env, \
                                  server, ' '.join(srv_args))

        if proc_srv is None:
            self.fail("Server launch failed, return code %s" \
                       % proc_srv.returncode)

        time.sleep(10)

        # Verify the server is still running.
        if not self.check_process(proc_srv):
            procrtn = self.stop_process(testmsg, proc_srv)
            self.fail("Server did not launch, return code %s" \
                       % procrtn)
        self.logger.info("Server running")

        self.logger.info("Client env CRT_PHY_ADDR_STR: %s OFI_INTERFACE %s ",
                         os.environ["CRT_PHY_ADDR_STR"],
                         os.environ["OFI_INTERFACE"])
        #os.getenv("CRT_PHY_ADDR_STR", "ofi+sockets")
        #os.getenv("OFI_INTERFACE", "eth0")
        # Launch the client without orterun
        cli_args = 'tests/crt_echo_cli' + ' -p ' + self.tempdir + ' -s '
        cli_rtn = self.execute_cmd(testmsg, cli_args)

        # Stop the server process
        srv_rtn = self.stop_process(testmsg, proc_srv)

        if cli_rtn or srv_rtn:
            self.fail("Failed, client return code: %d" % cli_rtn + \
                       "server: %d" % srv_rtn)

    def test_multi_tier_singleton_attach(self):
        """Multi_tier singleton attach test on two nodes"""
        testmsg = self.shortDescription()

        servers = self.get_server_list()
        if not servers:
            self.skipTest('Server list is empty.')
        self.logger.info("Servers: %s", servers)

        srv1 = ''.join([' -H ', servers.pop(0)])
        srv2 = ''.join([' -H ', servers.pop(0)])

        srv_args = []
        srv_args.extend(['tests/crt_echo_srv',
                         '-p', self.tempdir,
                         '-s', '-m',
                         ':',
                         self.pass_env,
                         '-N', '1',
                         srv2,
                         'tests/crt_echo_srv2'])

        # Launch a crt_echo_srv instance to act as a target in the background.
        # This will remain running for the duration.
        proc_srv = self.launch_bg(testmsg, '1', self.pass_env, \
                                  srv1, ' '.join(srv_args))

        if proc_srv is None:
            self.fail("Server launch failed, return code %s" \
                       % proc_srv.returncode)

        time.sleep(10)

        # Verify the server is still running.
        if not self.check_process(proc_srv):
            procrtn = self.stop_process(testmsg, proc_srv)
            self.fail("Server did not launch, return code %s" \
                       % procrtn)
        self.logger.info("Server running")

        # Launch the client without orterun
        cli_args = 'tests/crt_echo_cli' + ' -p ' + self.tempdir + ' -s -m'
        cli_rtn = self.execute_cmd(testmsg, cli_args)

        srv_rtn = self.stop_process(testmsg, proc_srv)
        if cli_rtn or srv_rtn:
            self.fail("Failed, client return code: %d" % cli_rtn + \
                       "server %d" % srv_rtn)

    def test_multi_tier_without_singleton_attach(self):
        """Multi_tier without singleton attach test on two nodes"""
        testmsg = self.shortDescription()

        servers = self.get_server_list()
        if not servers:
            self.skipTest('Server list is empty.')
        self.logger.info("Servers: %s", servers)

        # Launch the client and srv1 on the same node
        srv1 = client = ''.join([' -H ', servers.pop(0)])
        srv2 = ''.join([' -H ', servers.pop(0)])

        srv_args = []
        srv_args.extend(['tests/crt_echo_srv',
                         '-p', self.tempdir,
                         '-m',
                         ':',
                         self.pass_env,
                         '-N', '1',
                         srv2,
                         'tests/crt_echo_srv2'])
        # Launch a crt_echo_srv instance to act as a target in the background.
        # This will remain running for the duration.
        proc_srv = self.launch_bg(testmsg, '1', self.pass_env, \
                                  srv1, ' '.join(srv_args))

        if proc_srv is None:
            self.fail("Server launch failed, return code %s" \
                       % proc_srv.returncode)

        time.sleep(10)

        # Verify the server is still running.
        if not self.check_process(proc_srv):
            procrtn = self.stop_process(testmsg, proc_srv)
            self.fail("Server did not launch, return code %s" \
                       % procrtn)
        self.logger.info("Server running")

        # Launch the client without orterun
        cli_args = 'tests/crt_echo_cli' + ' -p ' + self.tempdir + ' -m'
        cli_rtn = self.launch_test(testmsg, '1', self.pass_env, \
                                   cli=client, cli_arg=cli_args)

        srv_rtn = self.stop_process(testmsg, proc_srv)
        if cli_rtn or srv_rtn:
            self.fail("Failed, client return code: %d" % cli_rtn + \
                       "server %d" % srv_rtn)
