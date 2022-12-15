#!/usr/bin/python3
"""
(C) Copyright 2021-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from exception_utils import CommandFailure
from daos_racer_utils import DaosRacerCommand


class DaosRacerTest(TestWithServers):
    """Test cases that utilize the daos_racer tool.

    :avocado: recursive
    """

    def test_parallel(self):
        """JIRA-8445: multi-client daos_racer/consistency checker test.

        Test Description:
            The daos_racer test tool generates a bunch of simultaneous, conflicting I/O requests. It
            will test both replicated objects and EC objects and verify the data consistency. The
            duration will depend on parameters in test yaml configuration file.

        Use Cases:
            Running simultaneous, conflicting I/O requests.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=io,daosracer
        :avocado: tags=daos_racer,DaosRacerTest,test_parallel
        """
        # Create the dmg command
        daos_racer = DaosRacerCommand(self.bin, self.hostlist_clients[0], self.get_dmg_command())
        daos_racer.get_params(self)

        # Create the orterun command
        self.job_manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        self.job_manager.assign_processes(len(self.hostlist_clients))
        self.job_manager.assign_environment(daos_racer.env)
        self.job_manager.job = daos_racer
        self.job_manager.check_results_list = ["<stderr>"]
        self.job_manager.timeout = daos_racer.clush_timeout.value
        self.log.info("Multi-process command: %s", str(self.job_manager))

        # Run the daos_racer command and check for errors
        try:
            self.job_manager.run()

        except CommandFailure as error:
            self.log.error("DAOS Racer Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")

        self.log.info("Test passed!")
