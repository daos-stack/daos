#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from daos_perf_utils import DaosPerfCommand
from exception_utils import CommandFailure


class DaosPerfBase(TestWithServers):
    """Base test cases for the daos_perf command.

    Test Class Description:
        Tests daos_perf with different configurations.

    :avocado: recursive
    """

    def run_daos_perf(self):
        """Run the daos_perf command."""
        # Create pool
        self.add_pool()
        # Create container
        self.add_container(self.pool)
        # Obtain the number of processes listed with the daos_perf options
        processes = self.params.get("processes", "/run/daos_perf/*")
        # Use the dmg_control yaml
        dmg_config_file = self.get_dmg_command().yaml.filename
        # Create the daos_perf command from the test yaml file
        daos_perf = DaosPerfCommand(self.bin)
        daos_perf.get_params(self)
        daos_perf.pool_uuid.update(self.pool.uuid)
        daos_perf.cont_uuid.update(self.container.uuid)
        daos_perf.dmg_config_file.update(dmg_config_file)
        self.log.info("daos_perf command: %s", str(daos_perf))
        daos_perf_env = daos_perf.get_environment(self.server_managers[0])

        # Create the orterun command
        self.job_manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        self.job_manager.assign_processes(processes)
        self.job_manager.assign_environment(daos_perf_env)
        self.job_manager.job = daos_perf
        self.log.info("orterun command: %s", str(self.job_manager))

        # Run the daos_perf command and check for errors
        try:
            return self.job_manager.run()

        except CommandFailure as error:
            self.log.error("DAOS PERF Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")
