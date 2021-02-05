#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from random import choice

from apricot import TestWithServers
from general_utils import run_task, pcmd


class TestHarnessTests(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Collection of tests to verify the behavior of the test harness.

    :avocado: recursive
    """

    def test_core_files(self):
        """Test to verify core file creation.

        This test will send a signal 6 to a random daos_io_server process so
        that it will create a core file, allowing the core file collection code
        in launch.py to be tested.

        :avocado: tags=medium,test_harness,core_files
        """
        # Choose a server find the pid of its daos_io_server process
        host = choice(self.server_managers[0].hosts)
        self.log.info("Obtaining pid of the daos_io_server process on %s", host)
        task = run_task([host], "pgrep daos_io_server", 20)
        for retcode, hostlist in task.iter_retcodes():
            for buffer, _ in task.iter_buffers(hostlist):
                if retcode != 0:
                    self.fail(
                        "Error obtaining pid of the daos_io_server process on "
                        "{}: {}".format(",".join(hostlist), buffer))
                pid = str(buffer)
                break
            break
        self.log.info("Found pid %s", pid)

        # Send a signal 6 to its daos_io_server process
        result = pcmd([host], "sudo kill -6 {}".format(pid))
        if 0 not in result:
            self.fail("Error sending a signal 6 to {} on {}".format(pid, host))

        # Simplify resolving the host name to rank by marking all ranks as
        # expected to be either running or errored (sent a signal 6)
        self.server_managers[0].update_expected_states(
            None, ["Joined", "Errored"])
