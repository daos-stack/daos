'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time
import tempfile
import json
import os
import struct
import codecs
import subprocess  # nosec
import shlex
import traceback

from cart_utils import CartTest


def _check_value(expected_value, received_value):
    """Check that the received value contains the expected value.

    Checks that the received value (a hex string) contains the expected value
    (a string). If the received value is longer than the expected value, make
    sure any remaining characters are zeros.

    Args:
        expected_value (str): the expected hex string
        received_value (str): the string to verify

    Returns:
        bool: True if the values match, False otherwise

    """
    char = None
    # Comparisons are lower case
    received_value = received_value.lower()

    # Convert the expected value to hex characters
    expected_value_hex = "".join(
        "{:02x}".format(ord(c)) for c in expected_value).lower()

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
    """Check that the received key is the same as the sent key.

    Args:
        key_rank (int): 32-bit integer
        key_idx (int): 32-bit integer
        received_key_hex (int): hex(key_rank|key_idx)

    Returns:
        bool: is the received key is the same as the sent key

    """
    if len(received_key_hex) != 16:
        return False

    rank = struct.unpack("<I",
                         codecs.decode(received_key_hex[:8], "hex"))[0]
    idx = struct.unpack("<I",
                        codecs.decode(received_key_hex[8:], "hex"))[0]

    return (rank == key_rank) and (idx == key_idx)


class CartIvOneNodeTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run basic CaRT tests on one-node.

    :avocado: recursive
    """

    def _verify_action(self, action):
        """Verify the action."""
        if (('operation' not in action) or ('rank' not in action) or ('key' not in action)):
            self.print("Error happened during action check")
            raise ValueError(
                "Each action must contain an operation, rank, and key")

        if len(action['key']) != 2:
            self.print("Error key should be tuple of (rank, idx)")
            raise ValueError("key should be a tuple of (rank, idx)")

    def _verify_fetch_operation(self, action):
        """Verify the fetch operation."""
        if (('return_code' not in action) or ('expected_value' not in action)):
            self.print("Error: fetch operation was malformed")
            raise ValueError("Fetch operation malformed")

    def _iv_test_actions(self, cmd, actions):
        # pylint: disable=too-many-locals
        """Go through each action and perform the test."""
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

                # try writing to an unwritable spot
                # log_path = "/"

                command = " {!s} -o '{!s}' -r '{!s}' -k '{!s}:{!s}' -l '{!s}'" \
                    .format(command, operation, rank, key_rank, key_idx,
                            log_path)
                clicmd += command

                self.print("\nClient cmd : %s\n" % clicmd)
                cli_rtn = subprocess.call(shlex.split(clicmd))

                if cli_rtn != 0:
                    raise ValueError(
                        'Error code {!s} running command "{!s}"'.format(
                            cli_rtn, command))

                # Try to induce "No JSON object could be decoded" error
                #
                # 1.
                # with open(log_path, "a") as my_file:
                # my_file.write("some-invalid-junk-appended-to-json")
                #
                # 2.
                # codecs.open(log_file, "w", "unicode").write('')

                # DEBUGGING: dump contents of JSON file to screen
                with open(log_path, 'r') as file:
                    print(file.read())

                # Read the result into test_result and remove the temp file
                with open(log_path, 'r') as log_file:
                    test_result = json.load(log_file)

                os.close(log_fd)
                os.remove(log_path)

                # Parse return code and make sure it matches
                if expected_rc != test_result["return_code"]:
                    raise ValueError(
                        "Fetch returned return code {!s} != expected value "
                        "{!s}".format(test_result["return_code"], expected_rc))

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

                command = \
                    " {!s} -o '{!s}' -r '{!s}' -k '{!s}:{!s}' -v '{!s}'".format(
                        command, operation, rank, key_rank, key_idx,
                        action['value'])
                if 'sync' in action:
                    command = "{!s} -s '{!s}'".format(command, action['sync'])
                if 'sync' not in action:
                    command = "{!s} -s '{!s}'".format(command, "none")

                clicmd += command

                self.print("\nClient cmd : %s\n" % clicmd)
                cli_rtn = subprocess.call(shlex.split(clicmd))

                if cli_rtn != 0:
                    raise ValueError(
                        'Error code {!s} running command "{!s}"'.format(
                            cli_rtn, command))

            if "invalidate" in operation:
                command = " {!s} -o '{!s}' -r '{!s}' -k '{!s}:{!s}' ".format(
                    command, operation, rank, key_rank, key_idx)
                if 'sync' in action:
                    command = "{!s} -s '{!s}'".format(command, action['sync'])
                if 'sync' not in action:
                    command = "{!s} -s '{!s}'".format(command, "none")
                clicmd += command

                self.print("\nClient cmd : %s\n" % clicmd)
                cli_rtn = subprocess.call(shlex.split(clicmd))

                if cli_rtn != 0:
                    raise ValueError(
                        'Error code {!s} running command "{!s}"'.format(
                            cli_rtn, command))

            if "set_grp_version" in operation:
                command = " {!s} -o '{!s}' -r '{!s}' -v '{!s}' "\
                    .format(command, operation, rank, action['version'])

                if 'time' in action:
                    command = " {!s} -m '{!s}'"\
                        .format(command, action['time'])
                if 'time' not in action:
                    command = " {!s} -m '{!s}'"\
                        .format(command, 0)
                clicmd += command

                self.print("\nClient cmd : %s\n" % clicmd)
                cli_rtn = subprocess.call(shlex.split(clicmd))

                if cli_rtn != 0:
                    raise ValueError(
                        'Error code {!s} running command "{!s}"'.format(
                            cli_rtn, command))

            if "get_grp_version" in operation:
                command = " {!s} -o '{!s}' -r '{!s}' ".format(command, operation, rank)
                clicmd += command

                self.print("\nClient cmd : %s\n" % clicmd)
                cli_rtn = subprocess.call(shlex.split(clicmd))

                if cli_rtn != 0:
                    raise ValueError(
                        'Error code {!s} running command "{!s}"'.format(
                            cli_rtn, command))

    def test_cart_iv_one_node(self):
        """Test CaRT IV.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=cart,iv,one_node,memcheck
        :avocado: tags=CartIvOneNodeTest,test_cart_iv_one_node
        """
        srvcmd = self.build_cmd(self.env, "test_servers")

        try:
            srv_rtn = self.launch_cmd_bg(srvcmd)
        # pylint: disable=broad-except
        except Exception as error:
            self.print("Exception in launching server : {}".format(error))
            self.fail("Test failed.\n")

        # Verify the server is still running.
        if not self.check_process(srv_rtn):
            procrtn = self.stop_process(srv_rtn)
            self.fail("Server did not launch, return code {}".format(procrtn))

        actions = [
            # ******************
            # Fetch, to expect fail, no variable yet
            # Make sure everything goes to the top rank
            {"operation": "fetch", "rank": 0, "key": (0, 42), "return_code": -1,
             "expected_value": ""},
            {"operation": "fetch", "rank": 1, "key": (0, 42), "return_code": -1,
             "expected_value": ""},
            {"operation": "fetch", "rank": 4, "key": (0, 42), "return_code": -1,
             "expected_value": ""},
            #
            # ****
            # Add variable 0:42
            {"operation": "update", "rank": 0, "key": (0, 42),
             "value": "potato"},
            #
            # ****
            # Fetch the value from each server and verify it
            {"operation": "fetch", "rank": 0, "key": (0, 42), "return_code": 0,
             "expected_value": "potato"},
            {"operation": "fetch", "rank": 1, "key": (0, 42), "return_code": 0,
             "expected_value": "potato"},
            {"operation": "fetch", "rank": 2, "key": (0, 42), "return_code": 0,
             "expected_value": "potato"},
            {"operation": "fetch", "rank": 3, "key": (0, 42), "return_code": 0,
             "expected_value": "potato"},
            {"operation": "fetch", "rank": 4, "key": (0, 42), "return_code": 0,
             "expected_value": "potato"},
            #
            # ******************
            # Invalidate the value
            {"operation": "invalidate", "rank": 0, "key": (0, 42),
             "sync": "eager_update"},
            #
            # ****
            # Fetch the value again from each server, expecting failure
            # Reverse order of fetch just in case.
            {"operation": "fetch", "rank": 4, "key": (0, 42), "return_code": -1,
             "expected_value": ""},
            {"operation": "fetch", "rank": 3, "key": (0, 42), "return_code": -1,
             "expected_value": ""},
            {"operation": "fetch", "rank": 2, "key": (0, 42), "return_code": -1,
             "expected_value": ""},
            {"operation": "fetch", "rank": 1, "key": (0, 42), "return_code": -1,
             "expected_value": ""},
            {"operation": "fetch", "rank": 0, "key": (0, 42), "return_code": -1,
             "expected_value": ""},
            #
            ######################
            # Testing version number conflicts.
            ######################
            # Test of version skew on fetch between rank 0 and rank 1.
            # Make sure we can set version numbers.
            {"operation": "set_grp_version", "rank": 0, "key": (0, 42),
             "version": "0xdeadc0de", "return_code": 0, "expected_value": ""},
            {"operation": "get_grp_version", "rank": 0, "key": (0, 42),
             "return_code": 0, "expected_value": "0xdeadc0de"},
            {"operation": "set_grp_version", "rank": 0, "key": (0, 42),
             "version": "", "return_code": 0, "expected_value": ""},
            #
            # ******************
            # Test of version skew on fetch between rank 0 and rank 1.
            # From parent to child and from child to parent
            # Don't setup a iv variable.
            # Modify version number on root 0.
            # Do fetch in both direction for and test for failure.
            # First, do test for normal failure.
            {"operation": "fetch", "rank": 0, "key": (1, 42), "return_code": -1,
             "expected_value": ""},
            {"operation": "set_grp_version", "rank": 0, "key": (0, 42),
             "version": "0xdeadc0de", "return_code": 0, "expected_value": ""},

            {"operation": "fetch", "rank": 0, "key": (1, 42),
             "return_code": -1036, "expected_value": ""},
            {"operation": "fetch", "rank": 1, "key": (0, 42),
             "return_code": -1036, "expected_value": ""},
            {"operation": "set_grp_version", "rank": 0, "key": (0, 42),
             "version": "0x0", "return_code": 0, "expected_value": ""},
            {"operation": "invalidate", "rank": 1, "key": (1, 42)},
            #
            # ******************
            # Test of version skew on fetch between rank 0 and rank 1.
            # Create iv variable on rank 1.
            # Fetch from rank 0.
            # Change version on rank 0 while request in flight,
            # Not an error:
            #   Used for testing to ensure we do not break something
            #   that should work.
            #   Version change occurs in iv_test_fetch_iv
            # Need to invalidate on both nodes, stale data.
            {"operation": "update", "rank": 1, "key": (1, 42),
             "value": "beans"},
            {"operation": "set_grp_version", "rank": 0, "key": (0, 42),
             "time": 1,
             "version": "0xc001c001", "return_code": 0, "expected_value": ""},
            {"operation": "fetch", "rank": 0, "key": (1, 42),
             "return_code": 0, "expected_value": "beans"},
            {"operation": "set_grp_version", "rank": 0, "key": (1, 42),
             "version": "0", "return_code": 0, "expected_value": ""},
            {"operation": "invalidate", "rank": 1, "key": (1, 42)},
            {"operation": "invalidate", "rank": 0, "key": (1, 42)},
            #
            # ******************
            # Test of version skew on fetch between rank 0 and rank 1.
            # From parent to child.
            # Create a iv variable on second server  (child).
            # Setup second server to change version after it receives
            #   the rpc request.
            # Fetch variable from the first server.
            # Tests version-check in crt_hdlr_iv_fetch_aux.
            # Version change in iv_pre_fetch
            #
            {"operation": "update", "rank": 1, "key": (1, 42),
             "value": "carrot"},
            {"operation": "set_grp_version", "rank": 1, "key": (1, 42),
             "time": 2, "version": "0xdeadc0de", "return_code": 0,
             "expected_value": ""},
            {"operation": "fetch", "rank": 0, "key": (1, 42),
             "return_code": -1036, "expected_value": ""},
            {"operation": "set_grp_version", "rank": 1, "key": (1, 42),
             "version": "0x0", "return_code": 0, "expected_value": ""},
            {"operation": "invalidate", "rank": 1, "key": (1, 42)},
            #
            # ******************
            # Test invalidate with synchronization.
            # First create an iv value from rank 0 to rank 4.
            # Then verify that all ranks can see it.
            # Then remove it and verify that no ranks has a local copy
            # Need to know that this works prior to changing version
            # Verifies eager_notify works with invalidate.
            #
            {"operation": "update", "rank": 0, "key": (4, 42),
             "value": "turnip"},
            {"operation": "fetch", "rank": 1, "key": (4, 42),
             "return_code": 0, "expected_value": "turnip"},
            {"operation": "fetch", "rank": 0, "key": (4, 42),
             "return_code": 0, "expected_value": "turnip"},
            {"operation": "fetch", "rank": 3, "key": (4, 42),
             "return_code": 0, "expected_value": "turnip"},
            {"operation": "fetch", "rank": 2, "key": (4, 42),
             "return_code": 0, "expected_value": "turnip"},
            {"operation": "fetch", "rank": 4, "key": (4, 42),
             "return_code": 0, "expected_value": "turnip"},
            #
            {"operation": "invalidate", "rank": 4, "key": (4, 42),
             "sync": "eager_notify", "return_code": 0},
            #
            # Check for stale state.
            {"operation": "fetch", "rank": 4, "key": (4, 42),
             "return_code": -1, "expected_value": ""},
            {"operation": "fetch", "rank": 1, "key": (4, 42),
             "return_code": -1, "expected_value": ""},
            {"operation": "fetch", "rank": 0, "key": (4, 42),
             "return_code": -1, "expected_value": ""},
            {"operation": "fetch", "rank": 2, "key": (4, 42),
             "return_code": -1, "expected_value": ""},
            {"operation": "fetch", "rank": 3, "key": (4, 42),
             "return_code": -1, "expected_value": ""},
            #
            # ******************
            # Test of version skew on update with synchronization
            #   when version on child process is different
            # Change version on rank 4
            # Create iv variable on rank 0 using sync.
            #   Should return error and no iv variable created.
            # Make sure nothing is left behind on other nodes.
            #
            {"operation": "set_grp_version", "rank": 4, "key": (4, 42),
             "version": "0xdeadc0de", "return_code": 0, "expected_value": ""},
            {"operation": "update", "rank": 0, "key": (0, 42), "value": "beans",
             "sync": "eager_update", "return_code": -1036},
            # Even though previous failure, leaves stale state on ranks
            {"operation": "fetch", "rank": 0, "key": (0, 42),
             "return_code": 0, "expected_value": "beans"},
            {"operation": "fetch", "rank": 1, "key": (0, 42),
             "return_code": 0, "expected_value": "beans"},
            {"operation": "fetch", "rank": 4, "key": (0, 42),
             "return_code": -1036, "expected_value": ""},
            #
            # Clean up. Make sure no stale state left
            {"operation": "invalidate", "rank": 0, "key": (0, 42),
             "sync": "eager_notify", "return_code": 0},
            {"operation": "fetch", "rank": 0, "key": (0, 42),
             "return_code": -1, "expected_value": ""},
            {"operation": "set_grp_version", "rank": 4, "key": (4, 42),
             "version": "0x0", "return_code": 0, "expected_value": ""},
            {"operation": "fetch", "rank": 1, "key": (0, 42),
             "return_code": -1, "expected_value": ""},

        ]

        # wait for servers to come up.
        time.sleep(4)

        failed = False

        clicmd = self.build_cmd(self.env, "test_clients")

        # Launch Client Actions

        try:
            self._iv_test_actions(clicmd, actions)
        except ValueError as exception:
            failed = True
            traceback.print_stack()
            self.print("TEST FAILED: %s" % str(exception))

        # Shutdown Servers

        num_servers = 1

        srv_ppn = self.params.get("test_servers_ppn", '/run/tests/*/')

        # Note: due to CART-408 issue, rank 0 needs to shutdown last
        # Request each server shut down gracefully
        for rank in reversed(list(range(1, int(srv_ppn) * num_servers))):
            clicmdt = clicmd + " -o shutdown -r " + str(rank)
            self.print("\nClient cmd : {}\n".format(clicmdt))
            try:
                subprocess.call(shlex.split(clicmdt))
            # pylint: disable=broad-except
            except Exception as error:
                failed = True
                self.print("Exception in launching client : {}".format(error))

        time.sleep(1)

        # Shutdown rank 0 separately
        clicmd += " -o shutdown -r 0"
        self.print("\nClient cmd : {}\n".format(clicmd))
        try:
            subprocess.call(shlex.split(clicmd))
        # pylint: disable=broad-except
        except Exception as error:
            failed = True
            self.print("Exception in launching client : {}".format(error))

        # wait for servers to finish shutting down
        time.sleep(2)

        # Stop the server if it is still running
        if self.check_process(srv_rtn):
            # Return value is meaningless with --continuous
            self.stop_process(srv_rtn)

        if failed:
            self.fail("Test failed.\n")
