"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import logging
import os
import re
import sys
from tempfile import TemporaryDirectory
import time

from ClusterShell.NodeSet import NodeSet

from environment_utils import TestEnvironment
from collection_utils import collect_test_result, TEST_RESULTS_DIRS
from data_utils import list_unique, list_flatten, dict_extract_values
from host_utils import get_node_set, get_local_host, HostInfo, HostException
from logger_utils import get_file_handler, LOG_FILE_FORMAT
from results_utils import LaunchTestName
from run_utils import RunException, run_local, run_remote
from slurm_setup import SlurmSetup, SlurmSetupException
from slurm_utils import show_partition, create_partition, delete_partition
from storage_utils import StorageInfo, StorageException
from user_utils import groupadd, useradd, userdel, get_group_id, get_user_groups
from yaml_utils import get_yaml_data, YamlUpdater

ARG_SPLITTER = "+"


class LaunchException(Exception):
    """Exception for launch.py execution."""


def get_test_groups(logger, avocado, test_env, servers, clients, control_host, tags, nvme,
                    yaml_directory, yaml_extension=None):
    """Get the list of test groups to run.

    Groups are defined by the tags and nvme arguments separated by '+'.  Ensure each group is
    defined with a list of tags and an nvme argument by padding any split argument list that is
    smaller than the other one.

    Args:
        logger (Logger): logger for the messages produced by this method
        avocado (AvocadoInfo): information about this version of avocado
        test_env (TestEnvironment): the test environment
        servers (NodeSet): hosts designated for the server role in tests
        clients (NodeSet): hosts designated for the client role in tests
        control_host (NodeSet): hosts designated for the control role in tests
        tags (list): a list of tags or test file names
        nvme (str): the --nvme argument
        yaml_directory (str): directory used to store modified test yaml files
        yaml_extension (str, optional): optional test yaml file extension to use when creating
            the TestInfo object.

    Returns:
        tuple: a list of lists of tags and a list of nvme arguments
    """
    logger.debug("-" * 80)
    logger.debug("Creating test groups")
    groups = []

    # Separate the tags and nvme arguments into groups separated by '+'.
    tag_groups = split_list(tags, ARG_SPLITTER) if tags else [[]]
    nvme_groups = nvme.split(ARG_SPLITTER) if nvme else []
    for _ in range(len(tag_groups) - len(nvme_groups)):
        # Fill the nvme group with 'auto_md_on_ssd' entries
        nvme_groups.append("auto_md_on_ssd")
    for _ in range(len(nvme_groups) - len(tag_groups)):
        # Fill the tag groups with the last specified tags
        tag_groups.append(tag_groups[-1])
    logger.debug("  tags: %s", tag_groups)
    logger.debug("  nvme: %s", nvme_groups)

    # List the test files that match the specified tags
    for index, tag_group in enumerate(tag_groups):
        groups.append(
            TestGroup(
                index, avocado, test_env, servers, clients, control_host, tag_group,
                nvme_groups[index], yaml_directory, yaml_extension)
        )
        groups[-1].list_tests(logger)
    return groups


def split_list(value, element):
    """Split a list into a list of lists by a list element.

    Args:
        value (list): the list to split
        element (object): an element in the list on which to split the list

    Returns:
        list: a list of lists split by the element (element is not included in the lists)
    """
    groups = []
    start = 0
    while True:
        try:
            end = value.index(element, start)
        except ValueError:
            end = None
        groups.append(value[start:end])
        if end is None:
            break
        start = end + 1
    return groups


def fault_injection_enabled(logger):
    """Determine if fault injection is enabled.

    Args:
        logger (Logger): logger for the messages produced by this method

    Returns:
        bool: whether or not fault injection is enabled
    """
    logger.debug("-" * 80)
    logger.debug("Checking for fault injection enablement via 'fault_status':")
    try:
        run_local(logger, "fault_status", check=True)
        logger.debug("  Fault injection is enabled")
        return True
    except RunException:
        # Command failed or yielded a non-zero return status
        logger.debug("  Fault injection is disabled")
    return False


def setup_fuse_config(logger, hosts):
    """Set up the system fuse config file.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts to setup

    Raises:
        LaunchException: if setup fails
    """
    logger.debug("-" * 80)
    logger.info("Setting up fuse config")
    fuse_configs = ("/etc/fuse.conf", "/etc/fuse3.conf")
    command = ";".join([
        "if [ -e {0} ]",
        "then ls -l {0}",
        "(grep -q '^user_allow_other$' {0} || echo user_allow_other | sudo tee -a {0})",
        "cat {0}",
        "fi"
    ])
    for config in fuse_configs:
        if not run_remote(logger, hosts, command.format(config)).passed:
            raise LaunchException(f"Failed to setup {config}")


def display_disk_space(logger, path):
    """Display disk space of provided path destination.

    Args:
        logger (Logger): logger for the messages produced by this method
        path (str): path to directory to print disk space for.
    """
    logger.debug("-" * 80)
    logger.debug("Current disk space usage of %s", path)
    try:
        run_local(logger, f"df -h {path}", check=False)
    except RunException:
        pass


def summarize_run(logger, mode, status):
    """Summarize any failures that occurred during testing.

    Args:
        status (int): overall status of running all tests

    Returns:
        int: status code to use when exiting launch.py
    """
    logger.debug("=" * 80)
    return_code = 0
    if status == 0:
        logger.info("All avocado tests passed!")
        return return_code

    # Log any of errors that occurred during the run and determine an overall exit code
    bit_error_map = {
        1: "Failed avocado tests detected!",
        2: "ERROR: Failed avocado jobs detected!",
        4: "ERROR: Failed avocado commands detected!",
        8: "Interrupted avocado jobs detected!",
        16: "ERROR: Failed to archive files after one or more tests!",
        32: "ERROR: Failed log size threshold check after one or more tests!",
        64: "ERROR: Failed to create a junit xml test error file!",
        128: "ERROR: Failed to prepare the hosts before running the one or more tests!",
        256: "ERROR: Failed to process core files after one or more tests!",
        512: "ERROR: Failed to stop daos_server.service after one or more tests!",
        1024: "ERROR: Failed to rename logs and results after one or more tests!",
        2048: "ERROR: Core stack trace files detected!",
        4096: "ERROR: Unexpected processes or mounts found running!"
    }
    for bit_code, error_message in bit_error_map.items():
        if status & bit_code == bit_code:
            logger.info(error_message)
            if mode == "ci" or (mode == "normal" and bit_code == 1) or bit_code == 8:
                # In CI mode the errors are reported in the results.xml, so always return 0
                # In normal mode avocado test failures do not yield a non-zero exit status
                # Interrupted avocado tests do not yield a non-zero exit status
                continue
            return_code = 1
    return return_code


class AvocadoInfo():
    """Information about this version of avocado."""

    def __init__(self):
        """Initialize an AvocadoInfo object."""
        self.major = 0
        self.minor = 0

    def __str__(self):
        """Get the avocado version as a string.

        Returns:
            str: the avocado version
        """
        return f"Avocado {str(self.major)}.{str(self.minor)}"

    @staticmethod
    def set_config(overwrite=False):
        """Set up the avocado config files if they do not already exist.

        Should be called before get_setting() to ensure any files generated by this method are
        included.

        Args:
            overwrite (bool, optional): if true overwrite any existing avocado config files. If
                false do not modify any existing avocado config files. Defaults to False.

        Raises:
            LaunchException: if there is an error writing an avocado config file
        """
        daos_base = os.getenv("DAOS_BASE", None)
        logs_dir = os.path.expanduser("~")
        if os.getenv("TEST_RPMS", "false").lower() == "true":
            logs_dir = os.path.join(os.sep, "var", "tmp", "ftest")
        elif daos_base:
            logs_dir = os.path.join(daos_base, "install", "lib", "daos", "TESTING", "ftest")

        job_results_dir = os.path.join(logs_dir, "avocado", "job-results")
        data_dir = os.path.join(logs_dir, "avocado", "data")
        config_dir = os.path.join(
            os.environ.get("VIRTUAL_ENV", os.path.expanduser("~")), ".config", "avocado")
        config_file = os.path.join(config_dir, "avocado.conf")
        sysinfo_dir = os.path.join(config_dir, "sysinfo")
        sysinfo_files_file = os.path.join(sysinfo_dir, "files")
        sysinfo_commands_file = os.path.join(sysinfo_dir, "commands")

        # Create the avocado configuration directories
        os.makedirs(config_dir, exist_ok=True)
        os.makedirs(sysinfo_dir, exist_ok=True)

        # Create the avocado config file. If one exists do not overwrite it.
        if not os.path.exists(config_file) or overwrite:
            # Give the avocado test tearDown method a minimum of 120 seconds to complete when the
            # test process has timed out.  The test harness will increment this timeout based upon
            # the number of pools created in the test to account for pool destroy command timeouts.
            config = [
                "[datadir.paths]\n",
                f"logs_dir = {job_results_dir}\n",
                f"data_dir = {data_dir}\n",
                "\n",
                "[job.output]\n",
                "loglevel = DEBUG\n",
                "\n",
                "[runner.timeout]\n",
                "after_interrupted = 120\n",
                "process_alive = 120\n",
                "process_died = 120\n",
                "\n",
                "[sysinfo.collectibles]\n",
                f"files = {sysinfo_files_file}\n",
                f"commands = {sysinfo_commands_file}\n",
            ]

            try:
                with open(config_file, "w", encoding="utf-8") as config_handle:
                    config_handle.writelines(config)
            except IOError as error:
                raise LaunchException(f"Error writing avocado config file {config_file}") from error

        # Create the avocado system info files file. If one exists do not overwrite it.
        if not os.path.exists(sysinfo_files_file) or overwrite:
            try:
                with open(sysinfo_files_file, "w", encoding="utf-8") as sysinfo_files_handle:
                    sysinfo_files_handle.write("/proc/mounts\n")
            except IOError as error:
                raise LaunchException(
                    f"Error writing avocado config file {sysinfo_files_file}") from error

        # Create the avocado system info commands file. If one exists do not overwrite it.
        if not os.path.exists(sysinfo_commands_file) or overwrite:
            try:
                with open(sysinfo_commands_file, "w", encoding="utf-8") as sysinfo_commands_handle:
                    sysinfo_commands_handle.write("ps axf\n")
                    sysinfo_commands_handle.write("dmesg\n")
                    sysinfo_commands_handle.write("df -h\n")
            except IOError as error:
                raise LaunchException(
                    f"Error writing avocado config file {sysinfo_commands_file}") from error

    def set_version(self, logger):
        """Set the avocado major and minor versions.

        Args:
            logger (logger): logger for the messages produced by this method

        Raises:
            LaunchException: if there is an error running 'avocado -v'
        """
        try:
            # pylint: disable=import-outside-toplevel
            from avocado.core.version import MAJOR, MINOR
            self.major = int(MAJOR)
            self.minor = int(MINOR)

        except ModuleNotFoundError:
            # Once lightweight runs are using python3-avocado, this can be removed
            try:
                result = run_local(logger, "avocado -v", check=True)
            except RunException as error:
                message = "Error obtaining avocado version after failed avocado.core.version import"
                raise LaunchException(message) from error
            try:
                version = re.findall(r"(\d+)\.(\d+)", result.stdout)[0]
                self.major = int(version[0])
                self.minor = int(version[1])
            except IndexError as error:
                raise LaunchException("Error extracting avocado version from command") from error

    @staticmethod
    def get_setting(logger, section, key, default=None):
        """Get the value for the specified avocado setting.

        Args:
            logger (logger): logger for the messages produced by this method
            section (str): avocado setting section name
            key (str): avocado setting key name
            default (object): default value to use if setting is undefined

        Raises:
            RunException: if there is an error getting the setting from the avocado command

        Returns:
            object: value for the avocado setting or None if not defined
        """
        try:
            # pylint: disable=import-outside-toplevel
            from avocado.core.settings import settings, SettingsError
            try:
                # Newer versions of avocado use this approach
                config = settings.as_dict()
                return config.get(".".join([section, key]))

            except AttributeError:
                # Older version of avocado, like 69LTS, use a different method
                # pylint: disable=no-member
                try:
                    return settings.get_value(section, key)
                except SettingsError:
                    # Setting not found
                    pass

            except KeyError:
                # Setting not found
                pass

        except ModuleNotFoundError:
            # Once lightweight runs are using python3-avocado, this can be removed
            result = run_local(logger, "avocado config", check=True)
            try:
                return re.findall(rf"{section}\.{key}\s+(.*)", result.stdout)[0]
            except IndexError:
                # Setting not found
                pass

        return default

    def get_logs_dir(self, logger):
        """Get the avocado directory in which the test results are stored.

        Args:
            logger (logger): logger for the messages produced by this method

        Returns:
            str: the directory used by avocado to log test results
        """
        default_base_dir = os.path.join("~", "avocado", "job-results")
        return os.path.expanduser(
            self.get_setting(logger, "datadir.paths", "logs_dir", default_base_dir))

    def get_directory(self, logger, directory, create=True):
        """Get the avocado test directory for the test.

        Args:
            logger (logger): logger for the messages produced by this method
            directory (str): name of the sub directory to add to the logs directory
            create (bool, optional): whether or not to create the directory if it doesn't exist.
                Defaults to True.

        Returns:
            str: the directory used by avocado to log test results
        """
        logs_dir = self.get_logs_dir(logger)
        test_dir = os.path.join(logs_dir, directory)
        if create:
            os.makedirs(test_dir, exist_ok=True)
        return test_dir

    def get_list_command(self):
        """Get the avocado list command for this version of avocado.

        Returns:
            list: avocado list command parts
        """
        if self.major >= 83:
            return ["avocado", "list"]
        if self.major >= 82:
            return ["avocado", "--paginator=off", "list"]
        return ["avocado", "list", "--paginator=off"]

    def get_list_regex(self):
        """Get the regular expression used to get the test file from the avocado list command.

        Returns:
            str: regular expression to use to get the test file from the avocado list command output
        """
        if self.major >= 92:
            return r"avocado-instrumented\s+(.*):"
        return r"INSTRUMENTED\s+(.*):"

    def get_run_command(self, test, tag_filters, sparse, failfast):
        """Get the avocado run command for this version of avocado.

        Args:
            test (TestInfo): the test information
            tag_filters (list): optional '--filter-by-tags' arguments
            sparse (bool): whether or not to provide sparse output of the test execution
            failfast (bool): whether or not to fail fast

        Returns:
            list: avocado run command
        """
        command = ["avocado"]
        if not sparse and self.major >= 82:
            command.append("--show=test")
        command.append("run")
        if self.major >= 82:
            command.append("--ignore-missing-references")
        else:
            command.extend(["--ignore-missing-references", "on"])
        if self.major >= 83:
            command.append("--disable-tap-job-result")
        else:
            command.extend(["--html-job-result", "on"])
            command.extend(["--tap-job-result", "off"])
        if not sparse and self.major < 82:
            command.append("--show-job-log")
        if tag_filters:
            command.extend(tag_filters)
        if failfast:
            command.extend(["--failfast", "on"])
        command.extend(["--mux-yaml", test.yaml_file])
        if test.extra_yaml:
            command.extend(test.extra_yaml)
        command.extend(["--", str(test)])
        return command


class TestInfo():
    """Defines the python test file and its associated test yaml file."""

    YAML_INFO_KEYS = [
        "test_servers",
        "server_partition",
        "server_reservation",
        "test_clients",
        "client_partition",
        "client_reservation",
        "client_users",
    ]

    def __init__(self, test_file, order, yaml_extension=None):
        """Initialize a TestInfo object.

        Args:
            test_file (str): the test python file
            order (int): order in which this test is executed
            yaml_extension (str, optional): if defined and a test yaml file exists with this
                extension, the yaml file will be used in place of the default test yaml file.
        """
        self.name = LaunchTestName(test_file, order, 0)
        self.test_file = test_file
        self.yaml_file = ".".join([os.path.splitext(self.test_file)[0], "yaml"])
        if yaml_extension:
            custom_yaml = ".".join(
                [os.path.splitext(self.test_file)[0], str(yaml_extension), "yaml"])
            if os.path.exists(custom_yaml):
                self.yaml_file = custom_yaml
        parts = self.test_file.split(os.path.sep)[1:]
        self.python_file = parts.pop()
        self.directory = os.path.join(*parts) if parts else "undefined"
        self.class_name = f"FTEST_launch.{self.directory}-{os.path.splitext(self.python_file)[0]}"
        self.host_info = HostInfo()
        self.yaml_info = {}
        self.extra_yaml = []

    def __str__(self):
        """Get the test file as a string.

        Returns:
            str: the test file
        """
        return self.test_file

    def set_yaml_info(self, logger, include_local_host=False):
        """Set the test yaml data from the test yaml file.

        Args:
            logger (logger): logger for the messages produced by this method
            include_local_host (bool, optional): whether or not the local host be included in the
                set of client hosts. Defaults to False.
        """
        self.yaml_info = {"include_local_host": include_local_host}
        yaml_data = get_yaml_data(self.yaml_file)
        info = {}
        for key in self.YAML_INFO_KEYS:
            # Get the unique values with lists flattened
            values = list_unique(list_flatten(dict_extract_values(yaml_data, [key], (str, list))))
            if values:
                # Use single value if list only contains 1 element
                info[key] = values if len(values) > 1 else values[0]

        logger.debug("Test yaml information for %s:", self.test_file)
        for key in self.YAML_INFO_KEYS:
            if key in (self.YAML_INFO_KEYS[0], self.YAML_INFO_KEYS[3]):
                self.yaml_info[key] = get_node_set(info[key] if key in info else None)
            else:
                self.yaml_info[key] = info[key] if key in info else None
            logger.debug("  %-18s = %s", key, self.yaml_info[key])

    def set_host_info(self, logger, control_node):
        """Set the test host information using the test yaml file.

        Args:
            logger (logger): logger for the messages produced by this method
            control_node (NodeSet): the slurm control node

        Raises:
            LaunchException: if there is an error getting the host from the test yaml or a problem
            setting up a slum partition
        """
        logger.debug("Using %s to define host information", self.yaml_file)
        if self.yaml_info["include_local_host"]:
            logger.debug("  Adding the localhost to the clients: %s", get_local_host())
        try:
            self.host_info.set_hosts(
                logger, control_node, self.yaml_info[self.YAML_INFO_KEYS[0]],
                self.yaml_info[self.YAML_INFO_KEYS[1]], self.yaml_info[self.YAML_INFO_KEYS[2]],
                self.yaml_info[self.YAML_INFO_KEYS[3]], self.yaml_info[self.YAML_INFO_KEYS[4]],
                self.yaml_info[self.YAML_INFO_KEYS[5]], self.yaml_info["include_local_host"])
        except HostException as error:
            raise LaunchException("Error getting hosts from {self.yaml_file}") from error

    def get_yaml_client_users(self):
        """Find all the users in the specified yaml file.

        Returns:
            list: list of (user, group) to create
        """
        yaml_data = get_yaml_data(self.yaml_file)
        return list_flatten(dict_extract_values(yaml_data, ["client_users"], list))

    def get_log_file(self, logs_dir, repeat, total):
        """Get the test log file name.

        Args:
            logs_dir (str): base directory in which to place the log file
            repeat (int): current test repetition
            total (int): total number of test repetitions

        Returns:
            str: a test log file name composed of the test class, name, and optional repeat count
        """
        name = os.path.splitext(self.python_file)[0]
        log_file = f"{self.name.order_str}-{self.directory}-{name}-launch.log"
        if total > 1:
            self.name.repeat = repeat
            os.makedirs(os.path.join(logs_dir, self.name.repeat_str), exist_ok=True)
            return os.path.join(logs_dir, self.name.repeat_str, log_file)
        return os.path.join(logs_dir, log_file)


class TestRunner():
    """Runs a test."""

    def __init__(self, avocado, launch_result, total_tests, total_repeats, tag_filters):
        """Initialize a FunctionalTest object.

        Args:
            avocado (AvocadoInfo): information about this version of avocado
            result (TestResult): _description_
        """
        self.avocado = avocado
        self.launch_result = launch_result
        self.test_result = None
        self.total_tests = total_tests
        self.total_repeats = total_repeats
        self.tag_filters = tag_filters
        self.local_host = get_local_host()

    def prepare(self, logger, test_log_file, test, repeat, user_create, slurm_setup, control_host,
                partition_hosts):
        """Prepare the test for execution.

        Args:
            logger (logger): logger for the messages produced by this method
            test (TestInfo): the test information
            repeat (str): the test repetition sequence, e.g. '1/10'
            user_create (bool): whether to create extra test users defined by the test

        Returns:
            int: status code: 0 = success, 128 = failure
        """
        logger.debug("=" * 80)
        logger.info(
            "Preparing to run the %s test on repeat %s/%s", test, repeat, self.total_repeats)

        # Create a new TestResult for this test
        self.test_result = self.launch_result.add_test(
            test.class_name, test.name.copy(), test_log_file)
        self.test_result.start()

        # Setup the test host information, including creating any required slurm partitions
        status = self._setup_host_information(
            logger, test, slurm_setup, control_host, partition_hosts)
        if status:
            return status

        # Setup (remove/create/list) the common test directory on each test host
        status = self._setup_test_directory(logger, test)
        if status:
            return status

        # Setup additional test users
        status = self._user_setup(logger, test, user_create)
        if status:
            return status

        # Generate certificate files for the test
        return self._generate_certs(logger)

    def execute(self, logger, test, repeat, number, sparse, fail_fast):
        """Run the specified test.

        Args:
            logger (logger): logger for the messages produced by this method
            test (TestInfo): the test information
            repeat (int): the test repetition number
            number (int): the test sequence number in this repetition
            sparse (bool): whether to use avocado sparse output
            fail_fast(bool): whether to use the avocado fail fast option

        Returns:
            int: status code: 0 = success, >0 = failure
        """
        # Avoid counting the test execution time as part of the processing time of this test
        self.test_result.end()

        logger.debug("=" * 80)
        command = self.avocado.get_run_command(test, self.tag_filters, sparse, fail_fast)
        logger.info(
            "[Test %s/%s] Running the %s test on repetition %s/%s",
            number, self.total_tests, test, repeat, self.total_repeats)
        start_time = int(time.time())

        try:
            return_code = run_local(
                logger, " ".join(command), capture_output=False, check=False).returncode
            if return_code == 0:
                logger.debug("All avocado test variants passed")
            elif return_code & 2 == 2:
                logger.debug("At least one avocado test variant failed")
            elif return_code & 4 == 4:
                message = "Failed avocado commands detected"
                self.test_result.fail_test(logger, "Execute", message)
            elif return_code & 8 == 8:
                logger.debug("At least one avocado test variant was interrupted")
            if return_code:
                self._collect_crash_files(logger)

        except RunException:
            message = f"Error executing {test} on repeat {repeat}"
            self.test_result.fail_test(logger, "Execute", message, sys.exc_info())
            return_code = 1

        end_time = int(time.time())
        logger.info("Total test time: %ss", end_time - start_time)
        return return_code

    def process(self, logger, job_results_dir, test, repeat, stop_daos, archive, rename,
                jenkins_xml, core_files, threshold):
        # pylint: disable=too-many-arguments
        """Process the test results.

        This may include (depending upon argument values):
            - Stopping any running servers or agents
            - Resetting the server storage
            - Archiving any files generated by the test and including them with the test results
            - Renaming the test results directory and results.xml entries
            - Processing any core files generated by the test

        Args:
            logger (logger): logger for the messages produced by this method
            test (TestInfo): the test information
            repeat (int): the test repetition number
            stop_daos (bool): whether or not to stop daos servers/clients after the test
            archive (bool): whether or not to collect remote files generated by the test
            rename (bool): whether or not to rename the default avocado job-results directory names
            jenkins_xml (bool): whether or not to update the results.xml to use Jenkins-style names
            core_files (dict): location and pattern defining where core files may be written
            threshold (str): optional upper size limit for test log files

        Returns:
            int: status code: 0 = success, >0 = failure
        """
        # Mark the continuation of the processing of this test
        self.test_result.start()

        logger.debug("=" * 80)
        logger.info(
            "Processing the %s test after the run on repeat %s/%s",
            test, repeat, self.total_repeats)
        status = collect_test_result(
            logger, test, self.test_result, job_results_dir, stop_daos, archive, rename,
            jenkins_xml, core_files, threshold, self.total_repeats)

        # Mark the execution of the test as passed if nothing went wrong
        if self.test_result.status is None:
            self.test_result.pass_test(logger)

        # Mark the end of the processing of this test
        self.test_result.end()

        return status

    def _setup_host_information(self, logger, test, slurm_setup, control_host, partition_hosts):
        """Set up the test host information and any required partitions.

        Args:
            logger (logger): logger for the messages produced by this method
            test (TestInfo): the test information
            slurm_setup (bool):
            control_host (NodeSet):
            partition_hosts (NodeSet):

        Returns:
            int: status code: 0 = success, 128 = failure
        """
        logger.debug("-" * 80)
        logger.debug("Setting up host information for %s", test)

        # Verify any required partitions exist
        if test.yaml_info["client_partition"]:
            partition = test.yaml_info["client_partition"]
            logger.debug("Determining if the %s client partition exists", partition)
            exists = show_partition(logger, control_host, partition).passed
            if not exists and not slurm_setup:
                message = f"Error missing {partition} partition"
                self.test_result.fail_test(logger, "Prepare", message, None)
                return 128
            if slurm_setup and exists:
                logger.info(
                    "Removing existing %s partition to ensure correct configuration", partition)
                if not delete_partition(logger, control_host, partition).passed:
                    message = f"Error removing existing {partition} partition"
                    self.test_result.fail_test(logger, "Prepare", message, None)
                    return 128
            if slurm_setup:
                hosts = partition_hosts.difference(test.yaml_info["test_servers"])
                logger.debug(
                    "Partition hosts from '%s', excluding test servers '%s': %s",
                    partition_hosts, test.yaml_info["test_servers"], hosts)
                if not hosts:
                    message = "Error no partition hosts exist after removing the test servers"
                    self.test_result.fail_test(logger, "Prepare", message, None)
                    return 128
                logger.info("Creating the '%s' partition with the '%s' hosts", partition, hosts)
                if not create_partition(logger, control_host, partition, hosts).passed:
                    message = f"Error adding the {partition} partition"
                    self.test_result.fail_test(logger, "Prepare", message, None)
                    return 128

        # Define the hosts for this test
        try:
            test.set_host_info(logger, control_host)
        except LaunchException:
            message = "Error setting up host information"
            self.test_result.fail_test(logger, "Prepare", message, sys.exc_info())
            return 128

        # Log the test information
        msg_format = "%3s  %-40s  %-60s  %-20s  %-20s"
        logger.debug("-" * 80)
        logger.debug("Test information:")
        logger.debug(msg_format, "UID", "Test", "Yaml File", "Servers", "Clients")
        logger.debug(msg_format, "-" * 3, "-" * 40, "-" * 60, "-" * 20, "-" * 20)
        logger.debug(
            msg_format, test.name.order, test.test_file, test.yaml_file,
            test.host_info.servers.hosts, test.host_info.clients.hosts)
        return 0

    def _setup_test_directory(self, logger, test):
        """Set up the common test directory on all hosts.

        Args:
            logger (logger): logger for the messages produced by this method
            test (TestInfo): the test information

        Returns:
            int: status code: 0 = success, 128 = failure
        """
        logger.debug("-" * 80)
        test_env = TestEnvironment()
        hosts = test.host_info.all_hosts
        hosts.add(self.local_host)
        logger.debug("Setting up '%s' on %s:", test_env.log_dir, hosts)
        commands = [
            f"sudo -n rm -fr {test_env.log_dir}",
            f"mkdir -p {test_env.log_dir}",
            f"chmod a+wrx {test_env.log_dir}",
            f"ls -al {test_env.log_dir}",
            f"mkdir -p {test_env.user_dir}"
        ]
        # Predefine the sub directories used to collect the files process()/_archive_files()
        for directory in TEST_RESULTS_DIRS:
            commands.append(f"mkdir -p {test_env.log_dir}/{directory}")
        for command in commands:
            if not run_remote(logger, hosts, command).passed:
                message = "Error setting up the common test directory on all hosts"
                self.test_result.fail_test(logger, "Prepare", message, sys.exc_info())
                return 128
        return 0

    def _user_setup(self, logger, test, create=False):
        """Set up test users on client nodes.

        Args:
            logger (logger): logger for the messages produced by this method
            test (TestInfo): the test information
            create (bool, optional): whether to create extra test users defined by the test

        Returns:
            int: status code: 0 = success, 128 = failure
        """
        users = test.get_yaml_client_users()
        clients = test.host_info.clients.hosts
        if users:
            logger.info('Setting up test users on %s', clients)

        # Keep track of queried groups to avoid redundant work
        group_gid = {}

        # Query and optionally create all groups and users
        for _user in users:
            user, *group = _user.split(':')
            group = group[0] if group else None

            # Save the group's gid
            if group and group not in group_gid:
                try:
                    group_gid[group] = self._query_create_group(clients, group, create)
                except LaunchException as error:
                    self.test_result.fail_test(logger, "Prepare", str(error), sys.exc_info())
                    return 128

            gid = group_gid.get(group, None)
            try:
                self._query_create_user(clients, user, gid, create)
            except LaunchException as error:
                self.test_result.fail_test(logger, "Prepare", str(error), sys.exc_info())
                return 128

        return 0

    @staticmethod
    def _query_create_group(logger, hosts, group, create=False):
        """Query and optionally create a group on remote hosts.

        Args:
            logger (logger): logger for the messages produced by this method
            hosts (NodeSet): hosts on which to query and create the group
            group (str): group to query and create
            create (bool, optional): whether to create the group if non-existent

        Raises:
            LaunchException: if there is an error querying or creating the group

        Returns:
            str: the group's gid
        """
        # Get the group id on each node
        logger.info('Querying group %s', group)
        group_ids = get_group_id(logger, hosts, group).keys()
        logger.debug('  found group_ids %s', group_ids)
        group_ids = list(group_ids)
        if len(group_ids) == 1 and group_ids[0] is not None:
            return group_ids[0]
        if not create:
            raise LaunchException(f'Group not setup correctly: {group}')

        # Create the group
        logger.info('Creating group %s', group)
        if not groupadd(hosts, group, True, True).passed:
            raise LaunchException(f'Error creating group {group}')

        # Get the group id on each node
        logger.info('Querying group %s', group)
        group_ids = get_group_id(logger, hosts, group).keys()
        logger.debug('  found group_ids %s', group_ids)
        group_ids = list(group_ids)
        if len(group_ids) == 1 and group_ids[0] is not None:
            return group_ids[0]
        raise LaunchException(f'Group not setup correctly: {group}')

    @staticmethod
    def _query_create_user(logger, hosts, user, gid=None, create=False):
        """Query and optionally create a user on remote hosts.

        Args:
            logger (logger): logger for the messages produced by this method
            hosts (NodeSet): hosts on which to query and create the group
            user (str): user to query and create
            gid (str, optional): user's primary gid. Default is None
            create (bool, optional): whether to create the group if non-existent. Default is False

        Raises:
            LaunchException: if there is an error querying or creating the user
        """
        logger.info('Querying user %s', user)
        groups = get_user_groups(logger, hosts, user)
        logger.debug('  found groups %s', groups)
        groups = list(groups)
        if len(groups) == 1 and groups[0] == gid:
            # Exists and in correct group
            return
        if not create:
            raise LaunchException(f'User {user} groups not as expected')

        # Delete and ignore errors, in case user account is inconsistent across nodes
        logger.info('Deleting user %s', user)
        _ = userdel(hosts, user, True)

        logger.info('Creating user %s in group %s', user, gid)
        test_env = TestEnvironment()
        if not useradd(hosts, user, gid, test_env.user_dir, True).passed:
            raise LaunchException(f'Error creating user {user}')

    def _generate_certs(self, logger):
        """Generate the certificates for the test.

        Returns:
            logger (logger): logger for the messages produced by this method
            int: status code: 0 = success, 128 = failure

        """
        logger.debug("-" * 80)
        logger.debug("Generating certificates")
        test_env = TestEnvironment()
        certs_dir = os.path.join(test_env.log_dir, "daosCA")
        certgen_dir = os.path.abspath(
            os.path.join("..", "..", "..", "..", "lib64", "daos", "certgen"))
        command = os.path.join(certgen_dir, "gen_certificates.sh")
        try:
            run_local(logger, f"/usr/bin/rm -rf {certs_dir}")
            run_local(logger, f"{command} {test_env.log_dir}")
        except RunException:
            message = "Error generating certificates"
            self.test_result.fail_test(logger, "Prepare", message, sys.exc_info())
            return 128
        return 0

    def _collect_crash_files(self, logger):
        """Move any avocado crash files into job-results/latest/crashes.

        Args:
            logger (logger): logger for the messages produced by this method
        """
        avocado_logs_dir = self.avocado.get_logs_dir(logger)
        crash_dir = os.path.join(avocado_logs_dir.replace("job-results", "data"), "crashes")
        if os.path.isdir(crash_dir):
            crash_files = [
                os.path.join(crash_dir, crash_file)
                for crash_file in os.listdir(crash_dir)
                if os.path.isfile(os.path.join(crash_dir, crash_file))]

            if crash_files:
                latest_crash_dir = os.path.join(avocado_logs_dir, "latest", "crashes")
                try:
                    run_local(logger, f"mkdir -p {latest_crash_dir}", check=True)
                    for crash_file in crash_files:
                        run_local(logger, f"mv {crash_file} {latest_crash_dir}", check=True)
                except RunException:
                    message = "Error collecting crash files"
                    self.test_result.fail_test(logger, "Execute", message, sys.exc_info())
            else:
                logger.debug("No avocado crash files found in %s", crash_dir)


class TestGroup():
    """Runs a group of tests with same configuration."""

    def __init__(self, index, avocado, test_env, servers, control, clients, tags, nvme,
                 yaml_directory=None, yaml_extension=None):
        """_summary_.

        Args:
            index (int): test group number
            avocado (AvocadoInfo): information about this version of avocado
            test_env (TestEnvironment): the test environment variables
            servers (NodeSet): hosts designated for the server role in tests
            clients (NodeSet): hosts designated for the client role in tests
            control (NodeSet): hosts designated for the control role in tests
            tags (list): a list of tags or test file names
            nvme (str): storage replacement keyword
            yaml_directory (str, optional): directory used to store modified test yaml files.
                Defaults to None.
            yaml_extension (str, optional): optional test yaml file extension to use when creating
                the TestInfo object. Defaults to None.
        """
        self._index = index
        self._avocado = avocado
        self._test_env = test_env
        self._servers = servers
        self._clients = clients
        self._control = control
        self._partition_hosts = NodeSet(self._servers or self._clients)
        self._tags = tags
        self._nvme = nvme
        self._yaml_directory = yaml_directory
        self._yaml_extension = yaml_extension

        self.tests = []
        self.tag_filters = []
        self._details = {"tags": self._tags, "nvme": self._nvme}

    @property
    def yaml_directory(self):
        """Get the directory used to store modified test yaml files.

        Returns:
            str: directory used to store modified test yaml files
        """
        return self._yaml_directory

    @yaml_directory.setter
    def yaml_directory(self, value=None):
        """Set the directory used to store modified test yaml files.

        Args:
            value (str, optional): directory used to store modified test yaml files
        """
        if value is None:
            # pylint: disable=consider-using-with
            temp_dir = TemporaryDirectory()
            self._yaml_directory = temp_dir.name
        else:
            self._yaml_directory = value
            if not os.path.exists(self._yaml_directory):
                os.mkdir(self._yaml_directory)

        # logger.info("Modified test yaml files being created in: %s", self._yaml_directory)

    @property
    def details(self):
        """Get the test group details.

        Returns:
            dict: test group details
        """
        return {'Group{self._index}': self._details}

    def list_tests(self, logger):
        """List the test files matching the tags.

        Populates the self.tests list and defines the self.tag_filters list to use when running
        tests.

        Args:
            logger (Logger): logger for the messages produced by this method

        Raises:
            RunException: if there is a problem listing tests
        """
        logger.debug("-" * 80)
        self.tests.clear()
        self.tag_filters.clear()

        # Determine if fault injection is enabled
        fault_tag = "-faults"
        fault_filter = f"--filter-by-tags={fault_tag}"
        faults_enabled = fault_injection_enabled(logger)

        # Determine if each tag list entry is a tag or file specification
        test_files = []
        for tag in self._tags:
            if os.path.isfile(tag):
                # Assume an existing file is a test and add it to the list of tests
                test_files.append(tag)
                if not faults_enabled and fault_filter not in self.tag_filters:
                    self.tag_filters.append(fault_filter)
            else:
                # Otherwise it is assumed that this is a tag
                if not faults_enabled:
                    tag = ",".join((tag, fault_tag))
                self.tag_filters.append(f"--filter-by-tags={tag}")

        # Get the avocado list command to find all the tests that match the specified files and tags
        command = self._avocado.get_list_command()
        command.extend(self.tag_filters)
        command.extend(test_files)
        if not test_files:
            command.append("./")

        # Find all the test files that contain tests matching the tags
        logger.debug("-" * 80)
        logger.info("Detecting tests matching tags: %s", " ".join(command))
        output = run_local(logger, " ".join(command), check=True)
        unique_test_files = set(re.findall(self._avocado.get_list_regex(), output.stdout))
        for index, test_file in enumerate(unique_test_files):
            self.tests.append(TestInfo(test_file, index + 1, self._yaml_extension))
            logger.info("  %s", self.tests[-1])

    def update_test_yaml(self, logger, scm_size, scm_mount, extra_yaml, multiplier, override,
                         verbose, include_localhost):
        """Update each test yaml file.

        Args:
            logger (Logger): logger for the messages produced by this method
            scm_size (_type_): _description_
            scm_mount (_type_): _description_
            extra_yaml (_type_): _description_
            multiplier (_type_): _description_
            override (_type_): _description_
            verbose (_type_): _description_
            include_localhost (_type_): _description_

        Raises:
            RunException: if there is an error modifying the test yaml files
            YamlException: if there is an error getting host information from the test yaml files
            StorageException: if there is an error creating the extra storage yaml files
        """
        storage = None
        storage_info = StorageInfo(logger, self._servers)
        tier_0_type = "pmem"
        control_metadata = None
        max_nvme_tiers = 1
        if self._nvme:
            kwargs = {"device_filter": f"'({'|'.join(self._nvme.split(','))})'"}
            if self._nvme.startswith("auto"):
                # Separate any optional filter from the key
                nvme_args = self._nvme.split(":")
                kwargs["device_filter"] = nvme_args[1] if len(nvme_args) > 1 else None
            logger.debug("-" * 80)
            storage_info.scan(**kwargs)

            # Determine which storage device types to use when replacing keywords in the test yaml
            if self._nvme.startswith("auto_nvme"):
                storage = ",".join([dev.address for dev in storage_info.disk_devices])
            elif self._nvme.startswith("auto_vmd") or storage_info.controller_devices:
                storage = ",".join([dev.address for dev in storage_info.controller_devices])
            else:
                storage = ",".join([dev.address for dev in storage_info.disk_devices])

            # Change the auto-storage extra yaml format if md_on_ssd is requested
            if self._nvme.startswith("auto_md_on_ssd"):
                tier_0_type = "ram"
                max_nvme_tiers = 5
                control_metadata = os.path.join(self._test_env.log_dir, 'control_metadata')

        self._details["storage"] = storage_info.device_dict()

        updater = YamlUpdater(
            logger, self._servers, self._clients, storage, multiplier, override, verbose)

        # Replace any placeholders in the extra yaml file, if provided
        if extra_yaml:
            logger.debug("Updating placeholders in extra yaml files: %s", extra_yaml)
            common_extra_yaml = [
                updater.update(extra, self._yaml_directory) or extra for extra in extra_yaml]
            for test in self.tests:
                test.extra_yaml.extend(common_extra_yaml)

        # Generate storage configuration extra yaml files if requested
        self._add_auto_storage_yaml(
            logger, storage_info, self._yaml_directory, tier_0_type, scm_size, scm_mount,
            max_nvme_tiers, control_metadata)

        # Replace any placeholders in the test yaml file
        for test in self.tests:
            new_yaml_file = updater.update(test.yaml_file, self._yaml_directory)
            if new_yaml_file:
                if verbose > 0:
                    # Optionally display a diff of the yaml file
                    run_local(logger, f"diff -y {test.yaml_file} {new_yaml_file}", check=False)
                test.yaml_file = new_yaml_file

            # Display the modified yaml file variants with debug
            command = ["avocado", "variants", "--mux-yaml", test.yaml_file]
            if test.extra_yaml:
                command.extend(test.extra_yaml)
            command.extend(["--summary", "3"])
            run_local(logger, " ".join(command))

            # Collect the host information from the updated test yaml
            test.set_yaml_info(logger, include_localhost)

    def _add_auto_storage_yaml(self, logger, storage_info, yaml_dir, tier_0_type, scm_size,
                               scm_mount, max_nvme_tiers, control_metadata):
        """Add extra storage yaml definitions for tests requesting automatic storage configurations.

        Args:
            storage_info (StorageInfo): the collected storage information from the hosts
            yaml_dir (str): path in which to create the extra storage yaml files
            tier_0_type (str): storage tier 0 type to define; 'pmem' or 'ram'
            scm_size (int): scm_size to use with ram storage tiers
            scm_mount (str): the base path for the storage tier 0 scm_mount.
            max_nvme_tiers (int): maximum number of NVMe tiers to generate
            control_metadata (str, optional): directory to store control plane metadata when using
                metadata on SSD.

        Raises:
            YamlException: if there is an error getting host information from the test yaml files
            StorageException: if there is an error creating the extra storage yaml files
        """
        engine_storage_yaml = {}
        for test in self.tests:
            yaml_data = get_yaml_data(test.yaml_file)
            logger.debug("Checking for auto-storage request in %s", test.yaml_file)

            storage = dict_extract_values(yaml_data, ["server_config", "engines", "*", "storage"])
            if "auto" in storage:
                if len(list_unique(storage)) > 1:
                    raise StorageException("storage: auto only supported for all or no engines")
                engines = list_unique(dict_extract_values(yaml_data, ["engines_per_host"]))
                if len(engines) > 1:
                    raise StorageException(
                        "storage: auto not supported for varying engines_per_host")
                engines = engines[0]
                yaml_file = os.path.join(yaml_dir, f"extra_yaml_storage_{engines}_engine.yaml")
                if engines not in engine_storage_yaml:
                    logger.debug("-" * 80)
                    storage_info.write_storage_yaml(
                        yaml_file, engines, tier_0_type, scm_size, scm_mount, max_nvme_tiers,
                        control_metadata)
                    engine_storage_yaml[engines] = yaml_file
                logger.debug(
                    "  - Adding auto-storage extra yaml %s for %s",
                    engine_storage_yaml[engines], str(test))
                # Allow extra yaml files to be to override the generated storage yaml
                test.extra_yaml.insert(0, engine_storage_yaml[engines])

    def setup_slurm(self, logger, setup, install, user, result):
        """Set up slurm on the hosts if any tests are using partitions.

        Args:
            logger (Logger): logger for the messages produced by this method
            setup (bool): whether or not to setup slurm
            install (bool): whether or not to install slurm
            user (str): user account to use with slurm

        Returns:
            int: status code: 0 = success, 128 = failure
        """
        status = 0
        logger.debug("-" * 80)
        logger.info("Setting up slurm partitions if required by tests")
        if not any(test.yaml_info["client_partition"] for test in self.tests):
            logger.debug("  No tests using client partitions detected - skipping slurm setup")
            return status

        if not setup:
            logger.debug("  The 'slurm_setup' argument is not set - skipping slurm setup")
            return status

        status |= self._setup_application_directory(logger, result)

        slurm_setup = SlurmSetup(logger, self._partition_hosts, self._control, True)
        logger.debug("-" * 80)
        try:
            if install:
                slurm_setup.install()
            slurm_setup.update_config(user, 'daos_client')
            slurm_setup.start_munge(user)
            slurm_setup.start_slurm(user, True)
        except SlurmSetupException:
            message = "Error setting up slurm"
            result.tests[-1].fail_test(logger, "Run", message, sys.exc_info())
            status |= 128
        except Exception:       # pylint: disable=broad-except
            message = "Unknown error setting up slurm"
            result.tests[-1].fail_test(logger, "Run", message, sys.exc_info())
            status |= 128

        return status

    def _setup_application_directory(self, logger, result):
        """Set up the application directory.

        Args:
            logger (Logger): logger for the messages produced by this method

        Returns:
            int: status code: 0 = success, 128 = failure
        """
        logger.debug("-" * 80)
        logger.debug("Setting up the '%s' application directory", self._test_env.app_dir)
        if not os.path.exists(self._test_env.app_dir):
            # Create the apps directory if it does not already exist
            try:
                logger.debug('  Creating the application directory')
                os.makedirs(self._test_env.app_dir)
            except OSError:
                message = 'Error creating the application directory'
                result.tests[-1].fail_test(logger, 'Run', message, sys.exc_info())
                return 128
        else:
            logger.debug('  Using the existing application directory')

        if self._test_env.app_src and os.path.exists(self._test_env.app_src):
            logger.debug("  Copying applications from the '%s' directory", self._test_env.app_src)
            run_local(logger, f"ls -al '{self._test_env.app_src}'")
            for app in os.listdir(self._test_env.app_src):
                try:
                    run_local(
                        logger,
                        f"cp -r '{os.path.join(self._test_env.app_src, app)}' "
                        f"'{self._test_env.app_dir}'",
                        check=True)
                except RunException:
                    message = 'Error copying files to the application directory'
                    result.tests[-1].fail_test(logger, 'Run', message, sys.exc_info())
                    return 128

        logger.debug("  Applications in '%s':", self._test_env.app_dir)
        run_local(logger, f"ls -al '{self._test_env.app_dir}'")
        return 0

    def run_tests(self, logger, result, repeat, setup, sparse, fail_fast, stop_daos, archive,
                  rename, jenkins_log, core_files, threshold, user_create, code_coverage,
                  job_results_dir, logdir):
        # pylint: disable=too-many-arguments
        """Run all the tests.

        Args:
            logger (Logger): logger for the messages produced by this method
            mode (str): launch mode
            sparse (bool): whether or not to display the shortened avocado test output
            fail_fast (bool): whether or not to fail the avocado run command upon the first failure
            stop_daos (bool): whether or not to stop daos servers/clients after the test
            archive (bool): whether or not to collect remote files generated by the test
            rename (bool): whether or not to rename the default avocado job-results directory names
            jenkins_log (bool): whether or not to update the results.xml to use Jenkins-style names
            core_files (dict): location and pattern defining where core files may be written
            threshold (str): optional upper size limit for test log files
            user_create (bool): whether to create extra test users defined by the test
            code_coverage (CodeCoverage): bullseye code coverage

        Returns:
            int: status code indicating any issues running tests
        """
        return_code = 0
        runner = TestRunner(self._avocado, result, len(self.tests), repeat, self.tag_filters)

        # Display the location of the avocado logs
        logger.info("Avocado job results directory: %s", job_results_dir)

        # Configure hosts to collect code coverage
        if not code_coverage.setup(logger, result.tests[0]):
            return_code |= 128

        # Run each test for as many repetitions as requested
        for loop in range(1, repeat + 1):
            logger.info("-" * 80)
            logger.info("Starting test repetition %s/%s", loop, repeat)

            for index, test in enumerate(self.tests):
                # Define a log for the execution of this test for this repetition
                test_log_file = test.get_log_file(logdir, loop, repeat)
                logger.info("-" * 80)
                logger.info("Log file for repetition %s of %s: %s", loop, test, test_log_file)
                test_file_handler = get_file_handler(test_log_file, LOG_FILE_FORMAT, logging.DEBUG)
                logger.addHandler(test_file_handler)

                # Prepare the hosts to run the tests
                step_status = runner.prepare(
                    logger, test_log_file, test, loop, user_create, setup, self._control,
                    self._partition_hosts)
                if step_status:
                    # Do not run this test - update its failure status to interrupted
                    return_code |= step_status
                    continue

                # Run the test with avocado
                return_code |= runner.execute(logger, test, loop, index + 1, sparse, fail_fast)

                # Archive the test results
                return_code |= runner.process(
                    logger, job_results_dir, test, loop, stop_daos, archive, rename,
                    jenkins_log, core_files, threshold)

                # Display disk usage after the test is complete
                display_disk_space(logger, logdir)

                # Stop logging to the test log file
                logger.removeHandler(test_file_handler)

        # Collect code coverage files after all test have completed
        if not code_coverage.finalize(logger, job_results_dir, result.tests[0]):
            return_code |= 16

        # Summarize the run
        return return_code
