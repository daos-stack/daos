"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithServers
from cmocka_utils import CmockaUtils
from job_manager_utils import get_job_manager


class Simple(TestWithServers):
    """Run Simple API tests.

    :avocado: recursive
    """

    def run_cmocka_test_command(self, test_name, command):
        """Create necessary objects to run a given cmocka test and run.

        Args:
            test_name (str): Test name.
            command (str): Test command string.
        """
        cmocka_utils = CmockaUtils(
            hosts=self.hostlist_clients, test_name=test_name, outputdir=self.outputdir,
            test_dir=self.test_dir, log=self.log)
        job = get_job_manager(
            self, "Clush", cmocka_utils.get_cmocka_command(command))
        job.assign_hosts(cmocka_utils.hosts)
        cmocka_utils.run_cmocka_test(self, job)
        if not job.result.passed:
            self.fail(f'Error running {command} on {job.hosts}')

    def test_simple_array(self):
        """Run simple_array.c cmocka test.

        Jira ID: DAOS-8297

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=api
        :avocado: tags=Simple,test_simple_array
        """
        test_name = "simple_array"
        simple_array_cmd = os.path.join(self.prefix, 'lib', 'daos', 'TESTING', 'tests', test_name)
        # wolf-313
        # simple_array_cmd = os.path.join(
        #     '/home', 'mkano', 'daos', 'install', 'lib', 'daos', 'TESTING', 'tests', test_name)
        self.log.debug(f"## simple_array_cmd = {simple_array_cmd}")
        self.run_cmocka_test_command(test_name=test_name, command=simple_array_cmd)

    def test_simple_obj(self):
        """Run simple_obj.c cmocka test.

        Jira ID: DAOS-8297

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=api
        :avocado: tags=Simple,test_simple_obj
        """
        test_name = "simple_obj"

        # This test requires a pool.
        pool = self.get_pool(connect=False)

        simple_obj_cmd = [os.path.join(self.prefix, 'lib', 'daos', 'TESTING', 'tests', test_name)]
        # wolf-313
        # simple_obj_cmd = [os.path.join(
        #     '/home', 'mkano', 'daos', 'install', 'lib', 'daos', 'TESTING', 'tests', test_name)]
        simple_obj_cmd.append(pool.identifier)
        command = " ".join(simple_obj_cmd)
        self.log.debug(f"## simple_obj_cmd = {command}")
        self.run_cmocka_test_command(test_name=test_name, command=command)

    def test_simple_dfs(self):
        """Run simple_dfs.c cmocka test.

        Jira ID: DAOS-8297

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=api
        :avocado: tags=Simple,test_simple_dfs
        """
        test_name = "simple_dfs"

        # This test requires a pool and a container.
        pool = self.get_pool(connect=False)
        container = self.get_container(pool=pool)

        simple_dfs_cmd = [os.path.join(self.prefix, 'lib', 'daos', 'TESTING', 'tests', test_name)]
        # wolf-313
        # simple_dfs_cmd = [os.path.join(
        #     '/home', 'mkano', 'daos', 'install', 'lib', 'daos', 'TESTING', 'tests', test_name)]
        simple_dfs_cmd.append(pool.identifier)
        simple_dfs_cmd.append(container.identifier)
        self.log.debug(f"## simple_dfs_cmd = {simple_dfs_cmd}")
        command = " ".join(simple_dfs_cmd)
        self.run_cmocka_test_command(test_name=test_name, command=command)
