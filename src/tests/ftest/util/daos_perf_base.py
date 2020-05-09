#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
import re

from apricot import TestWithServers
from daos_perf_utils import DaosPerfCommand
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

        # Create the daos_perf command from the test yaml file
        daos_perf = DaosPerfCommand(self.bin)
        daos_perf.get_params(self)
        self.log.info("daos_perf command: %s", str(daos_perf))
        daos_perf_env = daos_perf.get_environment(self.server_managers[0])

        # Create the orterun command
        orterun = Orterun(daos_perf)
        orterun.assign_hosts(self.hostlist_clients, self.workdir, None)
        orterun.assign_processes(processes)
        orterun.assign_environment(daos_perf_env)
        self.log.info("orterun command: %s", str(orterun))

        # Run the daos_perf command and check for errors
        result = orterun.run()
        errors = re.findall(
            r"(.*(?:non-zero exit code|errors|failed|Failed).*)", result.stdout)
        if errors:
            self.fail(
                "Errors detected in daos_perf output:\n{}".format(
                    "  \n".join(errors)))
