#!/usr/bin/python
"""
  (C) Copyright 2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from random import choice
from re import findall

from apricot import TestWithServers
from general_utils import run_pcmd


class HarnessAdvancedTest(TestWithServers):
    """Advanced harness test cases.

    :avocado: recursive
    """

    def test_core_files(self):
        """Test to verify core file creation.

        This test will send a signal 6 to a random daos_engine process so
        that it will create a core file, allowing the core file collection code
        in launch.py to be tested.

        This test can be run in any CI stage: vm, small, medium, large

        :avocado: tags=all
        :avocado: tags=harness,harness_advanced_test,test_core_files
        """
        # Choose a server find the pid of its daos_engine process
        host = choice(self.server_managers[0].hosts)
        self.log.info("Obtaining pid of the daos_engine process on %s", host)
        pid = None
        result = run_pcmd([host], "pgrep --list-full daos_engine", 20)
        index = 0
        while not pid and index < len(result):
            output = "\n".join(result[index]["stdout"])
            match = findall(r"(\d+)\s+[A-Za-z0-9/]+", output)
            if match:
                pid = match[0]
            index += 1
        if pid is None:
            self.fail(
                "Error obtaining pid of the daos_engine process on "
                "{}".format(host))
        self.log.info("Found pid %s", pid)

        # Send a signal 6 to its daos_engine process
        self.log.info("Sending a signal 6 to %s", pid)
        result = run_pcmd([host], "sudo kill -6 {}".format(pid))
        if len(result) > 1 or result[0]["exit_status"] != 0:
            self.fail("Error sending a signal 6 to {} on {}".format(pid, host))

        # Display the journalctl log for the process that was sent the signal
        self.server_managers[0].manager.dump_logs([host])

        # Simplify resolving the host name to rank by marking all ranks as
        # expected to be either running or errored (sent a signal 6)
        self.server_managers[0].update_expected_states(
            None, ["Joined", "Errored"])

    def test_core_files_hw(self):
        """Test to verify core file creation.

        This test will send a signal 6 to a random daos_engine process so
        that it will create a core file, allowing the core file collection code
        in launch.py to be tested.

        This test can be run in any CI stage: vm, small, medium, large

        :avocado: tags=all
        :avocado: tags=hw,small,medium,ib2,large
        :avocado: tags=harness,harness_advanced_test,test_core_files
        """
        self.test_core_files()
