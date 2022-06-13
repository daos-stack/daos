#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import write_host_file

from avocado import fail_on
from avocado.utils import process
from apricot import TestWithServers
from general_utils import get_log_file
from command_utils import ExecutableCommand
from exception_utils import CommandFailure
from agent_utils import include_local_host
from job_manager_utils import get_job_manager
from test_utils_pool import POOL_TIMEOUT_INCREMENT


class DaosCoreBase(TestWithServers):
    """Runs the daos_test subtests with multiple servers.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize the DaosCoreBase object."""
        super().__init__(*args, **kwargs)
        self.subtest_name = None

    def setUp(self):
        """Set up before each test."""
        self.subtest_name = self.get_test_param("test_name")
        self.subtest_name = self.subtest_name.replace(" ", "_")

        # obtain separate logs
        self.update_log_file_names(self.subtest_name)

        super().setUp()

        # if no client specified update self.hostlist_clients to local host
        # and create a new self.hostfile_clients.
        if self.hostlist_clients is None:
            self.hostlist_clients = include_local_host(self.hostlist_clients)
            self.hostfile_clients = write_host_file.write_host_file(
                self.hostlist_clients, self.workdir, None)

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
        nvme_size = self.params.get("nvme_size", '/run/pool/*')
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

        cmd = " ".join(
            [
                "-x", "=".join(["D_LOG_FILE", get_log_file(self.client_log)]),
                "--map-by node", "-x", "D_LOG_MASK=DEBUG",
                "-x", "DD_MASK=mgmt,io,md,epc,rebuild",
                "-x", "COVFILE=/tmp/test.cov",
                self.daos_test,
                "-n", dmg_config_file,
                "".join(["-", subtest]),
                str(args)
            ]
        )

        job_cmd = ExecutableCommand(namespace=None, command=cmd)
        job = get_job_manager(self, "Orterun", job_cmd, mpi_type="openmpi")
        # Assign the test to run
        job.hostfile.update(self.hostfile_clients)
        job.processes.update(num_clients)
        job_str = str(job)

        env = {}
        env['CMOCKA_XML_FILE'] = os.path.join(self.outputdir,
                                              "%g_cmocka_results.xml")
        env['CMOCKA_MESSAGE_OUTPUT'] = "xml"
        env['POOL_SCM_SIZE'] = "{}".format(scm_size)
        if not nvme_size:
            nvme_size = 0
        env['POOL_NVME_SIZE'] = "{}".format(nvme_size)

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

        try:
            process.run(job_str, env=env)
        except process.CmdError as result:
            if result.result.exit_status != 0:
                # fake a JUnit failure output
                self.create_results_xml(self.subtest_name, result,
                                        "Failed to run {}.".format(
                    self.daos_test))
                self.fail(
                    "{0} failed with return code={1}.\n".format(
                        job_str, result.result.exit_status))

    def create_results_xml(self, testname, result, error_message="Test failed to start up"):
        """Create a JUnit result.xml file for the failed command.

        Args:
            testname (str): name of the test
            result (CmdResult): result of the failed command.
        """
        filename = "".join([testname, "_results.xml"])
        filename = os.path.join(self.outputdir, filename)
        try:
            with open(filename, "w") as results_xml:
                results_xml.write('''<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="{0}" errors="1" failures="0" skipped="0" tests="1" time="0.0">
  <testcase name="ALL" time="0.0" >
    <error message="{3}"/>
    <system-out>
<![CDATA[{1}]]>
    </system-out>
    <system-err>
<![CDATA[{2}]]>
    </system-err>
  </testcase>
</testsuite>'''.format(
    testname, result.result.stdout_text, result.result.stderr_text, error_message))
        except IOError as error:
            self.log.error("Error creating %s: %s", filename, error)
