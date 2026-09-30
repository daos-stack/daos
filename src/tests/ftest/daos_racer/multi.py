"""
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from daos_racer_utils import DaosRacerCommand
from exception_utils import CommandFailure
from job_manager_utils import get_job_manager


class DaosRacerMultiTest(TestWithServers):
    """Test cases that utilize the daos_racer tool.

    :avocado: recursive
    """

    def test_daos_racer_multi(self):
        """JIRA-3855: daos_racer/consistency checker test.

        Test Description:
            The daos_racer test tool generates a bunch of simultaneous,
            conflicting I/O requests. After it is run it will verify that all
            the replicas of a given object are consistent.

            Run daos_racer for 5-10 minutes or so on 3-way replicated object
            data (at least 6 servers) and verify the object replicas.

        Use Cases:
            Running simultaneous, conflicting I/O requests.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=io,daos_racer
        :avocado: tags=DaosRacerMultiTest,test_daos_racer_multi
        """
        self.assertGreater(len(self.hostlist_clients), 0, "This test requires at least one client")

        # Create the daos_racer command
        daos_racer = DaosRacerCommand(self.bin, self.hostlist_clients, self.get_dmg_command())
        daos_racer.get_params(self)

        # Create the mpi command
        job_manager = get_job_manager(self)
        job_manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        job_manager.assign_processes(ppn=self.params.get('ppn', daos_racer.namespace))
        job_manager.assign_environment(daos_racer.env)
        job_manager.job = daos_racer
        job_manager.check_results_list = ["<stderr>", "No MPI found"]
        job_manager.timeout = daos_racer.daos_racer_timeout.value

        self.log_step("Run daos_racer")
        try:
            job_manager.run()

        except CommandFailure as error:
            self.fail(f"daos_racer failed: {error}")

        self.log_step("Test passed!")
