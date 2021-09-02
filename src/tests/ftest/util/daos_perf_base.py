#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re

from apricot import TestWithServers
from daos_perf_utils import DaosPerfCommand
from general_utils import get_default_config_file
from job_manager_utils import Orterun


class DaosPerfBase(TestWithServers):
    """Base test cases for the daos_perf command.

    Test Class Description:
        Tests daos_perf with different configurations.

    :avocado: recursive
    """

    def run_daos_perf(self):
        """Run the daos_perf command."""
        # Obtain the number of processes listed with the daos_perf options
        processes = self.params.get("processes", "/run/daos_perf/*")
        # Use the dmg_control yaml
        dmg_config_file = self.get_dmg_command().yaml.filename
        # Create the daos_perf command from the test yaml file
        daos_perf = DaosPerfCommand(self.bin)
        daos_perf.get_params(self)
        daos_perf.dmg_config_file.update(dmg_config_file)
        self.log.info("daos_perf command: %s", str(daos_perf))
        daos_perf_env = daos_perf.get_environment(self.server_managers[0])

        # Create the orterun command
        self.job_manager = Orterun(daos_perf)
        self.job_manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        self.job_manager.assign_processes(processes)
        self.job_manager.assign_environment(daos_perf_env)
        self.log.info("orterun command: %s", str(orterun))

        # Run the daos_perf command and check for errors
        result = self.job_manager.run()
        errors = re.findall(
            r"(.*(?:non-zero exit code|errors|failed|Failed).*)",
            result.stdout_text)
        if errors:
            self.fail(
                "Errors detected in daos_perf output:\n{}".format(
                    "  \n".join(errors)))
