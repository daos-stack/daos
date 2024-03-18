"""
  (C) Copyright 2021-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from random import choice
from re import findall

from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet
from run_utils import RunException, run_local, run_remote


class HarnessCoreFilesTest(TestWithServers):
    """Processed core file harness test cases.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestWithServers object."""
        super().__init__(*args, **kwargs)

        # Always start the servers for each test variant
        self.start_agents_once = False
        self.start_servers_once = False

    def test_core_files(self):
        """Test to verify core file creation.

        This test will send a signal 6 to a random daos_engine process so
        that it will create a core file, allowing the core file collection code
        in launch.py to be tested.

        :avocado: tags=all,pr
        :avocado: tags=vm
        :avocado: tags=harness,core_files
        :avocado: tags=HarnessCoreFilesTest,test_core_files
        """
        # create a core.gdb file
        self.log.debug("Create a core.gdb.harness.advanced file in core_pattern dir.")
        try:
            results = run_local(self.log, "cat /proc/sys/kernel/core_pattern", check=True)
        except RunException:
            self.fail("Unable to find local core file pattern")
        core_path = os.path.split(results.stdout.splitlines()[-1])[0]
        core_file = "{}/core.gdb.harness.advanced".format(core_path)

        self.log.debug("Creating %s", core_file)
        try:
            with open(core_file, "w", encoding="utf-8") as local_core_file:
                local_core_file.write("THIS IS JUST A TEST\n")
        except IOError as error:
            self.fail("Error writing {}: {}".format(core_file, str(error)))

        # Choose a server find the pid of its daos_engine process
        host = NodeSet(choice(self.server_managers[0].hosts))   # nosec
        ranks = self.server_managers[0].get_host_ranks(host)
        self.log.info("Obtaining pid of the daos_engine process on %s (rank %s)", host, ranks)
        pid = None
        result = run_remote(self.log, host, "pgrep --list-full daos_engine", timeout=20)
        if not result.passed:
            self.fail("Error obtaining pid of the daos_engine process on {}".format(host))
        pid = findall(r"(\d+)\s+[A-Za-z0-9/]+daos_engine\s+", "\n".join(result.output[0].stdout))[0]
        if pid is None:
            self.fail("Error obtaining pid of the daos_engine process on {}".format(host))
        self.log.info("Found pid %s", pid)

        # Send a signal 6 to its daos_engine process
        self.log.info("Sending a signal 6 to %s", pid)
        if not run_remote(self.log, host, "sudo -n kill -6 {}".format(pid)).passed:
            self.fail("Error sending a signal 6 to {} on {}".format(pid, host))

        # Simplify resolving the host name to rank by marking all ranks as
        # expected to be either running or errored (sent a signal 6)
        self.server_managers[0].update_expected_states(ranks, ["Joined", "Errored"])

        # Wait for the engine to create the core file
        ranks = self.server_managers[0].get_host_ranks(host)
        state = ["errored"]
        try:
            self.log.info(
                "Waiting for the engine on %s (rank %s) to move to the %s state",
                host, ranks, state)
            if self.server_managers[0].check_rank_state(ranks, state, 25):
                self.fail("Rank {} state not {} after sending signal 6".format(ranks, state))
        finally:
            # Display the journalctl log for the process that was sent the signal
            self.server_managers[0].manager.dump_logs(host)

        self.log.info("Test passed")

    def test_core_files_hw(self):
        """Test to verify core file creation.

        This test will send a signal 6 to a random daos_engine process so
        that it will create a core file, allowing the core file collection code
        in launch.py to be tested.

        :avocado: tags=all
        :avocado: tags=hw,medium,large
        :avocado: tags=harness,core_files
        :avocado: tags=HarnessCoreFilesTest,test_core_files_hw
        """
        self.test_core_files()
