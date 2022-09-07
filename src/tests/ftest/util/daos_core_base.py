#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os

from avocado import fail_on
from avocado.plugins.xunit import XUnitResult

from apricot import TestWithServers
from general_utils import get_log_file, get_clush_command, run_command, log_task
from command_utils_base import EnvironmentVariables
from command_utils import ExecutableCommand
from exception_utils import CommandFailure
from agent_utils import include_local_host
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

        # if no client specified update self.hostlist_clients to local host
        # and create a new self.hostfile_clients.
        if not self.hostlist_clients:
            self.hostlist_clients = include_local_host(self.hostlist_clients)
            self.using_local_host = True

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

        # Set up the daos test command and environment settings
        cmocka_dir = self._get_cmocka_dir()
        pool_envs = {"POOL_SCM_SIZE": str(scm_size), "POOL_NVME_SIZE": str(nvme_size)}
        daos_test_cmd = self._get_daos_test_command(
            " ".join([self.daos_test, "-n", dmg_config_file, "".join(["-", subtest]), str(args)]))
        job = get_job_manager(self, "Orterun", daos_test_cmd, mpi_type="openmpi")
        job.assign_hosts(self.hostlist_clients, self.workdir, None)
        job.assign_processes(num_clients)
        job.assign_environment(self._get_daos_test_env(cmocka_dir, pool_envs))

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

        self.run_daos_test(job, cmocka_dir)

    def _get_cmocka_dir(self):
        """Get the directory in which to write cmocka xml results.

        For tests running locally use a directory that will place the cmocka results directly in
        the avocado job-results/*/test-results/*/data/ directory (self.outputdir).

        For tests running remotely use a 'cmocka' subdirectory in the DAOS_TEST_LOG_DIR directory as
        this directory should exist on all hosts.  After the command runs these remote files will
        need to be copied back to this host and placed in the self.outputdir directory in order for
        them to be included as part of the Jenkins test results - see _collect_cmocka_results().

        Returns:
            str: the cmocka directory to use with the CMOCKA_XML_FILE env

        """
        cmocka_dir = self.outputdir
        if not self.using_local_host:
            cmocka_dir = os.path.join(self.test_dir, "cmocka")
            command = " ".join(["mkdir", "-p", cmocka_dir])
            log_task(include_local_host(self.hostlist_clients), command)
        return cmocka_dir

    def _get_daos_test_env(self, cmocka_dir, additional_envs=None):
        """Get the environment variables to use when running the daos_test command.

        Args:
            cmocka_dir (str): directory where the cmocka xml results should be written
            additional_envs (dict, optional): additional enviroment variables names and values to
                include with the required daos_test enviroment variables. Defaults to None.

        Returns:
            EnvironmentVariables: the environment variables for the daos_test command

        """
        env_dict = {
            "D_LOG_FILE": get_log_file(self.client_log),
            "D_LOG_MASK": "DEBUG",
            "DD_MASK": "mgmt,io,md,epc,rebuild,test",
            "COVFILE": "/tmp/test.cov",
            "CMOCKA_XML_FILE": os.path.join(cmocka_dir, "%g_cmocka_results.xml"),
            "CMOCKA_MESSAGE_OUTPUT": "xml",
        }
        if additional_envs:
            env_dict.update(additional_envs)
        return EnvironmentVariables(env_dict)

    def _get_daos_test_command(self, command):
        """Get an ExecutableCommand representing the provided command string.

        Adds detection of any bad words in the command output that, if found, will result in a
        command failure.

        Args:
            command (str): the command string to use to create the ExecutableCommand

        Returns:
            ExecutableCommand: the object setup to run the command

        """
        bad_words = ["Process received signal", "stack smashing detected", "End of error message"]
        return ExecutableCommand(namespace=None, command=command, check_results=bad_words)

    def run_daos_test(self, command, cmocka_dir):
        """Run the daos_test command.

        After the command completes, copy any remote cmocka results that may exist back to this host
        and generate a cmocka result if one is missing or the command failed before generating one.

        Args:
            command (ExecutableCommand): the command to run
            cmocka_dir (str): the location of the cmocak files generated by the test
        """
        job_command = command.job if hasattr(command, "job") else command
        error_message = None
        error_exception = None
        try:
            command.run()

        except CommandFailure as error:
            error_message = f"Error detected running {job_command}"
            error_exception = error
            self.log.exception(error_message)
            self.fail(error_message)

        finally:
            self._collect_cmocka_results(cmocka_dir)
            if not self._check_cmocka_files():
                if error_message is None:
                    error_message = f"Missing cmocka results for {job_command} in {cmocka_dir}"
                self._generate_cmocka_files(error_message, error_exception)

    def _collect_cmocka_results(self, cmocka_dir):
        """Collect any remote cmocka files.

        Args:
            cmocka_dir (str): directory containing cmocka files
        """
        if self.using_local_host:
            # No remote hosts where used to run the daos_test command, so nothing to do
            return

        # List any remote cmocka files
        self.log.debug("Remote %s directories:", cmocka_dir)
        ls_command = "ls -alR {0}".format(cmocka_dir)
        clush_ls_command = "{0} {1}".format(
            get_clush_command(self.hostlist_clients, "-B -S"), ls_command)
        log_task(self.hostlist_clients, clush_ls_command)

        # Copy any remote cmocka files back to this host
        command = "{0} --rcopy {1} --dest {1}".format(
            get_clush_command(self.hostlist_clients), cmocka_dir)
        try:
            run_command(command)

        finally:
            self.log.debug("Local %s directory after clush:", cmocka_dir)
            run_command(ls_command)
            # Move local files to the avocado test variant data directory
            for cmocka_node_dir in os.listdir(cmocka_dir):
                cmocka_node_path = os.path.join(cmocka_dir, cmocka_node_dir)
                if os.path.isdir(cmocka_node_path):
                    for cmocka_file in os.listdir(cmocka_node_path):
                        cmocka_file_path = os.path.join(cmocka_node_path, cmocka_file)
                        if "_cmocka_results." in cmocka_file:
                            command = "mv {0} {1}".format(cmocka_file_path, self.outputdir)
                            run_command(command)

    def _check_cmocka_files(self):
        """Determine if cmocka files exist in the expected location.

        Returns:
            bool: True if cmocka files exist in the expected location; False otherwise
        """
        for item in os.listdir(self.outputdir):
            if "cmocka_results.xml" in item:
                return True
        return False

    def _generate_cmocka_files(self, error_message, error_exception):
        """Generate a cmocka xml file.

        Args:
            error_message (str): a description of the test failure
            error_exception (Exception): the exception raised when the failure occurred
        """
        class DaosTestJob():
            # pylint: disable=too-few-public-methods
            """Provides the neccessary job data to generate a cmocka.xml file."""

            def __init__(self, name, log_dir):
                """Initialize a DaosTestJob object.

                Args:
                    name (str): name of the daos_test subtest
                    log_dir (str): where to create the cmocka.xml file
                """
                cmocka_xml = os.path.join(log_dir, f"{name}_cmocka_results.xml")
                self.config = {
                    "job.run.result.xunit.enabled": None,
                    "job.run.result.xunit.output": cmocka_xml,
                    "job.run.result.xunit.max_test_log_chars": 100000,
                    "job.run.result.xunit.job_name": name,
                }
                self.logdir = None

        class DaosTestResult():
            # pylint: disable=too-few-public-methods
            """Provides the neccessary result data to generate a cmocka.xml file."""

            def __init__(self, name, log_file, message, exception):
                """Initialize a DaosTestResult object with one test error.

                Args:
                    name (str): daos_test subtest name
                    log_file (str): the test log file
                    message (str): the failure message
                """
                self.class_name = f"FTEST_daos_test.{name}"
                self.name = name
                self.logfile = log_file
                self.errors = 1
                self.interrupted = 0
                self.failed = 0
                self.skipped = 0
                self.cancelled = 0
                self.tests_total = 1
                self.tests_total_time = 0
                self.tests = [
                    DaosTestResultTest(self.class_name, self.name, log_file, message, exception)]

        class DaosTestResultTest():
            # pylint: disable=too-few-public-methods
            """Provides the neccessary result test data to generate a cmocka.xml file."""

            def __init__(self, class_name, name, log_file, message, exception):
                """Initialize a DaosTestResultTest object with an error.

                Args:
                    class_name (str): _description_
                    name (str): _description_
                    log_file (str): _description_
                    message (str): a description of the test failure
                    exception (Exception): the exception raised when the failure occurred
                """
                self.class_name = class_name
                self.name = name
                self.logfile = log_file
                self.time_elapsed = 0
                self.status = "ERROR"
                self.fail_class = "Missing file" if exception is None else "Failed command"
                self.fail_reason = message
                self.traceback = exception

            def get(self, name, default=None):
                """Get the value of the attribute name.

                Args:
                    name (str): TimedResult attribute name to get
                    default (object, optional): value to return if name is not defined. Defaults to
                        None.

                Returns:
                    object: the attribute value or default if not defined

                """
                return getattr(self, name, default)

        job = DaosTestJob(self.subtest_name, self.outputdir)
        result = DaosTestResult(self.subtest_name, self.logfile, error_message, error_exception)
        result_xml = XUnitResult(error_message)
        result_xml.render(result, job)
