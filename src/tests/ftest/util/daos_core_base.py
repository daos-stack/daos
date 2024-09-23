"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import shutil

from apricot import TestWithServers
from cmocka_utils import CmockaUtils
from general_utils import get_log_file
from job_manager_utils import get_job_manager
from test_utils_pool import POOL_TIMEOUT_INCREMENT


class DaosCoreBase(TestWithServers):
    # pylint: disable=too-many-nested-blocks
    """Runs the daos_test sub-tests with multiple servers.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize the DaosCoreBase object."""
        super().__init__(*args, **kwargs)
        self.subtest_name = None
        self.using_local_host = False

    def setUp(self):
        """Set up before each test."""
        self.subtest_name = self.get_test_param("test_name")
        self.subtest_name = self.subtest_name.replace(" ", "_")

        # obtain separate logs
        self.update_log_file_names(f"{self.name.str_uid}-{self.subtest_name}")

        super().setUp()

    def get_test_param(self, name, default=None):
        """Get the test-specific test yaml parameter value.

        Args:
            name (str): name of the test yaml parameter to get
            default (object): value to return if a value is not found

        Returns:
            object: the test-specific test yaml parameter value

        """
        path = "/".join(["/run/daos_tests", name, "*"])
        return self.params.get(self.get_test_name(), path, default)

    def run_subtest(self):
        """Run daos_test with a subtest argument."""
        subtest = self.get_test_param("daos_test")
        num_clients = self.get_test_param("num_clients")
        if num_clients is None:
            num_clients = self.params.get("num_clients", '/run/daos_tests/*')

        scm_size = self.params.get("scm_size", '/run/pool/*')
        nvme_size = self.params.get("nvme_size", '/run/pool/*', 0)
        args = self.get_test_param("args", "")
        stopped_ranks = self.get_test_param("stopped_ranks", [])
        pools_created = self.get_test_param("pools_created", 1)
        self.increment_timeout(POOL_TIMEOUT_INCREMENT * pools_created)
        dmg = self.get_dmg_command()
        dmg_config_file = dmg.yaml.filename
        if self.hostlist_clients:
            dmg.copy_certificates(
                get_log_file("daosCA/certs"), self.hostlist_clients)
            dmg.copy_configuration(self.hostlist_clients)

        # Set up the daos test command
        cmocka_utils = CmockaUtils(
            self.hostlist_clients, self.subtest_name, self.outputdir, self.test_dir, self.log)
        daos_test_env = cmocka_utils.get_cmocka_env()
        daos_test_env["D_LOG_FILE"] = get_log_file(self.client_log)
        daos_test_env["D_LOG_MASK"] = self.get_test_param("test_log_mask", "DEBUG")
        daos_test_env["DD_MASK"] = "mgmt,io,md,epc,rebuild,test"
        daos_test_env["COVFILE"] = "/tmp/test.cov"
        daos_test_env["POOL_SCM_SIZE"] = str(scm_size)
        daos_test_env["POOL_NVME_SIZE"] = str(nvme_size)
        daos_test_cmd = cmocka_utils.get_cmocka_command(
            " ".join([self.daos_test, "-n", dmg_config_file, "".join(["-", subtest]), str(args)]))
        job = get_job_manager(self, "Orterun", daos_test_cmd, mpi_type="openmpi")
        job.assign_hosts(cmocka_utils.hosts, self.workdir, None)
        job.assign_processes(num_clients)
        job.assign_environment(daos_test_env)

        # Update the expected status for each ranks that will be stopped by this
        # test to avoid a false failure during tearDown().
        if "random" in stopped_ranks:
            # Set each expected rank state to be either stopped or running
            for manager in self.server_managers:
                manager.update_expected_states(
                    None, ["Joined", "Stopped", "Excluded"])
        else:
            # Set the specific expected rank state to stopped
            for rank in stopped_ranks:
                for manager in self.server_managers:
                    manager.update_expected_states(
                        rank, ["Stopped", "Excluded"])

        cmocka_utils.run_cmocka_test(self, job)

        try:
            tmp_log_path = "/tmp/suite_dmg.log"
            log_path = os.path.join(self.outputdir, f"{self.subtest_name}_dmg.log")
            shutil.move(tmp_log_path, log_path)
        except FileNotFoundError:
            # if dmg wasn't called, there will not be a dmg log file
            self.log.info("dmg log file not found")
        except IOError as error:
            self.log.error("unable to move dmg log: %s", error)
