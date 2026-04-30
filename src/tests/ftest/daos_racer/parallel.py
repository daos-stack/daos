"""
(C) Copyright 2021-2022 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

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

        self.log_step("Run daos_racer with multiple clients")
        try:
            job_manager.run()

        except CommandFailure as error:
            self.fail(f"daos_racer failed: {error}")

        self.log_step("Test passed!")
