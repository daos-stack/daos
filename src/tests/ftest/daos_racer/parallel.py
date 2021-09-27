#!/usr/bin/python3
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from command_utils import CommandFailure
from daos_racer_utils import DaosRacerCommand


class DaosRacerTest(TestWithServers):
    """Test cases that utilize the daos_racer tool.

    :avocado: recursive
    """

    def test_parallel(self):
        """JIRA-8445: multi-client daos_racer/consistency checker test.

        Test Description:
            The daos_racer test tool generates a bunch of simultaneous,
            conflicting I/O requests. After it is run it will verify that all
            the replicas of a given object are consistent.

            Run daos_racer for 5-10 minutes or so on 3-way replicated object
            data (at least 6 servers) and verify the object replicas.

        Use Cases:
            Running simultaneous, conflicting I/O requests.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=io,daosracer,daos_racer_parallel
        """
        # Create the dmg command
        daos_racer = DaosRacerCommand(self.bin, self.hostlist_clients[0], self.get_dmg_command())
        daos_racer.get_params(self)

        # Create the orterun command
        self.job_manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        self.job_manager.assign_processes(1)
        self.job_manager.assign_environment(daos_racer.get_environment(self.server_managers[0]))
        self.job_manager.job = daos_racer
        self.log.info("Multi-process command: %s", str(self.job_manager))

        # Run the daos_perf command and check for errors
        try:
            self.job_manager.run()

        except CommandFailure as error:
            self.log.error("DAOS Racer Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")

        self.log.info("Test passed!")
