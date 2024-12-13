"""
  (C) Copyright 2024 Hewlett Packard Enterprise Development LP.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os

from apricot import TestWithServers
from cmocka_utils import CmockaUtils
from job_manager_utils import get_job_manager


class DaosCoreTestShm(TestWithServers):
    """Runs DAOS shared memory tests.

    :avocado: recursive
    """

    def test_daos_shm_unit(self):
        """Jira ID: DAOS-16877.

        Test Description:
            Run shm_test

        Use cases:
            DAOS shared memory unit tests

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=daos_test,shm_test,shm
        :avocado: tags=DaosCoreTestShm,test_daos_shm_unit
        """
        daos_test = os.path.join(self.bin, 'shm_test')
        cmocka_utils = CmockaUtils(
            self.hostlist_clients, "shm", self.outputdir, self.test_dir, self.log)
        daos_test_env = cmocka_utils.get_cmocka_env()
        job = get_job_manager(self, "Clush", cmocka_utils.get_cmocka_command(daos_test))
        job.assign_hosts(cmocka_utils.hosts)
        job.assign_environment(daos_test_env)

        cmocka_utils.run_cmocka_test(self, job)
        if not job.result.passed:
            self.fail(f'Error running {job.command} on {job.hosts}')
