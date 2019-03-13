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
CART Incast Variables test

Usage:

Execute from the install/$arch/TESTING directory.

python3 test_runner scripts/cart_test_iv.yml

from cart_echo_test.py. Same usage as that test.
"""

import os
import sys
import math
import time
import json
import struct
import codecs
import commontestsuite
import tempfile
from socket import gethostname

def _check_value(expected_value, received_value):
    """
        Checks that the received value (a hex string) contains the expected
        value (a string). If the received value is longer than the expected
        value, make sure any remaining characters are zeros

        returns True if the values match, False otherwise
    """
    char = None
    # Comparisons are lower case
    received_value = received_value.lower()

    # Convert the expected value to hex characters
    expected_value_hex = "".join("{:02x}".format(ord(c)) \
                                 for c in expected_value).lower()

    # Make sure received value is at least as long as expected
    if len(received_value) < len(expected_value_hex):
        return False

    # Make sure received value starts with the expected value
    if expected_value_hex not in received_value[:len(expected_value_hex)]:
        return False

    # Make sure all characters after the expected value are zeros (if any)
    for char in received_value[len(expected_value_hex):]:
        if char != "0":
            return False

    return True

def _check_key(key_rank, key_idx, received_key_hex):
    """
        Checks that the received key is the same as the sent key
        key_rank and key_idx are 32-bit integers
        received_key_hex is hex(key_rank|key_idx)
    """

    if len(received_key_hex) != 16:
        return False

    rank = struct.unpack("<I",
                         codecs.decode(received_key_hex[:8], "hex"))[0]
    idx = struct.unpack("<I",
                        codecs.decode(received_key_hex[8:], "hex"))[0]

    return (rank == key_rank) and (idx == key_idx)

class TestIncastVariables(commontestsuite.CommonTestSuite):
    """ Execute process set tests """
    def setUp(self):
        """setup the test"""
        self.get_test_info()
        log_mask = os.getenv("D_LOG_MASK", "INFO")
        log_file = self.get_cart_long_log_name()
        crt_phy_addr = os.getenv("CRT_PHY_ADDR_STR", "ofi+sockets")
        ofi_interface = os.getenv("OFI_INTERFACE", "eth0")
        allow_singleton = os.getenv("CRT_ALLOW_SINGLETON", "1")
        ofi_share_addr = os.getenv("CRT_CTX_SHARE_ADDR", "0")
        ofi_ctx_num = os.getenv("CRT_CTX_NUM", "0")
        self.pass_env = ' -x D_LOG_MASK={!s} -x D_LOG_FILE={!s}' \
                        ' -x CRT_PHY_ADDR_STR={!s}' \
                        ' -x OFI_INTERFACE={!s} ' \
                        ' -x CRT_ALLOW_SINGLETON={!s} ' \
                        ' -x CRT_CTX_SHARE_ADDR={!s}' \
                        ' -x CRT_CTX_NUM={!s}'.format(
                            log_mask, log_file, crt_phy_addr, ofi_interface,
                            allow_singleton, ofi_share_addr,
                            ofi_ctx_num)


    def tearDown(self):
        """tear down the test"""
        self.logger.info("tearDown begin")
        self.logger.info("tearDown end\n")

    def _verify_action(self, action):
        """verify the action"""
        if (('operation' not in action) or
                ('rank' not in action) or
                ('key' not in action)):
            self.logger.error("Error happened during action check")
            raise ValueError("Each action must contain an operation," \
                             " rank, and key")

        if len(action['key']) != 2:
            self.logger.error("Error key should be tuple of (rank, idx)")
            raise ValueError("key should be a tuple of (rank, idx)")

    def _verify_fetch_operation(self, action):
        """verify fetch operation"""
        if (('return_code' not in action) or
                ('expected_value' not in action)):
            self.logger.error("Error: fetch operation was malformed")
            raise ValueError("Fetch operation malformed")

    def _iv_test_actions(self, testmsg, cli_host, actions):
        #pylint: disable=too-many-locals
        """Go through each action and perform the test"""
        for action in actions:
            command = 'tests/iv_client'

            self._verify_action(action)

            operation = action['operation']
            rank = int(action['rank'])
            key_rank = int(action['key'][0])
            key_idx = int(action['key'][1])

            if "fetch" in operation:
                self._verify_fetch_operation(action)
                expected_rc = int(action['return_code'])

                # Create a temporary file for iv_client to write the results to
                log_fd, log_path = tempfile.mkstemp()

                command = "{!s} -o '{!s}' -r '{!s}' -k '{!s}:{!s}' -l '{!s}'" \
                    .format(command, operation, rank, key_rank, key_idx,
                            log_path)

                cli_rtn = self.launch_test(testmsg, '1', self.pass_env,
                                           cli=cli_host, cli_arg=command)
                if cli_rtn != 0:
                    raise ValueError('Error code {!s} running command "{!s}"' \
                        .format(cli_rtn, command))

                # Read the result into test_result and remove the temp file
                log_file = open(log_path)
                test_result = json.load(log_file)
                log_file.close()
                os.close(log_fd)
                os.remove(log_path)

                # Parse return code and make sure it matches
                if expected_rc != test_result["return_code"]:
                    raise ValueError("Fetch returned return code {!s} != " \
                                     "expected value {!s}".format(
                                         test_result["return_code"],
                                         expected_rc))

                # Other values will be invalid if return code is failure
                if expected_rc != 0:
                    continue

                # Check that returned key matches expected one
                if not _check_key(key_rank, key_idx, test_result["key"]):
                    raise ValueError("Fetch returned unexpected key")

                # Check that returned value matches expected one
                if not _check_value(action['expected_value'],
                                    test_result["value"]):
                    raise ValueError("Fetch returned unexpected value")

            if "update" in operation:
                if 'value' not in action:
                    raise ValueError("Update operation requires value")

                command = "{!s} -o '{!s}' -r '{!s}' -k '{!s}:{!s}' -v '{!s}'" \
                        .format(command, operation, rank, key_rank, key_idx,
                                action['value'])

                cli_rtn = self.launch_test(testmsg, '1', self.pass_env,
                                           cli=cli_host, cli_arg=command)
                if cli_rtn != 0:
                    raise ValueError('Error code {!s} running command "{!s}"' \
                            .format(cli_rtn, command))

            if "invalidate" in operation:
                command = "{!s} -o '{!s}' -r '{!s}' -k '{!s}:{!s}'".format(
                    command, operation, rank, key_rank, key_idx)

                cli_rtn = self.launch_test(testmsg, '1', self.pass_env,
                                           cli=cli_host, cli_arg=command)
                if cli_rtn != 0:
                    raise ValueError('Error code {!s} running command "{!s}"' \
                            .format(cli_rtn, command))

    def _iv_base_test(self, testmsg, min_ranks, actions):
        #pylint: disable=too-many-locals
        failed = False

        # Get at one or more servers to run the IV server on
        servers = self.get_server_list()
        if servers:
            all_servers = ','.join(servers)
            srv_hosts = ''.join([' -H ', all_servers])
            num_servers = len(servers)
        else:
            srv_hosts = ''.join([' -H ', gethostname().split('.')[0]])
            num_servers = 1

        # Make sure there are at least min_ranks to test against
        ranks_per_node = int(math.floor((min_ranks / (num_servers + 1))) + 1)

        # Get a single client to run the client utility on
        clients = self.get_client_list()
        if clients:
            cli_host = ''.join([' -H ', clients.pop(0)])
        else:
            cli_host = ''.join([' -H ', gethostname().split('.')[0]])

        # Note: --continuous option is not currently required

        # Servers should stay running even if one is shut down
        #srv_hosts = ' --continuous ' + srv_hosts
        # Clients should stay running even if one is shut down
        #cli_host = ' --continuous ' + cli_host

        ########## Launch Servers ##########

        # Launch one or more IV server instances to act as background targets.
        # These will remain running for the duration.
        proc_srv = self.launch_bg(testmsg, str(ranks_per_node), self.pass_env,
                                  srv_hosts, 'tests/iv_server -v 3')
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

        ########## Launch Client Actions ##########

        try:
            self._iv_test_actions(testmsg, cli_host, actions)
        except ValueError as exception:
            failed = True
            self.logger.error("TEST FAILED: %s", str(exception))

        ########## Shutdown Servers ##########

        # Note: due to CART-408 issue, rank 0 needs to shutdown last
        # Request each server shut down gracefully
        for rank in reversed(range(1, ranks_per_node * num_servers)):
            self.launch_test(testmsg, '1', self.pass_env, cli=cli_host,
                             cli_arg='tests/iv_client -o shutdown -r ' +
                             str(rank))

        time.sleep(1)
        # Shutdown rank 0 separately
        self.launch_test(testmsg, '1', self.pass_env, cli=cli_host,
                         cli_arg='tests/iv_client -o shutdown -r 0')

        time.sleep(2)

        # Stop the server if it is still running
        if self.check_process(proc_srv):
            # Return value is meaningless with --continuous
            self.stop_process(testmsg, proc_srv)

        return failed

    def test_iv_base(self):
        """Simple IV test"""
        testmsg = self.shortDescription()
        print("Python system version:", sys.version)

        sample_actions = [
            # Fetch, expect fail, no variable yet
            {"operation":"fetch", "rank":0, "key":(0, 42), "return_code":-1,
             "expected_value":""},
            # Add variable 0:42
            {"operation":"update", "rank":0, "key":(0, 42), "value":"potato"},
            # Fetch the value and verify it
            {"operation":"fetch", "rank":0, "key":(0, 42), "return_code":0,
             "expected_value":"potato"},
            # Invalidate the value
            {"operation":"invalidate", "rank":0, "key":(0, 42)},
            # Fetch the value again expecting failure
            {"operation":"fetch", "rank":0, "key":(0, 42), "return_code":-1,
             "expected_value":""},
        ]

        status = self._iv_base_test(testmsg, 2, sample_actions)
        if status:
            self.fail("test_iv_base failed: %d " % status)
