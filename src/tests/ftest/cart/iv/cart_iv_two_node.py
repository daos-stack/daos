#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from __future__ import print_function

import sys
import time
import tempfile
import json
import os
import struct
import codecs
import subprocess
import shlex

from apricot  import TestWithoutServers

sys.path.append('./util')

# Can't all this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartUtils

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

class CartIvTwoNodeTest(TestWithoutServers):
    """
    Runs basic CaRT tests on one-node

    :avocado: recursive
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def tearDown(self):
        """ Tear down """
        super(CartIvTwoNodeTest, self).tearDown()
        self.utils.cleanup_processes()

    def _verify_action(self, action):
        """verify the action"""
        if (('operation' not in action) or
                ('rank' not in action) or
                ('key' not in action)):
            self.utils.print("Error happened during action check")
            raise ValueError("Each action must contain an operation," \
                             " rank, and key")

        if len(action['key']) != 2:
            self.utils.print("Error key should be tuple of (rank, idx)")
            raise ValueError("key should be a tuple of (rank, idx)")

    def _verify_fetch_operation(self, action):
        """verify fetch operation"""
        if (('return_code' not in action) or
                ('expected_value' not in action)):
            self.utils.print("Error: fetch operation was malformed")
            raise ValueError("Fetch operation malformed")

    def _iv_test_actions(self, cmd, actions):
        #pylint: disable=too-many-locals
        """Go through each action and perform the test"""
        for action in actions:
            clicmd = cmd
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
                log_path_dir = os.environ['HOME']
                if os.environ['DAOS_TEST_SHARED_DIR']:
                    log_path_dir = os.environ['DAOS_TEST_SHARED_DIR']

                log_fd, log_path = tempfile.mkstemp(dir=log_path_dir)

                command = " {!s} -o '{!s}' -r '{!s}' -k '{!s}:{!s}' -l '{!s}'" \
                    .format(command, operation, rank, key_rank, key_idx,
                            log_path)
                clicmd += command

                self.utils.print("\nClient cmd : %s\n" % clicmd)
                cli_rtn = subprocess.call(shlex.split(clicmd))

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

                command = " {!s} -o '{!s}' -r '{!s}' -k '{!s}:{!s}' -v '{!s}'" \
                        .format(command, operation, rank, key_rank, key_idx,
                                action['value'])
                clicmd += command

                self.utils.print("\nClient cmd : %s\n" % clicmd)
                cli_rtn = subprocess.call(shlex.split(clicmd))

                if cli_rtn != 0:
                    raise ValueError('Error code {!s} running command "{!s}"' \
                            .format(cli_rtn, command))

            if "invalidate" in operation:
                command = " {!s} -o '{!s}' -r '{!s}' -k '{!s}:{!s}'".format(
                    command, operation, rank, key_rank, key_idx)
                clicmd += command

                self.utils.print("\nClient cmd : %s\n" % clicmd)
                cli_rtn = subprocess.call(shlex.split(clicmd))

                if cli_rtn != 0:
                    raise ValueError('Error code {!s} running command "{!s}"' \
                            .format(cli_rtn, command))

    def test_cart_iv(self):
        """
        Test CaRT IV

        :avocado: tags=all,cart,pr,daily_regression,iv,two_node
        """

        srvcmd = self.utils.build_cmd(self, self.env, "test_servers")

        try:
            srv_rtn = self.utils.launch_cmd_bg(self, srvcmd)
        # pylint: disable=broad-except
        except Exception as e:
            self.utils.print("Exception in launching server : {}".format(e))
            self.fail("Test failed.\n")

        # Verify the server is still running.
        if not self.utils.check_process(srv_rtn):
            procrtn = self.utils.stop_process(srv_rtn)
            self.fail("Server did not launch, return code %s" \
                       % procrtn)

        actions = [
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

        time.sleep(2)

        failed = False

        clicmd = self.utils.build_cmd(self, self.env, "test_clients")

        ########## Launch Client Actions ##########

        try:
            self._iv_test_actions(clicmd, actions)
        except ValueError as exception:
            failed = True
            self.utils.print("TEST FAILED: %s" % str(exception))

        ########## Shutdown Servers ##########

        num_servers = self.utils.get_srv_cnt(self, "test_servers")

        srv_ppn = self.params.get("test_servers_ppn", '/run/tests/*/')

        # Note: due to CART-408 issue, rank 0 needs to shutdown last
        # Request each server shut down gracefully
        for rank in reversed(range(1, int(srv_ppn) * num_servers)):
            clicmd += " -o shutdown -r " + str(rank)
            self.utils.print("\nClient cmd : %s\n" % clicmd)
            try:
                subprocess.call(shlex.split(clicmd))
            # pylint: disable=broad-except
            except Exception as e:
                failed = True
                self.utils.print("Exception in launching client : {}".format(e))

        time.sleep(1)

        # Shutdown rank 0 separately
        clicmd += " -o shutdown -r 0"
        self.utils.print("\nClient cmd : %s\n" % clicmd)
        try:
            subprocess.call(shlex.split(clicmd))
        # pylint: disable=broad-except
        except Exception as e:
            failed = True
            self.utils.print("Exception in launching client : {}".format(e))

        time.sleep(2)

        # Stop the server if it is still running
        if self.utils.check_process(srv_rtn):
            # Return value is meaningless with --continuous
            self.utils.stop_process(srv_rtn)

        if failed:
            self.fail("Test failed.\n")


if __name__ == "__main__":
    main()
