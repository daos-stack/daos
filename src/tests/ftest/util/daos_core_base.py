"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from avocado import fail_on

from apricot import TestWithServers
from general_utils import get_log_file
from cmocka_utils import CmockaUtils
from exception_utils import CommandFailure
from job_manager_utils import get_job_manager
from test_utils_pool import POOL_TIMEOUT_INCREMENT


class DaosCoreBase(TestWithServers):
    # pylint: disable=too-many-nested-blocks
    """Runs the daos_test subtests with multiple servers.

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
        self.update_log_file_names(self.subtest_name)

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

    @fail_on(CommandFailure)
    def start_server_managers(self, force=False):
        """Start the daos_server processes on each specified list of hosts.

        Enable scalable endpoint if requested with a test-specific
        'scalable_endpoint' yaml parameter.

        Args:
            force (bool, optional): whether or not to force starting the
                servers. Defaults to False.

        Returns:
            bool: whether or not to force the starting of the agents

        """
        # Enable scalable endpoint (if requested) prior to starting the servers
        scalable_endpoint = self.get_test_param("scalable_endpoint")
        if scalable_endpoint:
            for server_mgr in self.server_managers:
                for engine_params in server_mgr.manager.job.yaml.engine_params:
                    # Number of CaRT contexts should equal or be greater than
                    # the number of DAOS targets
                    targets = engine_params.get_value("targets")

                    # Convert the list of variable assignments into a dictionary
                    # of variable names and their values
                    env_vars = engine_params.get_value("env_vars")
                    env_dict = {
                        item.split("=")[0]: item.split("=")[1]
                        for item in env_vars}
                    env_dict["CRT_CTX_SHARE_ADDR"] = "1"
                    env_dict["COVFILE"] = "/tmp/test.cov"
                    env_dict["D_LOG_FILE_APPEND_PID"] = "1"
                    if "CRT_CTX_NUM" not in env_dict or \
                            int(env_dict["CRT_CTX_NUM"]) < int(targets):
                        env_dict["CRT_CTX_NUM"] = str(targets)
                    engine_params.set_value("crt_ctx_share_addr", 1)
                    engine_params.set_value(
                        "env_vars",
                        ["=".join(items) for items in list(env_dict.items())]
                    )

        # Start the servers
        return super().start_server_managers(force=force)

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
        daos_test_env["D_LOG_MASK"] = "DEBUG"
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
