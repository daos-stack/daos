#!/usr/bin/python3
"""
(C) Copyright 2021-2022 Intel Corporation.
(C) Copyright 2025 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from daos_racer_utils import DaosRacerCommand
from exception_utils import CommandFailure
from job_manager_utils import get_job_manager


class DaosRacerParallelTest(TestWithServers):
    """Test cases that utilize the daos_racer tool.

    :avocado: recursive
    """

    def test_daos_racer_parallel(self):
        """JIRA-8445: multi-client daos_racer/consistency checker test.

        Test Description:
            The daos_racer test tool generates a bunch of simultaneous, conflicting I/O requests. It
            will test both replicated objects and EC objects and verify the data consistency. The
            duration will depend on parameters in test yaml configuration file.

        Use Cases:
            Running simultaneous, conflicting I/O requests.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=io,daos_racer
        :avocado: tags=DaosRacerParallelTest,test_daos_racer_parallel
        """
        # Create the dmg command
        daos_racer = DaosRacerCommand(self.bin, self.hostlist_clients[0], self.get_dmg_command())
        daos_racer.get_params(self)

        # Create the orterun command
        job_manager = get_job_manager(self)
        job_manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        job_manager.assign_processes(len(self.hostlist_clients))
        job_manager.assign_environment(daos_racer.env)
        job_manager.job = daos_racer
        job_manager.check_results_list = ["<stderr>"]
        job_manager.timeout = daos_racer.clush_timeout.value
        self.log.info("Multi-process command: %s", str(job_manager))

        # Run the daos_racer command and check for errors
        try:
            job_manager.run()

        except CommandFailure as error:
            msg = f"daos_racer failed: {error}"
            self.log.error(msg)
            self.fail(msg)

        self.log.info("Test passed!")
