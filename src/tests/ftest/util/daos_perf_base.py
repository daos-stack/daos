"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from apricot import TestWithServers
from daos_perf_utils import DaosPerfCommand
from exception_utils import CommandFailure
from general_utils import get_log_file


class DaosPerfBase(TestWithServers):
    """Base test cases for the daos_perf command.

    Test Class Description:
        Tests daos_perf with different configurations.

    :avocado: recursive
    """

    def run_daos_perf(self):
        """Run the daos_perf command."""
        # Create pool
        self.add_pool(connect=False)
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
        daos_perf_env = daos_perf.env.copy()
        daos_perf_env["D_LOG_FILE"] = get_log_file("{}_daos.log".format(daos_perf.command))

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

        return None
