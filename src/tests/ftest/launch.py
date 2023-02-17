#!/usr/bin/env python3
"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines

from argparse import ArgumentParser, RawDescriptionHelpFormatter
from collections import OrderedDict, defaultdict
from tempfile import TemporaryDirectory
import errno
import json
import logging
import os
import re
import site
import sys
import time
import yaml

# When SRE-439 is fixed we should be able to include these import statements here
# from avocado.core.settings import settings
# from avocado.core.version import MAJOR, MINOR
# from avocado.utils.stacktrace import prepare_exc_info
from ClusterShell.NodeSet import NodeSet

# When SRE-439 is fixed we should be able to include these import statements here
# from util.distro_utils import detect
# pylint: disable=import-error,no-name-in-module
from process_core_files import CoreFileProcessing, CoreFileException

# Update the path to support utils files that import other utils files
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "util"))
# pylint: disable=import-outside-toplevel
from host_utils import get_node_set, get_local_host, HostInfo, HostException  # noqa: E402
from logger_utils import get_console_handler, get_file_handler                # noqa: E402
from results_utils import create_html, create_xml, Job, Results, TestResult   # noqa: E402
from run_utils import run_local, run_remote, find_command, RunException       # noqa: E402
from slurm_utils import show_partition, create_partition, delete_partition    # noqa: E402
from user_utils import get_chown_command                                      # noqa: E402

BULLSEYE_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test.cov")
BULLSEYE_FILE = os.path.join(os.sep, "tmp", "test.cov")
DEFAULT_DAOS_APP_DIR = os.path.join(os.sep, "scratch")
DEFAULT_DAOS_TEST_LOG_DIR = os.path.join(os.sep, "var", "tmp", "daos_testing")
DEFAULT_DAOS_TEST_SHARED_DIR = os.path.expanduser(os.path.join("~", "daos_test"))
DEFAULT_LOGS_THRESHOLD = "2150M"    # 2.1G
FAILURE_TRIGGER = "00_trigger-launch-failure_00"
LOG_FILE_FORMAT = "%(asctime)s %(levelname)-5s %(funcName)30s: %(message)s"
MAX_CI_REPETITIONS = 10
TEST_EXPECT_CORE_FILES = ["./harness/core_files.py"]
PROVIDER_KEYS = OrderedDict(
    [
        ("cxi", "ofi+cxi"),
        ("verbs", "ofi+verbs"),
        ("ucx", "ucx+dc_x"),
        ("tcp", "ofi+tcp"),
    ]
)
YAML_KEYS = OrderedDict(
    [
        ("test_servers", "test_servers"),
        ("test_clients", "test_clients"),
        ("bdev_list", "nvme"),
        ("timeout", "timeout_multiplier"),
        ("timeouts", "timeout_multiplier"),
        ("clush_timeout", "timeout_multiplier"),
        ("ior_timeout", "timeout_multiplier"),
        ("job_manager_timeout", "timeout_multiplier"),
        ("pattern_timeout", "timeout_multiplier"),
        ("pool_query_timeout", "timeout_multiplier"),
        ("rebuild_timeout", "timeout_multiplier"),
        ("srv_timeout", "timeout_multiplier"),
        ("storage_prepare_timeout", "timeout_multiplier"),
        ("storage_format_timeout", "timeout_multiplier"),
    ]
)
PROCS_TO_CLEANUP = ["cart_ctl", "orterun", "mpirun", "dfuse"]
TYPES_TO_UNMOUNT = ["fuse.daos"]


# Set up a logger for the console messages. Initially configure the console handler to report debug
# messages until a file logger can be established to handle the debug messages. After which the
# console logger will be updated to handle info messages.
logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)
logger.addHandler(get_console_handler("%(message)s", logging.DEBUG))


class LaunchException(Exception):
    """Exception for launch.py execution."""


def get_yaml_data(yaml_file):
    """Get the contents of a yaml file as a dictionary.

    Removes any mux tags and ignores any other tags present.

    Args:
        yaml_file (str): yaml file to read

    Raises:
        Exception: if an error is encountered reading the yaml file

    Returns:
        dict: the contents of the yaml file

    """
    class DaosLoader(yaml.SafeLoader):  # pylint: disable=too-many-ancestors
        """Helper class for parsing avocado yaml files."""

        def forward_mux(self, node):
            """Pass on mux tags unedited."""
            return self.construct_mapping(node)

        def ignore_unknown(self, node):  # pylint: disable=no-self-use,unused-argument
            """Drop any other tag."""
            return None

    DaosLoader.add_constructor('!mux', DaosLoader.forward_mux)
    DaosLoader.add_constructor(None, DaosLoader.ignore_unknown)

    yaml_data = {}
    if os.path.isfile(yaml_file):
        with open(yaml_file, "r", encoding="utf-8") as open_file:
            try:
                yaml_data = yaml.load(open_file.read(), Loader=DaosLoader)  # nosec
            except yaml.YAMLError as error:
                logger.error("Error reading %s: %s", yaml_file, str(error))
                sys.exit(1)
    return yaml_data


def find_values(obj, keys, key=None, val_type=str):
    """Find dictionary values of a certain type specified with certain keys.

    Args:
        obj (obj): a python object; initially the dictionary to search
        keys (list): list of keys to find their matching list values
        key (str, optional): key to check for a match. Defaults to None.

    Returns:
        dict: a dictionary of each matching key and its value

    """
    def add_matches(found):
        """Add found matches to the match dictionary entry of the same key.

        If a match does not already exist for this key add all the found values.
        When a match already exists for a key, append the existing match with
        any new found values.

        For example:
            Match       Found           Updated Match
            ---------   ------------    -------------
            None        [A, B]          [A, B]
            [A, B]      [C]             [A, B, C]
            [A, B, C]   [A, B, C, D]    [A, B, C, D]

        Args:
            found (dict): dictionary of matches found for each key
        """
        for found_key in found:
            if found_key not in matches:
                # Simply add the new value found for this key
                matches[found_key] = found[found_key]

            else:
                is_list = isinstance(matches[found_key], list)
                if not is_list:
                    matches[found_key] = [matches[found_key]]
                if isinstance(found[found_key], list):
                    for found_item in found[found_key]:
                        if found_item not in matches[found_key]:
                            matches[found_key].append(found_item)
                elif found[found_key] not in matches[found_key]:
                    matches[found_key].append(found[found_key])

                if not is_list and len(matches[found_key]) == 1:
                    matches[found_key] = matches[found_key][0]

    matches = {}
    if isinstance(obj, val_type) and isinstance(key, str) and key in keys:
        # Match found
        matches[key] = obj
    elif isinstance(obj, dict):
        # Recursively look for matches in each dictionary entry
        for obj_key, obj_val in list(obj.items()):
            add_matches(find_values(obj_val, keys, obj_key, val_type))
    elif isinstance(obj, list):
        # Recursively look for matches in each list entry
        for item in obj:
            add_matches(find_values(item, keys, None, val_type))

    return matches


def get_test_category(test_file):
    """Get a category for the specified test using its path and name.

    Args:
        test_file (str): the test python file

    Returns:
        str: concatenation of the test path and base filename joined by dashes

    """
    file_parts = os.path.split(test_file)
    return "-".join([os.path.splitext(os.path.basename(part))[0] for part in file_parts])


def find_pci_address(value):
    """Find PCI addresses in the specified string.

    Args:
        value (str): string to search for PCI addresses

    Returns:
        list: a list of all the PCI addresses found in the string

    """
    digit = "0-9a-fA-F"
    pattern = rf"[{digit}]{{4,5}}:[{digit}]{{2}}:[{digit}]{{2}}\.[{digit}]"
    return re.findall(pattern, str(value))


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
        config_dir = os.path.expanduser(os.path.join("~", ".config", "avocado"))
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

    def set_version(self):
        """Set the avocado major and minor versions.

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
    def get_setting(section, key, default=None):
        """Get the value for the specified avocado setting.

        Args:
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

    def get_logs_dir(self):
        """Get the avocado directory in which the test results are stored.

        Returns:
            str: the directory used by avocado to log test results

        """
        default_base_dir = os.path.join("~", "avocado", "job-results")
        return os.path.expanduser(self.get_setting("datadir.paths", "logs_dir", default_base_dir))

    def get_directory(self, directory, create=True):
        """Get the avocado test directory for the test.

        Args:
            directory (str): name of the sub directory to add to the logs directory
            create (bool, optional): whether or not to create the directory if it doesn't exist.
                Defaults to True.

        Returns:
            str: the directory used by avocado to log test results

        """
        logs_dir = self.get_logs_dir()
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

    def get_run_command(self, test, tag_filters, sparse, failfast, extra_yaml):
        """Get the avocado run command for this version of avocado.

        Args:
            test (TestInfo): the test information
            tag_filters (list): optional '--filter-by-tags' arguments
            sparse (bool): whether or not to provide sparse output of the test execution
            failfast (bool): _description_
            extra_yaml (list): additional yaml files to include on the command line

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
        if extra_yaml:
            command.extend(extra_yaml)
        command.extend(["--", str(test)])
        return command


class TestName():
    """Define a test name compatible with avocado's result render classes."""

    def __init__(self, name, order, repeat):
        """Initialize a TestName object.

        Args:
            name (str): test name
            order (int): order in which this test is executed
            repeat (int): repeat count for this test
        """
        self.name = name
        self.order = order
        self.repeat = repeat

    def __str__(self):
        """Get the test name as a string.

        Returns:
            str: combination of the order and name

        """
        if self.repeat > 0:
            return f"{self.uid}-{self.name}{self.variant}"
        return f"{self.uid}-{self.name}"

    def __getitem__(self, name, default=None):
        """Get the value of the attribute name.

        Args:
            name (str): name of the class attribute to get
            default (object, optional): value to return if name is not defined. Defaults to None.

        Returns:
            object: the attribute value or default if not defined

        """
        return self.get(name, default)

    def get(self, name, default=None):
        """Get the value of the attribute name.

        Args:
            name (str): name of the class attribute to get
            default (object, optional): value to return if name is not defined. Defaults to None.

        Returns:
            object: the attribute value or default if not defined

        """
        try:
            return getattr(self, name, default)
        except TypeError:
            return default

    @property
    def order_str(self):
        """Get the string representation of the order count.

        Returns:
            str: the order count as a string

        """
        return f"{self.order:02}"

    @property
    def repeat_str(self):
        """Get the string representation of the repeat count.

        Returns:
            str: the repeat count as a string

        """
        return f"repeat{self.repeat:03}"

    @property
    def uid(self):
        """Get the test order to use as the test uid for xml/html results.

        Returns:
            str: the test uid (order)

        """
        return self.order_str

    @property
    def variant(self):
        """Get the test repeat count as the test variant for xml/html results.

        Returns:
            str: the test variant (repeat)

        """
        return f";{self.repeat_str}"

    def copy(self):
        """Create a copy of this object.

        Returns:
            TestName: a copy of this TestName object

        """
        return TestName(self.name, self.order, self.repeat)


class TestInfo():
    """Defines the python test file and its associated test yaml file."""

    YAML_INFO_KEYS = [
        "test_servers",
        "server_partition",
        "server_reservation",
        "test_clients",
        "client_partition",
        "client_reservation",
        "client_users"
    ]

    def __init__(self, test_file, order):
        """Initialize a TestInfo object.

        Args:
            test_file (str): the test python file
            order (int): order in which this test is executed
        """
        self.name = TestName(test_file, order, 0)
        self.test_file = test_file
        self.yaml_file = ".".join([os.path.splitext(self.test_file)[0], "yaml"])
        parts = self.test_file.split(os.path.sep)[1:]
        self.python_file = parts.pop()
        self.directory = os.path.join(*parts)
        self.class_name = f"FTEST_launch.{self.directory}-{os.path.splitext(self.python_file)[0]}"
        self.host_info = HostInfo()
        self.yaml_info = {}

    def __str__(self):
        """Get the test file as a string.

        Returns:
            str: the test file

        """
        return self.test_file

    def set_yaml_info(self, include_local_host=False):
        """Set the test yaml data from the test yaml file.

        Args:
            include_local_host (bool, optional): whether or not the local host be included in the
                set of client hosts. Defaults to False.
        """
        self.yaml_info = {"include_local_host": include_local_host}
        yaml_data = get_yaml_data(self.yaml_file)
        info = find_values(yaml_data, self.YAML_INFO_KEYS, (str, list))

        logger.debug("Test yaml information for %s:", self.test_file)
        for key in self.YAML_INFO_KEYS:
            if key in (self.YAML_INFO_KEYS[0], self.YAML_INFO_KEYS[3]):
                self.yaml_info[key] = get_node_set(info[key] if key in info else None)
            else:
                self.yaml_info[key] = info[key] if key in info else None
            logger.debug("  %-18s = %s", key, self.yaml_info[key])

    def set_host_info(self, control_node):
        """Set the test host information using the test yaml file.

        Args:
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


class Launch():
    """Class to launch avocado tests."""

    def __init__(self, name, mode):
        """Initialize a Launch object.

        Args:
            name (str): launch job name
            mode (str): execution mode, e.g. "normal", "manual", or "ci"
        """
        self.name = name
        self.mode = mode

        self.avocado = AvocadoInfo()
        self.class_name = f"FTEST_launch.launch-{self.name.lower().replace('.', '-')}"
        self.logdir = None
        self.logfile = None
        self.tests = []
        self.tag_filters = []
        self.repeat = 1
        self.local_host = get_local_host()

        # Results tracking settings
        self.job_results_dir = None
        self.job = None
        self.result = None

        # Options for bullseye code coverage
        self.bullseye_hosts = NodeSet()

        # Details about the run
        self.details = OrderedDict()

        # Options for creating slurm partitions
        self.slurm_control_node = NodeSet()
        self.slurm_partition_hosts = NodeSet()
        self.slurm_add_partition = False

    def _start_test(self, class_name, test_name, log_file):
        """Start a new test result.

        Args:
            class_name (str): the test class name
            test_name (TestName): the test uid, name, and variant
            log_file (str): the log file for a single test

        Returns:
            TestResult: the test result for this test

        """
        # Create a new TestResult for this test
        self.result.tests.append(TestResult(class_name, test_name, log_file, self.logdir))

        # Mark the start of the processing of this test
        self.result.tests[-1].start()

        return self.result.tests[-1]

    def _end_test(self, test_result, message, fail_class=None, exc_info=None):
        """Mark the end of the test result.

        Args:
            test_result (TestResult): the test result to complete
            message (str): exit message or reason for failure
            fail_class (str, optional): failure category.
            exc_info (OptExcInfo, optional): return value from sys.exc_info().
        """
        if fail_class is None:
            self._pass_test(test_result, message)
        else:
            self._fail_test(test_result, fail_class, message, exc_info)
        if test_result:
            test_result.end()

    def _pass_test(self, test_result, message=None):
        """Set the test result as passed.

        Args:
            test_result (TestResult): the test result to mark as passed
            message (str, optional): explanation of test passing. Defaults to None.
        """
        if message is not None:
            logger.debug(message)
        self.__set_test_status(test_result, TestResult.PASS, None, None)

    def _warn_test(self, test_result, fail_class, fail_reason, exc_info=None):
        """Set the test result as warned.

        Args:
            test_result (TestResult): the test result to mark as warned
            fail_class (str): failure category.
            fail_reason (str): failure description.
            exc_info (OptExcInfo, optional): return value from sys.exc_info(). Defaults to None.
        """
        logger.warning(fail_reason)
        self.__set_test_status(test_result, TestResult.WARN, fail_class, fail_reason, exc_info)

    def _fail_test(self, test_result, fail_class, fail_reason, exc_info=None):
        """Set the test result as failed.

        Args:
            test_result (TestResult): the test result to mark as failed
            fail_class (str): failure category.
            fail_reason (str): failure description.
            exc_info (OptExcInfo, optional): return value from sys.exc_info(). Defaults to None.
        """
        logger.error(fail_reason)
        self.__set_test_status(test_result, TestResult.ERROR, fail_class, fail_reason, exc_info)

    @staticmethod
    def __set_test_status(test_result, status, fail_class, fail_reason, exc_info=None):
        """Set the test result.

        Args:
            test_result (TestResult): the test result to mark as failed
            status (str): TestResult status to set.
            fail_class (str): failure category.
            fail_reason (str): failure description.
            exc_info (OptExcInfo, optional): return value from sys.exc_info(). Defaults to None.
        """
        if exc_info is not None:
            logger.debug("Stacktrace", exc_info=True)
        if not test_result:
            return

        if status == TestResult.PASS:
            # Do not override a possible WARN status
            if test_result.status is None:
                test_result.status = status
            return

        if test_result.fail_count == 0 \
                or test_result.status == TestResult.WARN and status == TestResult.ERROR:
            # Update the test result with the information about the first ERROR.
            # Elevate status from WARN to ERROR if WARN came first.
            test_result.status = status
            test_result.fail_class = fail_class
            test_result.fail_reason = fail_reason
            if exc_info is not None:
                try:
                    # pylint: disable=import-outside-toplevel
                    from avocado.utils.stacktrace import prepare_exc_info
                    test_result.traceback = prepare_exc_info(exc_info)
                except Exception:       # pylint: disable=broad-except
                    pass

        if test_result.fail_count > 0:
            # Additional ERROR/WARN only update the test result fail reason with a fail counter
            plural = "s" if test_result.fail_count > 1 else ""
            fail_reason = test_result.fail_reason.split(" (+")[0:1]
            fail_reason.append(f"{test_result.fail_count} other failure{plural})")
            test_result.fail_reason = " (+".join(fail_reason)

        test_result.fail_count += 1

    def get_exit_status(self, status, message, fail_class=None, exc_info=None):
        """Get the exit status for the current mode.

        Also update the overall test result.

        Args:
            status (int): the exit status to use for non-ci mode operation
            message (str): exit message or reason for failure
            fail_class (str, optional): failure category.
            exc_info (OptExcInfo, optional): return value from sys.exc_info().

        Returns:
            int: the exit status

        """
        # Mark the end of the test result for any non-test execution steps
        if self.result and self.result.tests:
            self._end_test(self.result.tests[0], message, fail_class, exc_info)
        else:
            self._end_test(None, message, fail_class, exc_info)

        # Write the details to a json file
        self._write_details_json()

        if self.job and self.result:
            # Generate a results.xml for this run
            results_xml_path = os.path.join(self.logdir, "results.xml")
            try:
                logger.debug("Creating results.xml: %s", results_xml_path)
                create_xml(self.job, self.result)
            except ModuleNotFoundError as error:
                # When SRE-439 is fixed this should be an error
                logger.warning("Unable to create results.xml file: %s", str(error))
            else:
                if not os.path.exists(results_xml_path):
                    logger.error("results.xml does not exist: %s", results_xml_path)

            # Generate a results.html for this run
            results_html_path = os.path.join(self.logdir, "results.html")
            try:
                logger.debug("Creating results.html: %s", results_html_path)
                create_html(self.job, self.result)
            except ModuleNotFoundError as error:
                # When SRE-439 is fixed this should be an error
                logger.warning("Unable to create results.html file: %s", str(error))
            else:
                if not os.path.exists(results_html_path):
                    logger.error("results.html does not exist: %s", results_html_path)

        # Set the return code for the program based upon the mode and the provided status
        #   - always return 0 in CI mode since errors will be reported via the results.xml file
        return 0 if self.mode == "ci" else status

    def _write_details_json(self):
        """Write the details to a json file."""
        if self.logdir:
            details_json = os.path.join(self.logdir, "details.json")
            try:
                with open(details_json, "w", encoding="utf-8") as details:
                    details.write(json.dumps(self.details, indent=4))
            except TypeError as error:
                logger.error("Error writing %s: %s", details_json, str(error))

    def run(self, args):
        # pylint: disable=too-many-return-statements
        """Perform the actions specified by the command line arguments.

        Args:
            args (argparse.Namespace): command line arguments for this program

        Returns:
            int: exit status for the steps executed

        """
        # Setup the avocado config files to ensure these files are read by avocado
        try:
            self.avocado.set_config(args.overwrite_config)
        except LaunchException:
            message = "Error creating avocado config files"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())

        # Setup launch to log and run the requested action
        try:
            self._configure()
        except LaunchException:
            message = "Error configuring launch.py to start logging and track test results"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())

        # Add a test result to account for any non-test execution steps
        setup_result = self._start_test(
            self.class_name, TestName("./launch.py", 0, 0), self.logfile)

        # Set the number of times to repeat execution of each test
        if "ci" in self.mode and args.repeat > MAX_CI_REPETITIONS:
            message = "The requested number of test repetitions exceeds the CI limitation."
            self._warn_test(setup_result, "Setup", message)
            logger.debug(
                "The number of test repetitions has been reduced from %s to %s.",
                args.repeat, MAX_CI_REPETITIONS)
            args.repeat = MAX_CI_REPETITIONS
        self.repeat = args.repeat

        # Record the command line arguments
        logger.debug("Arguments:")
        for key in sorted(args.__dict__.keys()):
            logger.debug("  %s = %s", key, getattr(args, key))

        # Convert host specifications into NodeSets
        try:
            args.test_servers = NodeSet(args.test_servers)
        except TypeError:
            message = f"Invalid '--test_servers={args.test_servers}' argument"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())
        try:
            args.test_clients = NodeSet(args.test_clients)
        except TypeError:
            message = f"Invalid '--test_clients={args.test_clients}' argument"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())

        # A list of server hosts is required
        if not args.test_servers and not args.list:
            return self.get_exit_status(1, "Missing required '--test_servers' argument", "Setup")
        logger.info("Testing with hosts:       %s", args.test_servers.union(args.test_clients))
        self.details["test hosts"] = str(args.test_servers.union(args.test_clients))

        # Setup the user environment
        try:
            self._set_test_environment(
                args.test_servers, args.test_clients, args.list, args.provider, args.insecure_mode)
        except LaunchException as error:
            return self.get_exit_status(1, str(error), "Setup", sys.exc_info())

        # Auto-detect nvme test yaml replacement values if requested
        if args.nvme and args.nvme.startswith("auto") and not args.list:
            try:
                args.nvme = self._get_device_replacement(args.test_servers, args.nvme)
            except LaunchException:
                message = "Error auto-detecting NVMe test yaml file replacement values"
                return self.get_exit_status(1, message, "Setup", sys.exc_info())
        elif args.nvme and args.nvme.startswith("vmd:"):
            args.nvme = args.nvme.replace("vmd:", "")

        # Process the tags argument to determine which tests to run - populates self.tests
        try:
            self.list_tests(args.tags)
        except RunException:
            message = f"Error detecting tests that match tags: {' '.join(args.tags)}"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())

        # Verify at least one test was requested
        if not self.tests:
            message = f"No tests found for tags: {' '.join(args.tags)}"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())

        # Done if just listing tests matching the tags
        if args.list and not args.modify:
            return self.get_exit_status(0, "Listing tests complete")

        # Create a temporary directory
        if args.yaml_directory is None:
            # pylint: disable=consider-using-with
            temp_dir = TemporaryDirectory()
            yaml_dir = temp_dir.name
        else:
            yaml_dir = args.yaml_directory
            if not os.path.exists(yaml_dir):
                os.mkdir(yaml_dir)
        logger.info("Modified test yaml files being created in: %s", yaml_dir)

        # Modify the test yaml files to run on this cluster
        try:
            self.setup_test_files(args, yaml_dir)
        except (RunException, LaunchException):
            message = "Error modifying the test yaml files"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())
        if args.modify:
            return self.get_exit_status(0, "Modifying test yaml files complete")

        # Get the core file pattern information
        try:
            core_files = self._get_core_file_pattern(
                args.test_servers, args.test_clients, args.process_cores)
        except LaunchException:
            message = "Error obtaining the core file pattern information"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())

        # Split the timer for the test result to account for any non-test execution steps as not to
        # double report the test time accounted for in each individual test result
        setup_result.end()

        # Determine if bullseye code coverage collection is enabled
        logger.debug("Checking for bullseye code coverage configuration")
        # pylint: disable=unsupported-binary-operation
        self.bullseye_hosts = args.test_servers | get_local_host()
        result = run_remote(logger, self.bullseye_hosts, " ".join(["ls", "-al", BULLSEYE_SRC]))
        if not result.passed:
            logger.info(
                "Bullseye code coverage collection not configured on %s", self.bullseye_hosts)
            self.bullseye_hosts = NodeSet()
        else:
            logger.info("Bullseye code coverage collection configured on %s", self.bullseye_hosts)

        # Define options for creating any slurm partitions required by the tests
        try:
            self.slurm_control_node = NodeSet(args.slurm_control_node)
        except TypeError:
            message = f"Invalid '--slurm_control_node={args.slurm_control_node}' argument"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())
        self.slurm_partition_hosts.add(args.test_clients or args.test_servers)
        self.slurm_add_partition = args.slurm_setup

        # Execute the tests
        status = self.run_tests(
            args.sparse, args.failfast, args.extra_yaml, not args.disable_stop_daos, args.archive,
            args.rename, args.jenkinslog, core_files, args.logs_threshold)

        # Restart the timer for the test result to account for any non-test execution steps
        setup_result.start()

        # Return the appropriate return code and mark the test result to account for any non-test
        # execution steps complete
        return self.get_exit_status(status, "Executing tests complete")

    def _configure(self):
        """Configure launch to start logging and track test results.

        Raises:
            LaunchException: if there are any issues obtaining data from avocado commands

        """
        # Configure the logfile
        self.avocado.set_version()
        self.logdir = self.avocado.get_directory(os.path.join("launch", self.name.lower()), False)
        self.logfile = os.path.join(self.logdir, "job.log")

        # Rename the launch log directory if one exists
        renamed_log_dir = self._create_log_dir()

        # Setup a file handler to handle debug messages
        logger.addHandler(get_file_handler(self.logfile, LOG_FILE_FORMAT, logging.DEBUG))

        # Update the console logger to handle info messages
        logger.handlers[0].setLevel(logging.INFO)

        logger.info("-" * 80)
        logger.info("DAOS functional test launcher")
        logger.info("")
        logger.info("Running with %s", self.avocado)
        logger.info("Launch job results directory:  %s", self.logdir)
        if renamed_log_dir is not None:
            logger.info("  Renamed existing launch job results directory to %s", renamed_log_dir)
        logger.info("Launch log file:               %s", self.logfile)
        logger.info("-" * 80)

        # Results tracking settings
        self.job_results_dir = self.avocado.get_logs_dir()
        max_chars = self.avocado.get_setting("job.run.result.xunit", "max_test_log_chars")
        self.job = Job(
            self.name, xml_enabled="on", html_enabled="on", log_dir=self.logdir,
            max_chars=max_chars)
        self.result = Results(self.logdir)

        # Add details about the run
        self.details["avocado version"] = str(self.avocado)
        self.details["launch host"] = str(get_local_host())

    def _create_log_dir(self):
        """Create the log directory and rename it if it already exists.

        Returns:
            str: name of the old log directory if renamed, otherwise None

        """
        # When running manually save the previous log if one exists
        old_launch_log_dir = None
        if os.path.exists(self.logdir):
            old_launch_log_dir = "_".join([self.logdir, "old"])
            if os.path.exists(old_launch_log_dir):
                for file in os.listdir(old_launch_log_dir):
                    rm_file = os.path.join(old_launch_log_dir, file)
                    if os.path.isdir(rm_file):
                        for file2 in os.listdir(rm_file):
                            os.remove(os.path.join(rm_file, file2))
                        os.rmdir(rm_file)
                    else:
                        os.remove(rm_file)
                os.rmdir(old_launch_log_dir)
            os.rename(self.logdir, old_launch_log_dir)
        os.makedirs(self.logdir)

        return old_launch_log_dir

    def _set_test_environment(self, servers, clients, list_tests, provider, insecure_mode):
        """Set up the test environment.

        Args:
            servers (NodeSet): hosts designated for the server role in testing
            clients (NodeSet): hosts designated for the client role in testing
            list_tests (bool): whether or not the user has requested to just list the tests that
                match the specified tags
            provider (str): provider to use in testing
            insecure_mode (bool): whether or not to run tests in insecure mode

        Raises:
            LaunchException: if there is a problem setting up the test environment

        """
        base_dir = self._get_build_environment(list_tests)["PREFIX"]
        bin_dir = os.path.join(base_dir, "bin")
        sbin_dir = os.path.join(base_dir, "sbin")
        # /usr/sbin is not setup on non-root user for CI nodes.
        # SCM formatting tool mkfs.ext4 is located under
        # /usr/sbin directory.
        usr_sbin = os.path.sep + os.path.join("usr", "sbin")
        path = os.environ.get("PATH")
        os.environ["COVFILE"] = BULLSEYE_FILE

        if not list_tests:
            # Get the default fabric_iface value (DAOS_TEST_FABRIC_IFACE)
            self._set_interface_environment(servers, clients)

            # Get the default provider if CRT_PHY_ADDR_STR is not set
            self._set_provider_environment(servers, os.environ["DAOS_TEST_FABRIC_IFACE"], provider)

            # Set the default location for daos log files written during testing
            # if not already defined.
            if "DAOS_APP_DIR" not in os.environ:
                os.environ["DAOS_APP_DIR"] = DEFAULT_DAOS_APP_DIR
            if "DAOS_TEST_LOG_DIR" not in os.environ:
                os.environ["DAOS_TEST_LOG_DIR"] = DEFAULT_DAOS_TEST_LOG_DIR
            if "DAOS_TEST_SHARED_DIR" not in os.environ:
                if base_dir != os.path.join(os.sep, "usr"):
                    os.environ["DAOS_TEST_SHARED_DIR"] = os.path.join(base_dir, "tmp")
                else:
                    os.environ["DAOS_TEST_SHARED_DIR"] = DEFAULT_DAOS_TEST_SHARED_DIR
            os.environ["D_LOG_FILE"] = os.path.join(os.environ["DAOS_TEST_LOG_DIR"], "daos.log")
            os.environ["D_LOG_FILE_APPEND_PID"] = "1"

            # Assign the default value for transport configuration insecure mode
            os.environ["DAOS_INSECURE_MODE"] = str(insecure_mode)

        # Update PATH
        os.environ["PATH"] = ":".join([bin_dir, sbin_dir, usr_sbin, path])
        os.environ["COVFILE"] = os.path.join(os.sep, "tmp", "test.cov")

        # Python paths required for functional testing
        self._set_python_environment()

        logger.debug("ENVIRONMENT VARIABLES")
        for key in sorted(os.environ):
            if not key.startswith("BASH_FUNC_"):
                logger.debug("  %s: %s", key, os.environ[key])

    @staticmethod
    def _get_build_environment(list_tests):
        """Obtain DAOS build environment variables from the .build_vars.json file.

        Args:
            list_tests (bool): whether or not the user has requested to just list the tests that
                match the specified tags

        Raises:
            LaunchException: if there is an error obtaining the DAOS build environment variables

        Returns:
            dict: a dictionary of DAOS build environment variable names and values

        """
        build_vars_file = os.path.join(
            os.path.dirname(os.path.realpath(__file__)), "..", "..", ".build_vars.json")
        try:
            with open(build_vars_file, encoding="utf-8") as vars_file:
                return json.load(vars_file)

        except ValueError as error:
            if not list_tests:
                raise LaunchException("Error setting test environment") from error

        except IOError as error:
            if error.errno == errno.ENOENT:
                if not list_tests:
                    raise LaunchException("Error setting test environment") from error

        return json.loads(f'{{"PREFIX": "{os.getcwd()}"}}')

    def _set_interface_environment(self, servers, clients):
        """Set up the interface environment variables.

        Use the existing OFI_INTERFACE setting if already defined, or select the fastest, active
        interface on this host to define the DAOS_TEST_FABRIC_IFACE environment variable.

        The DAOS_TEST_FABRIC_IFACE defines the default fabric_iface value in the daos_server
        configuration file.

        Args:
            servers (NodeSet): hosts designated for the server role in testing
            clients (NodeSet): hosts designated for the client role in testing

        Raises:
            LaunchException: if there is a problem obtaining the default interface

        """
        logger.debug("-" * 80)
        # Get the default interface to use if OFI_INTERFACE is not set
        interface = os.environ.get("OFI_INTERFACE")
        if interface is None:
            # Find all the /sys/class/net interfaces on the launch node
            # (excluding lo)
            logger.debug("Detecting network devices - OFI_INTERFACE not set")
            available_interfaces = self._get_available_interfaces(servers, clients)
            try:
                # Select the fastest active interface available by sorting
                # the speed
                interface = available_interfaces[sorted(available_interfaces)[-1]]
            except IndexError as error:
                raise LaunchException("Error obtaining a default interface!") from error

        # Update env definitions
        os.environ["CRT_CTX_SHARE_ADDR"] = "0"
        os.environ["DAOS_TEST_FABRIC_IFACE"] = interface
        logger.info("Testing with interface:   %s", interface)
        self.details["interface"] = interface
        for name in ("OFI_INTERFACE", "DAOS_TEST_FABRIC_IFACE", "CRT_CTX_SHARE_ADDR"):
            try:
                logger.debug("Testing with %s=%s", name, os.environ[name])
            except KeyError:
                logger.debug("Testing with %s unset", name)

    @staticmethod
    def _get_available_interfaces(servers, clients):
        # pylint: disable=too-many-nested-blocks,too-many-branches,too-many-locals
        """Get a dictionary of active available interfaces and their speeds.

        Args:
            servers (NodeSet): hosts designated for the server role in testing
            clients (NodeSet): hosts designated for the client role in testing

        Raises:
            LaunchException: if there is a problem finding active network interfaces

        Returns:
            dict: a dictionary of speeds with the first available active interface providing that
                speed

        """
        available_interfaces = {}
        all_hosts = NodeSet()
        all_hosts.update(servers)
        all_hosts.update(clients)

        # Find any active network interfaces on the server or client hosts
        net_path = os.path.join(os.path.sep, "sys", "class", "net")
        operstate = os.path.join(net_path, "*", "operstate")
        command = [f"grep -l 'up' {operstate}", "grep -Ev '/(lo|bonding_masters)/'", "sort"]

        result = run_remote(logger, all_hosts, " | ".join(command))
        if not result.passed:
            raise LaunchException(
                f"Error obtaining a default interface on {str(all_hosts)} from {net_path}")

        # Populate a dictionary of active interfaces with a NodSet of hosts on which it was found
        active_interfaces = {}
        for data in result.output:
            for line in data.stdout:
                try:
                    interface = line.split("/")[-2]
                    if interface not in active_interfaces:
                        active_interfaces[interface] = data.hosts
                    else:
                        active_interfaces[interface].update(data.hosts)
                except IndexError:
                    pass

        # From the active interface dictionary find all the interfaces that are common to all hosts
        logger.debug("Active network interfaces detected:")
        common_interfaces = []
        for interface, node_set in active_interfaces.items():
            logger.debug("  - %-8s on %s (Common=%s)", interface, node_set, node_set == all_hosts)
            if node_set == all_hosts:
                common_interfaces.append(interface)

        # Find the speed of each common active interface in order to be able to choose the fastest
        interface_speeds = {}
        for interface in common_interfaces:
            # Check for a virtual interface
            module_path = os.path.join(net_path, interface, "device", "driver", "module")
            command = [f"readlink {module_path}", "grep 'virtio_net'"]
            result = run_remote(logger, all_hosts, " | ".join(command))
            if result.passed and result.homogeneous:
                interface_speeds[interface] = 1000
                continue

            # Verify each host has the same speed for non-virtual interfaces
            command = " ".join(["cat", os.path.join(net_path, interface, "speed")])
            result = run_remote(logger, all_hosts, command)
            if result.passed and result.homogeneous:
                for line in result.output[0].stdout:
                    try:
                        interface_speeds[interface] = int(line.strip())
                    except ValueError:
                        # Any line not containing a speed (integer)
                        pass
            elif not result.homogeneous:
                logger.error(
                    "Non-homogeneous interface speed detected for %s on %s.",
                    interface, str(all_hosts))
            else:
                logger.error("Error detecting speed of %s on %s", interface, str(all_hosts))

        if interface_speeds:
            logger.debug("Active network interface speeds on %s:", all_hosts)

        for interface, speed in interface_speeds.items():
            logger.debug("  - %-8s (speed: %6s)", interface, speed)
            # Only include the first active interface for each speed - first is
            # determined by an alphabetic sort: ib0 will be checked before ib1
            if speed is not None and speed not in available_interfaces:
                available_interfaces[speed] = interface

        logger.debug("Available interfaces on %s: %s", all_hosts, available_interfaces)
        return available_interfaces

    def _set_provider_environment(self, servers, interface, provider):
        """Set up the provider environment variables.

        Use the existing CRT_PHY_ADDR_STR setting if already defined, otherwise
        select the appropriate provider based upon the interface driver.

        Args:
            servers (NodeSet): hosts designated for the server role in testing
            interface (str): the current interface being used.
            provider (str): provider to use in testing

        Raises:
            LaunchException: if there is a problem finding a provider for the interface

        """
        logger.debug("-" * 80)
        # Use the detected provider if one is not set
        if not provider:
            provider = os.environ.get("CRT_PHY_ADDR_STR")
        if provider is None:
            logger.debug("Detecting provider for %s - CRT_PHY_ADDR_STR not set", interface)

            # Check for a Omni-Path interface
            logger.debug("Checking for Omni-Path devices")
            command = "sudo -n opainfo"
            result = run_remote(logger, servers, command)
            if result.passed:
                # Omni-Path adapter found; remove verbs as it will not work with OPA devices.
                logger.debug("  Excluding verbs provider for Omni-Path adapters")
                PROVIDER_KEYS.pop("verbs")

            # Detect all supported providers
            command = f"fi_info -d {interface} -l | grep -v 'version:'"
            result = run_remote(logger, servers, command)
            if result.passed:
                # Find all supported providers
                keys_found = defaultdict(NodeSet)
                for data in result.output:
                    for line in data.stdout:
                        provider_name = line.replace(":", "")
                        if provider_name in PROVIDER_KEYS:
                            keys_found[provider_name].update(data.hosts)

                # Only use providers available on all the server hosts
                if keys_found:
                    logger.debug("Detected supported providers:")
                provider_name_keys = list(keys_found)
                for provider_name in provider_name_keys:
                    logger.debug("  %4s: %s", provider_name, str(keys_found[provider_name]))
                    if keys_found[provider_name] != servers:
                        keys_found.pop(provider_name)

                # Select the preferred found provider based upon PROVIDER_KEYS order
                logger.debug("Supported providers detected: %s", list(keys_found))
                for key in PROVIDER_KEYS:
                    if key in keys_found:
                        provider = PROVIDER_KEYS[key]
                        break

            # Report an error if a provider cannot be found
            if not provider:
                raise LaunchException(
                    f"Error obtaining a supported provider for {interface} "
                    f"from: {list(PROVIDER_KEYS)}")

            logger.debug("  Found %s provider for %s", provider, interface)

        # Update env definitions
        os.environ["CRT_PHY_ADDR_STR"] = provider
        logger.info("Testing with provider:    %s", provider)
        self.details["provider"] = provider
        logger.debug("Testing with CRT_PHY_ADDR_STR=%s", os.environ["CRT_PHY_ADDR_STR"])

    @staticmethod
    def _set_python_environment():
        """Set up the test python environment.

        Args:
            log (logger): logger for the messages produced by this method
        """
        logger.debug("-" * 80)
        required_python_paths = [
            os.path.abspath("util/apricot"),
            os.path.abspath("util"),
            os.path.abspath("cart"),
        ]

        # Include the cart directory paths when running from sources
        for cart_dir in os.listdir(os.path.abspath("cart")):
            cart_path = os.path.join(os.path.abspath("cart"), cart_dir)
            if os.path.isdir(cart_path):
                required_python_paths.append(cart_path)

        required_python_paths.extend(site.getsitepackages())

        # Check the PYTHONPATH env definition
        python_path = os.environ.get("PYTHONPATH")
        if python_path is None or python_path == "":
            # Use the required paths to define the PYTHONPATH env if it is not set
            os.environ["PYTHONPATH"] = ":".join(required_python_paths)
        else:
            # Append any missing required paths to the existing PYTHONPATH env
            defined_python_paths = [
                os.path.abspath(os.path.expanduser(path))
                for path in python_path.split(":")]
            for required_path in required_python_paths:
                if required_path not in defined_python_paths:
                    python_path += ":" + required_path
            os.environ["PYTHONPATH"] = python_path
        logger.debug("Testing with PYTHONPATH=%s", os.environ["PYTHONPATH"])

    def _get_device_replacement(self, servers, nvme):
        """Determine the value to use for the '--nvme' command line argument.

        Determine if the specified hosts have homogeneous NVMe drives (either standalone or VMD
        controlled) and use these values to replace placeholder devices in avocado test yaml files.

        Supported auto '--nvme' arguments:
            auto[:filter]       = select any PCI domain number of a NVMe device or VMD controller
                                (connected to a VMD enabled NVMe device) in the homogeneous
                                'lspci -D' output from each server.  Optionally grep the list of
                                NVMe or VMD enabled NVMe devices for 'filter'.
            auto_nvme[:filter]  = select any PCI domain number of a non-VMD controlled NVMe device
                                in the homogeneous 'lspci -D' output from each server.  Optionally
                                grep this output for 'filter'.
            auto_vmd[:filter]   = select any PCI domain number of a VMD controller connected to a
                                VMD enabled NVMe device in the homogeneous 'lspci -D' output from
                                each server.  Optionally grep the list of VMD enabled NVMe devices
                                for 'filter'.

        Args:
            servers (NodeSet): hosts designated for the server role in testing
            nvme (str): the --nvme argument value

        Raises:
            LaunchException: if there is a problem finding a device replacement

        Returns:
            str: a comma-separated list of nvme device pci addresses available on all of the
                specified test servers

        """
        logger.debug("-" * 80)
        logger.debug("Detecting devices that match: %s", nvme)
        devices = []
        device_types = []

        # Separate any optional filter from the key
        dev_filter = None
        nvme_args = nvme.split(":")
        if len(nvme_args) > 1:
            dev_filter = nvme_args[1]

        # First check for any VMD disks, if requested
        if nvme_args[0] in ["auto", "auto_vmd"]:
            vmd_devices = self._auto_detect_devices(servers, "NVMe", "5", dev_filter)
            if vmd_devices:
                # Find the VMD controller for the matching VMD disks
                vmd_controllers = self._auto_detect_devices(servers, "VMD", "4", None)
                devices.extend(
                    self._get_vmd_address_backed_nvme(servers, vmd_devices, vmd_controllers))
            elif not dev_filter:
                # Use any VMD controller if no VMD disks found w/o a filter
                devices = self._auto_detect_devices(servers, "VMD", "4", None)
            if devices:
                device_types.append("VMD")

        # Second check for any non-VMD NVMe disks, if requested
        if nvme_args[0] in ["auto", "auto_nvme"]:
            dev_list = self._auto_detect_devices(servers, "NVMe", "4", dev_filter)
            if dev_list:
                devices.extend(dev_list)
                device_types.append("NVMe")

        # If no VMD or NVMe devices were found exit
        if not devices:
            raise LaunchException(
                f"Error: Unable to auto-detect devices for the '--nvme {nvme}' argument")

        logger.debug(
            "Auto-detected %s devices on %s: %s", " & ".join(device_types), servers, devices)
        logger.info("Testing with %s devices: %s", " & ".join(device_types), devices)
        self.details[f"{' & '.join(device_types)} devices"] = devices
        return ",".join(devices)

    @staticmethod
    def _auto_detect_devices(hosts, device_type, length, device_filter=None):
        """Get a list of NVMe/VMD devices found on each specified host.

        Args:
            log (logger): logger for the messages produced by this method
            hosts (NodeSet): hosts on which to find the NVMe/VMD devices
            device_type (str): device type to find, e.g. 'NVMe' or 'VMD'
            length (str): number of digits to match in the first PCI domain number
            device_filter (str, optional): optional filter to apply to device searching. Defaults to
                None.

        Raises:
            LaunchException: if there is a problem finding a devices

        Returns:
            list: A list of detected devices - empty if none found

        """
        found_devices = {}

        # Find the devices on each host
        if device_type == "VMD":
            # Exclude the controller revision as this causes heterogeneous clush output
            command_list = [
                "/sbin/lspci -D",
                f"grep -E '^[0-9a-f]{{{length}}}:[0-9a-f]{{2}}:[0-9a-f]{{2}}.[0-9a-f] '",
                "grep -E 'Volume Management Device NVMe RAID Controller'",
                r"sed -E 's/\(rev\s+([a-f0-9])+\)//I'"]
        elif device_type == "NVMe":
            command_list = [
                "/sbin/lspci -D",
                f"grep -E '^[0-9a-f]{{{length}}}:[0-9a-f]{{2}}:[0-9a-f]{{2}}.[0-9a-f] '",
                "grep -E 'Non-Volatile memory controller:'"]
            if device_filter and device_filter.startswith("-"):
                command_list.append(f"grep -v '{device_filter[1:]}'")
            elif device_filter:
                command_list.append(f"grep '{device_filter}'")
        else:
            raise LaunchException(
                f"ERROR: Invalid 'device_type' for NVMe/VMD auto-detection: {device_type}")
        command = " | ".join(command_list) + " || :"

        # Find all the VMD PCI addresses common to all hosts
        result = run_remote(logger, hosts, command)
        if result.passed:
            for data in result.output:
                for line in data.stdout:
                    if line not in found_devices:
                        found_devices[line] = NodeSet()
                    found_devices[line].update(data.hosts)

            # Remove any non-homogeneous devices
            for key in list(found_devices):
                if found_devices[key] != hosts:
                    logger.debug(
                        "  device '%s' not found on all hosts: %s", key, found_devices[key])
                    found_devices.pop(key)

        if not found_devices:
            raise LaunchException("Error: Non-homogeneous {device_type} PCI addresses.")

        # Get the devices from the successful, homogeneous command output
        return find_pci_address("\n".join(found_devices))

    @staticmethod
    def _get_vmd_address_backed_nvme(hosts, vmd_disks, vmd_controllers):
        """Find valid VMD address which has backing NVMe.

        Args:
            log (logger): logger for the messages produced by this method
            hosts (NodeSet): hosts on which to find the VMD addresses
            vmd_disks (list): list of PCI domain numbers for each VMD controlled disk
            vmd_controllers (list): list of PCI domain numbers for each VMD controller

        Raises:
            LaunchException: if there is a problem finding a devices

        Returns:
            list: a list of the VMD controller PCI domain numbers which are connected to the VMD
                disks

        """
        disk_controllers = {}
        command_list = ["ls -l /sys/block/", "grep nvme"]
        if vmd_disks:
            command_list.append(f"grep -E '({'|'.join(vmd_disks)})'")
        command_list.extend(["cut -d'>' -f2", "cut -d'/' -f4"])
        command = " | ".join(command_list) + " || :"
        result = run_remote(logger, hosts, command)

        # Verify the command was successful on each server host
        if not result.passed:
            raise LaunchException(f"Error issuing command '{command}'")

        # Collect a list of NVMe devices behind the same VMD addresses on each host.
        logger.debug("Checking for %s in command output", vmd_controllers)
        if result.passed:
            for data in result.output:
                for device in vmd_controllers:
                    if device in data.stdout:
                        if device not in disk_controllers:
                            disk_controllers[device] = NodeSet()
                        disk_controllers[device].update(data.hosts)

            # Remove any non-homogeneous devices
            for key in list(disk_controllers):
                if disk_controllers[key] != hosts:
                    logger.debug(
                        "  device '%s' not found on all hosts: %s", key, disk_controllers[key])
                    disk_controllers.pop(key)

        # Verify each server host has the same NVMe devices behind the same VMD addresses.
        if not disk_controllers:
            raise LaunchException("Error: Non-homogeneous NVMe device behind VMD addresses.")

        return disk_controllers

    def list_tests(self, tags):
        """List the test files matching the tags.

        Populates the self.tests list and defines the self.tag_filters list to use when running
        tests.

        Args:
            tags (list): a list of tags or test file names

        Raises:
            RunException: if there is a problem listing tests

        """
        logger.debug("-" * 80)
        self.tests = []
        self.tag_filters = []

        # Determine if fault injection is enabled
        fault_tag = "-faults"
        fault_filter = f"--filter-by-tags={fault_tag}"
        faults_enabled = self._faults_enabled()

        # Determine if each tag list entry is a tag or file specification
        test_files = []
        for tag in tags:
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
        command = self.avocado.get_list_command()
        command.extend(self.tag_filters)
        command.extend(test_files)
        if not test_files:
            command.append("./")

        # Find all the test files that contain tests matching the tags
        logger.info("Detecting tests matching tags: %s", " ".join(command))
        output = run_local(logger, " ".join(command), check=True)
        unique_test_files = set(re.findall(self.avocado.get_list_regex(), output.stdout))
        for index, test_file in enumerate(unique_test_files):
            self.tests.append(TestInfo(test_file, index + 1))
            logger.info("  %s", self.tests[-1])

    @staticmethod
    def _faults_enabled():
        """Determine if fault injection is enabled.

        Returns:
            bool: whether or not fault injection is enabled

        """
        logger.debug("Checking for fault injection enablement via 'fault_status':")
        try:
            run_local(logger, "fault_status", check=True)
            logger.debug("  Fault injection is enabled")
            return True
        except RunException:
            # Command failed or yielded a non-zero return status
            logger.debug("  Fault injection is disabled")
        return False

    def setup_test_files(self, args, yaml_dir):
        """Set up the test yaml files with any placeholder replacements.

        Args:
            args (argparse.Namespace): command line arguments for this program
            yaml_dir (str): directory in which to write the modified yaml files

        Raises:
            RunException: if there is a problem updating the test yaml files
            LaunchException: if there is an error getting host information from the test yaml files

        """
        # Replace any placeholders in the extra yaml file, if provided
        if args.extra_yaml:
            args.extra_yaml = [
                self._replace_yaml_file(extra, args, yaml_dir) for extra in args.extra_yaml]

        for test in self.tests:
            test.yaml_file = self._replace_yaml_file(test.yaml_file, args, yaml_dir)

            # Display the modified yaml file variants with debug
            command = ["avocado", "variants", "--mux-yaml", test.yaml_file]
            if args.extra_yaml:
                command.extend(args.extra_yaml)
            command.extend(["--summary", "3"])
            run_local(logger, " ".join(command), check=False)

            # Collect the host information from the updated test yaml
            test.set_yaml_info(args.include_localhost)

    @staticmethod
    def _replace_yaml_file(yaml_file, args, yaml_dir):
        # pylint: disable=too-many-nested-blocks,too-many-branches
        """Create a temporary test yaml file with any requested values replaced.

        Optionally replace the following test yaml file values if specified by the
        user via the command line arguments:

            test_servers:   Use the list specified by the --test_servers (-ts)
                            argument to replace any host name placeholders listed
                            under "test_servers:"

            test_clients    Use the list specified by the --test_clients (-tc)
                            argument (or any remaining names in the --test_servers
                            list argument, if --test_clients is not specified) to
                            replace any host name placeholders listed under
                            "test_clients:".

            bdev_list       Use the list specified by the --nvme (-n) argument to
                            replace the string specified by the "bdev_list:" yaml
                            parameter.  If multiple "bdev_list:" entries exist in
                            the yaml file, evenly divide the list when making the
                            replacements.

        Any replacements are made in a copy of the original test yaml file.  If no
        replacements are specified return the original test yaml file.

        Args:
            yaml_file (str): test yaml file
            args (argparse.Namespace): command line arguments for this program
            yaml_dir (str): directory in which to write the modified yaml files

        Raises:
            RunException: if a yaml file placeholder is missing a value

        Returns:
            str: the test yaml file; None if the yaml file contains placeholders
                w/o replacements

        """
        logger.debug("-" * 80)
        replacements = {}

        if args.test_servers or args.nvme or args.timeout_multiplier:
            # Find the test yaml keys and values that match the replaceable fields
            yaml_data = get_yaml_data(yaml_file)
            logger.debug("Detected yaml data: %s", yaml_data)
            yaml_keys = list(YAML_KEYS.keys())
            yaml_find = find_values(yaml_data, yaml_keys, val_type=(list, int, dict, str))

            # Generate a list of values that can be used as replacements
            user_values = OrderedDict()
            for key, value in list(YAML_KEYS.items()):
                args_value = getattr(args, value)
                if isinstance(args_value, NodeSet):
                    user_values[key] = list(args_value)
                elif isinstance(args_value, str):
                    user_values[key] = args_value.split(",")
                elif args_value:
                    user_values[key] = [args_value]
                else:
                    user_values[key] = None

            # Assign replacement values for the test yaml entries to be replaced
            logger.debug("Detecting replacements for %s in %s", yaml_keys, yaml_file)
            logger.debug("  Found values: %s", yaml_find)
            logger.debug("  User values:  %s", dict(user_values))

            node_mapping = {}
            for key, user_value in user_values.items():
                # If the user did not provide a specific list of replacement
                # test_clients values, use the remaining test_servers values to
                # replace test_clients placeholder values
                if key == "test_clients" and not user_value:
                    user_value = user_values["test_servers"]

                # Replace test yaml keys that were:
                #   - found in the test yaml
                #   - have a user-specified replacement
                if key in yaml_find and user_value:
                    if key.startswith("test_"):
                        # The entire server/client test yaml list entry is replaced
                        # by a new test yaml list entry, e.g.
                        #   '  test_servers: server-[1-2]' --> '  test_servers: wolf-[10-11]'
                        #   '  test_servers: 4'            --> '  test_servers: wolf-[10-13]'
                        if not isinstance(yaml_find[key], list):
                            yaml_find[key] = [yaml_find[key]]

                        for yaml_find_item in yaml_find[key]:
                            replacement = NodeSet()
                            try:
                                # Replace integer placeholders with the number of nodes from the
                                # user provided list equal to the quantity requested by the test
                                # yaml
                                quantity = int(yaml_find_item)
                                if args.override and args.test_clients:
                                    # When individual lists of server and client nodes are provided
                                    # with the override flag set use the full list of nodes
                                    # specified by the test_server/test_client arguments
                                    quantity = len(user_value)
                                elif args.override:
                                    logger.warning(
                                        "Warning: In order to override the node quantity a "
                                        "'--test_clients' argument must be specified: %s: %s",
                                        key, yaml_find_item)
                                for _ in range(quantity):
                                    try:
                                        replacement.add(user_value.pop(0))
                                    except IndexError:
                                        # Not enough nodes provided for the replacement
                                        if not args.override:
                                            replacement = None
                                        break

                            except ValueError:
                                try:
                                    # Replace clush-style placeholders with nodes from the user
                                    # provided list using a mapping so that values used more than
                                    # once yield the same replacement
                                    for node in NodeSet(yaml_find_item):
                                        if node not in node_mapping:
                                            try:
                                                node_mapping[node] = user_value.pop(0)
                                            except IndexError:
                                                # Not enough nodes provided for the replacement
                                                if not args.override:
                                                    replacement = None
                                                break
                                            logger.debug(
                                                "  - %s replacement node mapping: %s -> %s",
                                                key, node, node_mapping[node])
                                        replacement.add(node_mapping[node])

                                except TypeError:
                                    # Unsupported format
                                    replacement = None

                            hosts_key = r":\s+".join([key, str(yaml_find_item)])
                            hosts_key = hosts_key.replace("[", r"\[")
                            hosts_key = hosts_key.replace("]", r"\]")
                            if replacement:
                                replacements[hosts_key] = ": ".join([key, str(replacement)])
                            else:
                                replacements[hosts_key] = None

                    elif key == "bdev_list":
                        # Individual bdev_list NVMe PCI addresses in the test yaml
                        # file are replaced with the new NVMe PCI addresses in the
                        # order they are found, e.g.
                        #   0000:81:00.0 --> 0000:12:00.0
                        for yaml_find_item in yaml_find[key]:
                            bdev_key = f"\"{yaml_find_item}\""
                            if bdev_key in replacements:
                                continue
                            try:
                                replacements[bdev_key] = f"\"{user_value.pop(0)}\""
                            except IndexError:
                                replacements[bdev_key] = None

                    else:
                        # Timeouts - replace the entire timeout entry (key + value)
                        # with the same key with its original value multiplied by the
                        # user-specified value, e.g.
                        #   timeout: 60 -> timeout: 600
                        if isinstance(yaml_find[key], int):
                            timeout_key = r":\s+".join([key, str(yaml_find[key])])
                            timeout_new = max(1, round(yaml_find[key] * user_value[0]))
                            replacements[timeout_key] = ": ".join([key, str(timeout_new)])
                            logger.debug(
                                "  - Timeout adjustment (x %s): %s -> %s",
                                user_value, timeout_key, replacements[timeout_key])
                        elif isinstance(yaml_find[key], dict):
                            for timeout_test, timeout_val in list(yaml_find[key].items()):
                                timeout_key = r":\s+".join([timeout_test, str(timeout_val)])
                                timeout_new = max(1, round(timeout_val * user_value[0]))
                                replacements[timeout_key] = ": ".join(
                                    [timeout_test, str(timeout_new)])
                                logger.debug(
                                    "  - Timeout adjustment (x %s): %s -> %s",
                                    user_value, timeout_key, replacements[timeout_key])

            # Display the replacement values
            for value, replacement in list(replacements.items()):
                logger.debug("  - Replacement: %s -> %s", value, replacement)

        if replacements:
            # Read in the contents of the yaml file to retain the !mux entries
            logger.debug("Reading %s", yaml_file)
            with open(yaml_file, encoding="utf-8") as yaml_buffer:
                yaml_data = yaml_buffer.read()

            # Apply the placeholder replacements
            missing_replacements = []
            logger.debug("Modifying contents: %s", yaml_file)
            for key in sorted(replacements):
                value = replacements[key]
                if value:
                    # Replace the host entries with their mapped values
                    logger.debug("  - Replacing: %s --> %s", key, value)
                    yaml_data = re.sub(key, value, yaml_data)
                else:
                    # Keep track of any placeholders without a replacement value
                    logger.debug("  - Missing:   %s", key)
                    missing_replacements.append(key)
            if missing_replacements:
                # Report an error for all of the placeholders w/o a replacement
                logger.error(
                    "Error: Placeholders missing replacements in %s:\n  %s",
                    yaml_file, ", ".join(missing_replacements))
                raise LaunchException(f"Error: Placeholders missing replacements in {yaml_file}")

            # Write the modified yaml file into a temporary file.  Use the path to
            # ensure unique yaml files for tests with the same filename.
            orig_yaml_file = yaml_file
            yaml_name = get_test_category(yaml_file)
            yaml_file = os.path.join(yaml_dir, f"{yaml_name}.yaml")
            logger.debug("Creating copy: %s", yaml_file)
            with open(yaml_file, "w", encoding="utf-8") as yaml_buffer:
                yaml_buffer.write(yaml_data)

            # Optionally display a diff of the yaml file
            if args.verbose > 0:
                command = ["diff", "-y", orig_yaml_file, yaml_file]
                run_local(logger, " ".join(command), check=False)

        # Return the untouched or modified yaml file
        return yaml_file

    def _get_core_file_pattern(self, servers, clients, process_cores):
        """Get the core file pattern information from the hosts if collecting core files.

        Args:
            servers (NodeSet): hosts designated for the server role in testing
            clients (NodeSet): hosts designated for the client role in testing
            process_cores (bool): whether or not to collect core files after the tests complete

        Raises:
            LaunchException: if there was an error obtaining the core file pattern information

        Returns:
            dict: a dictionary containing the path and pattern for the core files per NodeSet

        """
        core_files = {}
        if not process_cores:
            logger.debug("Not collecting core files")
            return core_files

        # Determine the core file pattern being used by the hosts
        all_hosts = servers | clients
        all_hosts |= self.local_host
        command = "cat /proc/sys/kernel/core_pattern"
        result = run_remote(logger, all_hosts, command)

        # Verify all the hosts have the same core file pattern
        if not result.passed:
            raise LaunchException("Error obtaining the core file pattern")

        # Get the path and pattern information from the core pattern
        for data in result.output:
            hosts = str(data.hosts)
            try:
                info = os.path.split(result.output[0].stdout[-1])
            except (TypeError, IndexError) as error:
                raise LaunchException(
                    "Error obtaining the core file pattern and directory") from error
            if not info[0]:
                raise LaunchException("Error obtaining the core file pattern directory")
            core_files[hosts] = {"path": info[0], "pattern": re.sub(r"%[A-Za-z]", "*", info[1])}
            logger.info(
                "Collecting any '%s' core files written to %s on %s",
                core_files[hosts]["pattern"], core_files[hosts]["path"], hosts)

        return core_files

    def run_tests(self, sparse, fail_fast, extra_yaml, stop_daos, archive, rename, jenkinslog,
                  core_files, threshold):
        """Run all the tests.

        Args:
            sparse (bool): whether or not to display the shortened avocado test output
            fail_fast (bool): whether or not to fail the avocado run command upon the first failure
            extra_yaml (list): optional test yaml file to use when running the test
            stop_daos (bool): whether or not to stop daos servers/clients after the test
            archive (bool): whether or not to collect remote files generated by the test
            rename (bool): whether or not to rename the default avocado job-results directory names
            jenkinslog (bool): whether or not to update the results.xml to use Jenkins-style names
            core_files (dict): location and pattern defining where core files may be written
            threshold (str): optional upper size limit for test log files

        Returns:
            int: status code to use when exiting launch.py

        """
        return_code = 0

        # Display the location of the avocado logs
        logger.info("Avocado job results directory: %s", self.job_results_dir)

        # Configure hosts to collect code coverage
        self.setup_bullseye()

        # Run each test for as many repetitions as requested
        for repeat in range(1, self.repeat + 1):
            logger.info("-" * 80)
            logger.info("Starting test repetition %s/%s", repeat, self.repeat)

            for test in self.tests:
                # Define a log for the execution of this test for this repetition
                test_log_file = test.get_log_file(self.logdir, repeat, self.repeat)
                logger.info("-" * 80)
                logger.info("Log file for repetition %s of %s: %s", repeat, test, test_log_file)
                test_file_handler = get_file_handler(test_log_file, LOG_FILE_FORMAT, logging.DEBUG)
                logger.addHandler(test_file_handler)

                # Create a new TestResult for this test
                test_result = self._start_test(test.class_name, test.name.copy(), test_log_file)

                # Prepare the hosts to run the tests
                step_status = self.prepare(test, repeat)
                if step_status:
                    # Do not run this test - update its failure status to interrupted
                    return_code |= step_status
                    continue

                # Avoid counting the test execution time as part of the processing time of this test
                test_result.end()

                # Run the test with avocado
                return_code |= self.execute(test, repeat, sparse, fail_fast, extra_yaml)

                # Mark the continuation of the processing of this test
                test_result.start()

                # Archive the test results
                return_code |= self.process(
                    test, repeat, stop_daos, archive, rename, jenkinslog, core_files, threshold)

                # Mark the execution of the test as passed if nothing went wrong
                if test_result.status is None:
                    self._pass_test(test_result)

                # Mark the end of the processing of this test
                test_result.end()

                # Display disk usage after the test is complete
                self.display_disk_space(self.logdir)

                # Stop logging to the test log file
                logger.removeHandler(test_file_handler)

        # Collect code coverage files after all test have completed
        self.finalize_bullseye()

        # Summarize the run
        return self._summarize_run(return_code)

    def setup_bullseye(self):
        """Set up the hosts for bullseye code coverage collection.

        Returns:
            int: status code: 0 = success, 128 = failure

        """
        if self.bullseye_hosts:
            logger.debug("-" * 80)
            logger.info("Setting up bullseye code coverage on %s:", self.bullseye_hosts)

            logger.debug("Removing any existing %s file", BULLSEYE_FILE)
            command = ["rm", "-fr", BULLSEYE_FILE]
            if not run_remote(logger, self.bullseye_hosts, " ".join(command)).passed:
                message = "Error removing bullseye code coverage file on at least one host"
                self._fail_test(self.result.tests[0], "Run", message, None)
                return 128

            logger.debug("Copying %s bullseye code coverage source file", BULLSEYE_SRC)
            command = ["cp", BULLSEYE_SRC, BULLSEYE_FILE]
            if not run_remote(logger, self.bullseye_hosts, " ".join(command)).passed:
                message = "Error copying bullseye code coverage file on at least one host"
                self._fail_test(self.result.tests[0], "Run", message, None)
                return 128

            logger.debug("Updating %s bullseye code coverage file permissions", BULLSEYE_FILE)
            command = ["chmod", "777", BULLSEYE_FILE]
            if not run_remote(logger, self.bullseye_hosts, " ".join(command)).passed:
                message = "Error updating bullseye code coverage file on at least one host"
                self._fail_test(self.result.tests[0], "Run", message, None)
                return 128
        return 0

    def finalize_bullseye(self):
        """Retrieve the bullseye code coverage collection information from the hosts.

        Returns:
            int: status code: 0 = success, 16 = failure

        """
        if not self.bullseye_hosts:
            return 0

        bullseye_path, bullseye_file = os.path.split(BULLSEYE_FILE)
        bullseye_dir = os.path.join(self.job_results_dir, "bullseye_coverage_logs")
        status = self._archive_files(
            "bullseye coverage log files", self.bullseye_hosts, bullseye_path,
            "".join([bullseye_file, "*"]), bullseye_dir, 1, None, 900)
        # Rename bullseye_coverage_logs.host/test.cov.* to
        # bullseye_coverage_logs/test.host.cov.*
        for item in os.listdir(self.job_results_dir):
            item_full = os.path.join(self.job_results_dir, item)
            if os.path.isdir(item_full) and "bullseye_coverage_logs" in item:
                host_ext = os.path.splitext(item)
                if len(host_ext) > 1:
                    os.makedirs(bullseye_dir, exist_ok=True)
                    for name in os.listdir(item_full):
                        old_file = os.path.join(item_full, name)
                        if os.path.isfile(old_file):
                            new_name = name.split(".")
                            new_name.insert(1, host_ext[-1][1:])
                            new_file_name = ".".join(new_name)
                            new_file = os.path.join(bullseye_dir, new_file_name)
                            logger.debug("Renaming %s to %s", old_file, new_file)
                            os.rename(old_file, new_file)
        return status

    @staticmethod
    def display_disk_space(path):
        """Display disk space of provided path destination.

        Args:
            log (logger): logger for the messages produced by this method
            path (str): path to directory to print disk space for.
        """
        logger.debug("-" * 80)
        logger.debug("Current disk space usage of %s", path)
        try:
            run_local(logger, " ".join(["df", "-h", path]), check=False)
        except RunException:
            pass

    def prepare(self, test, repeat):
        """Prepare the test for execution.

        Args:
            test (TestInfo): the test information
            repeat (int): the test repetition number

        Returns:
            int: status code: 0 = success, 128 = failure

        """
        logger.debug("=" * 80)
        logger.info("Preparing to run the %s test on repeat %s/%s", test, repeat, self.repeat)

        # Setup the test host information, including creating any required slurm partitions
        status = self._setup_host_information(test)
        if status:
            return status

        # Setup (remove/create/list) the common DAOS_TEST_LOG_DIR directory on each test host
        status = self._setup_test_directory(test)
        if status:
            return status

        # Generate certificate files for the test
        return self._generate_certs()

    def _setup_host_information(self, test):
        """Set up the test host information and any required partitions.

        Args:
            test (TestInfo): the test information

        Returns:
            int: status code: 0 = success, 128 = failure

        """
        logger.debug("-" * 80)
        logger.debug("Setting up host information for %s", test)

        # Verify any required partitions exist
        if test.yaml_info["client_partition"]:
            partition = test.yaml_info["client_partition"]
            logger.debug("Determining if the %s client partition exists", partition)
            exists = show_partition(logger, self.slurm_control_node, partition).passed
            if not exists and not self.slurm_add_partition:
                message = f"Error missing {partition} partition"
                self._fail_test(self.result.tests[-1], "Prepare", message, None)
                return 128
            if self.slurm_add_partition and exists:
                logger.info(
                    "Removing existing %s partition to ensure correct configuration", partition)
                if not delete_partition(logger, self.slurm_control_node, partition).passed:
                    message = f"Error removing existing {partition} partition"
                    self._fail_test(self.result.tests[-1], "Prepare", message, None)
                    return 128
            if self.slurm_add_partition:
                hosts = self.slurm_partition_hosts.difference(test.yaml_info["test_servers"])
                logger.debug(
                    "Partition hosts from '%s', excluding test servers '%s': %s",
                    self.slurm_partition_hosts, test.yaml_info["test_servers"], hosts)
                if not hosts:
                    message = "Error no partition hosts exist after removing the test servers"
                    self._fail_test(self.result.tests[-1], "Prepare", message, None)
                    return 128
                logger.info("Creating the '%s' partition with the '%s' hosts", partition, hosts)
                if not create_partition(logger, self.slurm_control_node, partition, hosts).passed:
                    message = f"Error adding the {partition} partition"
                    self._fail_test(self.result.tests[-1], "Prepare", message, None)
                    return 128

        # Define the hosts for this test
        try:
            test.set_host_info(self.slurm_control_node)
        except LaunchException:
            message = "Error setting up host information"
            self._fail_test(self.result.tests[-1], "Prepare", message, sys.exc_info())
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

    def _setup_test_directory(self, test):
        """Set up the common test directory on all hosts.

        Args:
            test (TestInfo): the test information

        Returns:
            int: status code: 0 = success, 128 = failure

        """
        logger.debug("-" * 80)
        test_dir = os.environ["DAOS_TEST_LOG_DIR"]
        logger.debug("Setting up '%s' on %s:", test_dir, test.host_info.all_hosts)
        commands = [
            f"sudo -n rm -fr {test_dir}",
            f"mkdir -p {test_dir}",
            f"chmod a+wr {test_dir}",
            f"ls -al {test_dir}",
        ]
        for command in commands:
            if not run_remote(logger, test.host_info.all_hosts, command).passed:
                message = "Error setting up the DAOS_TEST_LOG_DIR directory on all hosts"
                self._fail_test(self.result.tests[-1], "Prepare", message, sys.exc_info())
                return 128
        return 0

    def _generate_certs(self):
        """Generate the certificates for the test.

        Returns:
            int: status code: 0 = success, 128 = failure

        """
        logger.debug("-" * 80)
        logger.debug("Generating certificates")
        daos_test_log_dir = os.environ["DAOS_TEST_LOG_DIR"]
        certs_dir = os.path.join(daos_test_log_dir, "daosCA")
        certgen_dir = os.path.abspath(
            os.path.join("..", "..", "..", "..", "lib64", "daos", "certgen"))
        command = os.path.join(certgen_dir, "gen_certificates.sh")
        try:
            run_local(logger, " ".join(["/usr/bin/rm", "-rf", certs_dir]))
            run_local(logger, " ".join([command, daos_test_log_dir]))
        except RunException:
            message = "Error generating certificates"
            self._fail_test(self.result.tests[-1], "Prepare", message, sys.exc_info())
            return 128
        return 0

    def execute(self, test, repeat, sparse, fail_fast, extra_yaml):
        """Run the specified test.

        Args:
            test (TestInfo): the test information
            repeat (int): the test repetition number
            sparse (bool): whether to use avocado sparse output
            fail_fast(bool): whether to use the avocado fail fast option
            extra_yaml (list): whether to use an exta yaml file with the avocado run command

        Returns:
            int: status code: 0 = success, >0 = failure

        """
        logger.debug("=" * 80)
        command = self.avocado.get_run_command(
            test, self.tag_filters, sparse, fail_fast, extra_yaml)
        logger.info(
            "Running the %s test on repeat %s/%s: %s", test, repeat, self.repeat, " ".join(command))
        start_time = int(time.time())

        try:
            return_code = run_local(
                logger, " ".join(command), capture_output=False, check=False).returncode
            if return_code == 0:
                logger.debug("All avocado test variants passed")
            elif return_code == 2:
                logger.debug("At least one avocado test variant failed")
            elif return_code == 4:
                message = "Failed avocado commands detected"
                self._fail_test(self.result.tests[-1], "Process", message)
            elif return_code == 8:
                logger.debug("At least one avocado test variant was interrupted")
            if return_code:
                self._collect_crash_files()

        except RunException:
            message = f"Error executing {test} on repeat {repeat}"
            self._fail_test(self.result.tests[-1], "Execute", message, sys.exc_info())
            return_code = 1

        end_time = int(time.time())
        logger.info("Total test time: %ss", end_time - start_time)
        return return_code

    def _collect_crash_files(self):
        """Move any avocado crash files into job-results/latest/crashes.

        Args:
            log (logger): logger for the messages produced by this method
            avocado_logs_dir (str): path to the avocado log files.
        """
        avocado_logs_dir = self.avocado.get_logs_dir()
        crash_dir = os.path.join(avocado_logs_dir.replace("job-results", "data"), "crashes")
        if os.path.isdir(crash_dir):
            crash_files = [
                os.path.join(crash_dir, crash_file)
                for crash_file in os.listdir(crash_dir)
                if os.path.isfile(os.path.join(crash_dir, crash_file))]

            if crash_files:
                latest_crash_dir = os.path.join(avocado_logs_dir, "latest", "crashes")
                try:
                    run_local(logger, " ".join(["mkdir", "-p", latest_crash_dir]), check=True)
                    for crash_file in crash_files:
                        run_local(
                            logger, " ".join(["mv", crash_file, latest_crash_dir]), check=True)
                except RunException:
                    message = "Error collecting crash files"
                    self._fail_test(self.result.tests[-1], "Execute", message, sys.exc_info())
            else:
                logger.debug("No avocado crash files found in %s", crash_dir)

    def process(self, test, repeat, stop_daos, archive, rename, jenkinslog, core_files, threshold):
        """Process the test results.

        This may include (depending upon argument values):
            - Stopping any running servers or agents
            - Resetting the server storage
            - Archiving any files generated by the test and including them with the test results
            - Renaming the test results directory and results.xml entries
            - Processing any core files generated by the test

        Args:
            test (TestInfo): the test information
            repeat (int): the test repetition number
            stop_daos (bool): whether or not to stop daos servers/clients after the test
            archive (bool): whether or not to collect remote files generated by the test
            rename (bool): whether or not to rename the default avocado job-results directory names
            jenkinslog (bool): whether or not to update the results.xml to use Jenkins-style names
            core_files (dict): location and pattern defining where core files may be written
            threshold (str): optional upper size limit for test log files

        Returns:
            int: status code: 0 = success, >0 = failure

        """
        return_code = 0
        logger.debug("=" * 80)
        logger.info(
            "Processing the %s test after the run on repeat %s/%s", test, repeat, self.repeat)

        # Stop any agents or servers running via systemd
        if stop_daos:
            return_code |= self._stop_daos_agent_services(test)
            return_code |= self._stop_daos_server_service(test)
            return_code |= self._reset_server_storage(test)
            return_code |= self._cleanup_procs(test)

        # Optionally store all of the server and client config files and remote logs along with
        # this test's results. Also report an error if the test generated any log files with a
        # size exceeding the threshold.
        if archive:
            daos_test_log_dir = os.environ.get("DAOS_TEST_LOG_DIR", DEFAULT_DAOS_TEST_LOG_DIR)
            remote_files = OrderedDict()
            remote_files["local configuration files"] = {
                "source": daos_test_log_dir,
                "destination": os.path.join(self.job_results_dir, "latest", "daos_configs"),
                "pattern": "*_*_*.yaml",
                "hosts": self.local_host,
                "depth": 1,
                "timeout": 300,
            }
            remote_files["remote configuration files"] = {
                "source": os.path.join(os.sep, "etc", "daos"),
                "destination": os.path.join(self.job_results_dir, "latest", "daos_configs"),
                "pattern": "daos_*.yml",
                "hosts": test.host_info.all_hosts,
                "depth": 1,
                "timeout": 300,
            }
            remote_files["daos log files"] = {
                "source": daos_test_log_dir,
                "destination": os.path.join(self.job_results_dir, "latest", "daos_logs"),
                "pattern": "*log*",
                "hosts": test.host_info.all_hosts,
                "depth": 1,
                "timeout": 900,
            }
            remote_files["cart log files"] = {
                "source": daos_test_log_dir,
                "destination": os.path.join(self.job_results_dir, "latest", "cart_logs"),
                "pattern": "*log*",
                "hosts": test.host_info.all_hosts,
                "depth": 2,
                "timeout": 900,
            }
            remote_files["ULTs stacks dump files"] = {
                "source": os.path.join(os.sep, "tmp"),
                "destination": os.path.join(self.job_results_dir, "latest", "daos_dumps"),
                "pattern": "daos_dump*.txt*",
                "hosts": test.host_info.servers.hosts,
                "depth": 1,
                "timeout": 900,
            }
            remote_files["valgrind log files"] = {
                "source": os.environ.get("DAOS_TEST_SHARED_DIR", DEFAULT_DAOS_TEST_SHARED_DIR),
                "destination": os.path.join(self.job_results_dir, "latest", "valgrind_logs"),
                "pattern": "valgrind*",
                "hosts": test.host_info.servers.hosts,
                "depth": 1,
                "timeout": 900,
            }
            for index, hosts in enumerate(core_files):
                remote_files[f"core files {index + 1}/{len(core_files)}"] = {
                    "source": core_files[hosts]["path"],
                    "destination": os.path.join(self.job_results_dir, "latest", "stacktraces"),
                    "pattern": core_files[hosts]["pattern"],
                    "hosts": NodeSet(hosts),
                    "depth": 1,
                    "timeout": 1800,
                }
            for summary, data in remote_files.items():
                if not data["hosts"]:
                    continue
                return_code |= self._archive_files(
                    summary, data["hosts"].copy(), data["source"], data["pattern"],
                    data["destination"], data["depth"], threshold, data["timeout"], test)

        # Optionally rename the test results directory for this test
        if rename:
            return_code |= self._rename_avocado_test_dir(test, jenkinslog)

        return return_code

    def _stop_daos_agent_services(self, test):
        """Stop any daos_agent.service running on the hosts running servers.

        Args:
            test (TestInfo): the test information

        Returns:
            int: status code: 0 = success, 512 = failure

        """
        service = "daos_agent.service"
        # pylint: disable=unsupported-binary-operation
        hosts = test.host_info.clients.hosts | get_local_host()
        logger.debug("-" * 80)
        logger.debug("Verifying %s after running '%s'", service, test)
        return self._stop_service(hosts, service)

    def _stop_daos_server_service(self, test):
        """Stop any daos_server.service running on the hosts running servers.

        Args:
            test (TestInfo): the test information

        Returns:
            int: status code: 0 = success, 512 = failure

        """
        service = "daos_server.service"
        hosts = test.host_info.servers.hosts
        logger.debug("-" * 80)
        logger.debug("Verifying %s after running '%s'", service, test)
        return self._stop_service(hosts, service)

    def _stop_service(self, hosts, service):
        """Stop any daos_server.service running on the hosts running servers.

        Args:
            hosts (NodeSet): list of hosts on which to stop the service.
            service (str): name of the service

        Returns:
            int: status code: 0 = success, 512 = failure

        """
        result = {"status": 0}
        if hosts:
            status_keys = ["reset-failed", "stop", "disable"]
            mapping = {"stop": "active", "disable": "enabled", "reset-failed": "failed"}
            check_hosts = NodeSet(hosts)
            loop = 1
            # Reduce 'max_loops' to 2 once https://jira.hpdd.intel.com/browse/DAOS-7809
            # has been resolved
            max_loops = 3
            while check_hosts:
                # Check the status of the service on each host
                result = self._get_service_status(check_hosts, service)
                check_hosts = NodeSet()
                for key in status_keys:
                    if result[key]:
                        if loop == max_loops:
                            # Exit the while loop if the service is still running
                            logger.error(
                                " - Error %s still %s on %s", service, mapping[key], result[key])
                            result["status"] = 512
                        else:
                            # Issue the appropriate systemctl command to remedy the
                            # detected state, e.g. 'stop' for 'active'.
                            command = ["sudo", "-n", "systemctl", key, service]
                            run_remote(logger, result[key], " ".join(command))

                            # Run the status check again on this group of hosts
                            check_hosts.add(result[key])
                loop += 1
        else:
            logger.debug("  Skipping stopping %s service - no hosts", service)

        return result["status"]

    @staticmethod
    def _get_service_status(hosts, service):
        """Get the status of the daos_server.service.

        Args:
            hosts (NodeSet): hosts on which to get the service state
            service (str): name of the service

        Returns:
            dict: a dictionary with the following keys:
                - "status":       status code: 0 = success, 512 = failure
                - "stop":         NodeSet where to stop the daos_server.service
                - "disable":      NodeSet where to disable the daos_server.service
                - "reset-failed": NodeSet where to reset the daos_server.service

        """
        status = {
            "status": 0,
            "stop": NodeSet(),
            "disable": NodeSet(),
            "reset-failed": NodeSet()}
        status_states = {
            "stop": ["active", "activating", "deactivating"],
            "disable": ["active", "activating", "deactivating"],
            "reset-failed": ["failed"]}
        command = ["systemctl", "is-active", service]
        result = run_remote(logger, hosts, " ".join(command))
        for data in result.output:
            if data.timeout:
                status["status"] = 512
                status["stop"].add(data.hosts)
                status["disable"].add(data.hosts)
                status["reset-failed"].add(data.hosts)
                logger.debug("  %s: TIMEOUT", data.hosts)
                break
            logger.debug("  %s: %s", data.hosts, "\n".join(data.stdout))
            for key, state_list in status_states.items():
                for line in data.stdout:
                    if line in state_list:
                        status[key].add(data.hosts)
                        break
        return status

    @staticmethod
    def _reset_server_storage(test):
        """Reset the server storage for the hosts that ran servers in the test.

        This is a workaround to enable binding devices back to nvme or vfio-pci after they are
        unbound from vfio-pci to nvme.  This should resolve the "NVMe not found" error seen when
        attempting to start daos engines in the test.

        Args:
            test (TestInfo): the test information

        Returns:
            int: status code: 0 = success, 512 = failure

        """
        hosts = test.host_info.servers.hosts
        logger.debug("-" * 80)
        logger.debug("Resetting server storage after running %s", test)
        if hosts:
            commands = [
                "if lspci | grep -i nvme",
                f"then export COVFILE={BULLSEYE_FILE} && daos_server storage prepare -n --reset && "
                "sudo -n rmmod vfio_pci && sudo -n modprobe vfio_pci",
                "fi"]
            logger.info("Resetting server storage on %s after running '%s'", hosts, test)
            result = run_remote(logger, hosts, f"bash -c '{';'.join(commands)}'", timeout=600)
            if not result.passed:
                logger.debug("Ignoring any errors from these workaround commands")
        else:
            logger.debug("  Skipping resetting server storage - no server hosts")
        return 0

    def _cleanup_procs(self, test):
        """Cleanup any processes left running on remote nodes.

        Args:
            test (TestInfo): the test information

        Returns:
            int: status code: 0 = success; 4096 if processes were found

        """
        any_found = False
        hosts = test.host_info.all_hosts
        logger.debug("-" * 80)
        logger.debug("Cleaning up running processes after running %s", test)

        proc_pattern = "|".join(PROCS_TO_CLEANUP)
        logger.debug("Looking for running processes: %s", proc_pattern)
        pgrep_cmd = f"pgrep --list-full '{proc_pattern}'"
        pgrep_result = run_remote(logger, hosts, pgrep_cmd)
        if pgrep_result.passed_hosts:
            any_found = True
            logger.debug("Killing running processes: %s", proc_pattern)
            pkill_cmd = f"sudo -n pkill -e --signal KILL '{proc_pattern}'"
            pkill_result = run_remote(logger, pgrep_result.passed_hosts, pkill_cmd)
            if pkill_result.failed_hosts:
                message = f"Failed to kill processes on {pkill_result.failed_hosts}"
                self._fail_test(self.result.tests[-1], "Process", message)
            else:
                message = f"Running processes found on {pgrep_result.passed_hosts}"
                self._warn_test(self.result.tests[-1], "Process", message)

        logger.debug("Looking for mount types: %s", " ".join(TYPES_TO_UNMOUNT))
        # Use mount | grep instead of mount -t for better logging
        grep_pattern = "|".join(f'type {_type}' for _type in TYPES_TO_UNMOUNT)
        mount_grep_cmd = f"mount | grep -E '{grep_pattern}'"
        mount_grep_result = run_remote(logger, hosts, mount_grep_cmd)
        if mount_grep_result.passed_hosts:
            any_found = True
            logger.debug("Unmounting: %s", " ".join(TYPES_TO_UNMOUNT))
            type_list = ",".join(TYPES_TO_UNMOUNT)
            umount_cmd = f"sudo -n umount -v --all --force -t '{type_list}'"
            umount_result = run_remote(logger, mount_grep_result.passed_hosts, umount_cmd)
            if umount_result.failed_hosts:
                message = f"Failed to unmount on {umount_result.failed_hosts}"
                self._fail_test(self.result.tests[-1], "Process", message)
            else:
                message = f"Unexpected mounts on {mount_grep_result.passed_hosts}"
                self._warn_test(self.result.tests[-1], "Process", message)

        return 4096 if any_found else 0

    def _archive_files(self, summary, hosts, source, pattern, destination, depth, threshold,
                       timeout, test=None):
        """Archive the files from the source to the destination.

        Args:
            summary (str): description of the files being processed
            hosts (NodSet): hosts on which the files are located
            source (str): where the files are currently located
            pattern (str): pattern used to limit which files are processed
            destination (str): where the files should be moved to on this host
            depth (int): max depth for find command
            threshold (str): optional upper size limit for test log files
            timeout (int): number of seconds to wait for the command to complete.
            test (TestInfo): the test information

        Returns:
            int: status code: 0 = success, 16 = failure

        """
        logger.debug("=" * 80)
        logger.info(
            "Archiving %s from %s:%s to %s",
            summary, hosts, os.path.join(source, pattern), destination)
        logger.debug("  Remote hosts: %s", hosts.difference(self.local_host))
        logger.debug("  Local host:   %s", hosts.intersection(self.local_host))

        # List any remote files and their sizes and determine which hosts contain these files
        return_code, file_hosts = self._list_files(hosts, source, pattern, depth)
        if not file_hosts:
            # If no files are found then there is nothing else to do
            logger.debug("No %s files found on %s", os.path.join(source, pattern), hosts)
            return return_code

        if "log" in pattern:
            # Remove any empty files
            return_code |= self._remove_empty_files(file_hosts, source, pattern, depth)

            # Report an error if any files sizes exceed the threshold
            if threshold is not None:
                return_code |= self._check_log_size(file_hosts, source, pattern, depth, threshold)

            # Run cart_logtest on log files
            return_code |= self._cart_log_test(file_hosts, source, pattern, depth)

        # Remove any empty files
        return_code |= self._remove_empty_files(file_hosts, source, pattern, depth)

        # Compress any files larger than 1 MB
        return_code |= self._compress_files(file_hosts, source, pattern, depth)

        # Move the test files to the test-results directory on this host
        return_code |= self._move_files(file_hosts, source, pattern, destination, depth, timeout)

        if test and "core files" in summary:
            # Process the core files
            return_code |= self._process_core_files(os.path.split(destination)[0], test)

        return return_code

    def _list_files(self, hosts, source, pattern, depth):
        """List the files in source with that match the pattern.

        Args:
            hosts (NodSet): hosts on which the files are located
            source (str): where the files are currently located
            pattern (str): pattern used to limit which files are processed
            depth (int): max depth for find command

        Returns:
            tuple: a tuple containing:
                int: 0 = success, 16 = failure
                NodeSet: hosts with at least one file matching the pattern in the source directory

        """
        status = 0
        hosts_with_files = NodeSet()
        source_files = os.path.join(source, pattern)
        logger.debug("-" * 80)
        logger.debug("Listing any %s files on %s", source_files, hosts)
        other = ["-printf", "'%M %n %-12u %-12g %12k %t %p\n'"]
        result = run_remote(logger, hosts, find_command(source, pattern, depth, other))
        if not result.passed:
            message = f"Error determining if {source_files} files exist on {hosts}"
            self._fail_test(self.result.tests[-1], "Process", message)
            status = 16
        else:
            for data in result.output:
                for line in data.stdout:
                    if source in line:
                        logger.debug("Found at least one file match on %s: %s", data.hosts, line)
                        hosts_with_files.add(data.hosts)
                        break
                if re.findall(fr"{FAILURE_TRIGGER}", "\n".join(data.stdout)):
                    # If a file is found containing the FAILURE_TRIGGER then report a failure.
                    # This feature is used by avocado tests to verify that launch.py reports
                    # errors correctly in CI. See test_launch_failures in harness/basic.py for
                    # more details.
                    logger.debug(
                        "Found a file matching the '%s' failure trigger on %s",
                        FAILURE_TRIGGER, data.hosts)
                    message = f"Error trigger failure file found in {source} (error handling test)"
                    self._fail_test(self.result.tests[-1], "Process", message)
                    hosts_with_files.add(data.hosts)
                    status = 16

        logger.debug("List files results: status=%s, hosts_with_files=%s", status, hosts_with_files)
        return status, hosts_with_files

    def _check_log_size(self, hosts, source, pattern, depth, threshold):
        """Check if any file sizes exceed the threshold.

        Args:
            hosts (NodSet): hosts on which the files are located
            source (str): where the files are currently located
            pattern (str): pattern used to limit which files are processed
            depth (int): max depth for find command
            threshold (str): optional upper size limit for test log files

        Returns:
            int: status code: 0 = success, 32 = failure

        """
        source_files = os.path.join(source, pattern)
        logger.debug("-" * 80)
        logger.debug(
            "Checking for any %s files exceeding %s on %s", source_files, threshold, hosts)
        other = ["-size", f"+{threshold}", "-printf", "'%p %k KB'"]
        result = run_remote(logger, hosts, find_command(source, pattern, depth, other))
        if not result.passed:
            message = f"Error checking for {source_files} files exceeding the {threshold} threshold"
            self._fail_test(self.result.tests[-1], "Process", message)
            return 32

        # The command output will include the source path if the threshold has been exceeded
        for data in result.output:
            if source in "\n".join(data.stdout):
                message = f"One or more {source_files} files exceeded the {threshold} threshold"
                self._fail_test(self.result.tests[-1], "Process", message)
                return 32

        logger.debug("No %s file sizes found exceeding the %s threshold", source_files, threshold)
        return 0

    def _cart_log_test(self, hosts, source, pattern, depth):
        """Run cart_logtest on the log files.

        Args:
            hosts (NodSet): hosts on which the files are located
            source (str): where the files are currently located
            pattern (str): pattern used to limit which files are processed
            depth (int): max depth for find command

        Returns:
            int: status code: 0 = success, 16 = failure

        """
        source_files = os.path.join(source, pattern)
        cart_logtest = os.path.abspath(os.path.join("cart", "cart_logtest.py"))
        logger.debug("-" * 80)
        logger.debug("Running %s on %s files on %s", cart_logtest, source_files, hosts)
        other = ["-print0", "|", "xargs", "-0", "-r0", "-n1", "-I", "%", "sh", "-c",
                 f"'{cart_logtest} % > %.cart_logtest 2>&1'"]
        result = run_remote(
            logger, hosts, find_command(source, pattern, depth, other), timeout=2700)
        if not result.passed:
            message = f"Error running {cart_logtest} on the {source_files} files"
            self._fail_test(self.result.tests[-1], "Process", message)
            return 16
        return 0

    def _remove_empty_files(self, hosts, source, pattern, depth):
        """Remove any files with zero size.

        Args:
            hosts (NodSet): hosts on which the files are located
            source (str): where the files are currently located
            pattern (str): pattern used to limit which files are processed
            depth (int): max depth for find command

        Returns:
            bint: status code: 0 = success, 16 = failure

        """
        logger.debug("-" * 80)
        logger.debug("Removing any zero-length %s files in %s on %s", pattern, source, hosts)
        other = ["-empty", "-print", "-delete"]
        if not run_remote(logger, hosts, find_command(source, pattern, depth, other)).passed:
            message = f"Error removing any zero-length {os.path.join(source, pattern)} files"
            self._fail_test(self.result.tests[-1], "Process", message)
            return 16
        return 0

    def _compress_files(self, hosts, source, pattern, depth):
        """Compress any files larger than 1M.

        Args:
            hosts (NodSet): hosts on which the files are located
            source (str): where the files are currently located
            pattern (str): pattern used to limit which files are processed
            depth (int): max depth for find command

        Returns:
            int: status code: 0 = success, 16 = failure

        """
        logger.debug("-" * 80)
        logger.debug(
            "Compressing any %s files in %s on %s larger than 1M", pattern, source, hosts)
        other = ["-size", "+1M", "-print0", "|", "sudo", "-n", "xargs", "-0", "-r0", "lbzip2", "-v"]
        result = run_remote(logger, hosts, find_command(source, pattern, depth, other))
        if not result.passed:
            message = f"Error compressing {os.path.join(source, pattern)} files larger than 1M"
            self._fail_test(self.result.tests[-1], "Process", message)
            return 16
        return 0

    def _move_files(self, hosts, source, pattern, destination, depth, timeout):
        """Move files from the source to the destination.

        Args:
            hosts (NodSet): hosts on which the files are located
            source (str): where the files are currently located
            pattern (str): pattern used to limit which files are processed
            destination (str): where the files should be moved to on this host
            depth (int): max depth for find command
            timeout (int): number of seconds to wait for the command to complete.

        Returns:
            int: status code: 0 = success, 16 = failure

        """
        logger.debug("-" * 80)
        logger.debug("Moving files from %s to %s on %s", source, destination, hosts)

        # Core and dump files require a file ownership change before they can be copied
        if "stacktrace" in destination or "daos_dumps" in destination:
            # pylint: disable=import-outside-toplevel
            other = ["-print0", "|", "xargs", "-0", "-r0", "sudo", "-n", get_chown_command()]
            if not run_remote(logger, hosts, find_command(source, pattern, depth, other)).passed:
                message = f"Error changing {os.path.join(source, pattern)} file permissions"
                self._fail_test(self.result.tests[-1], "Process", message)
                return 16

        # Use the last directory in the destination path to create a temporary sub-directory on the
        # remote hosts in which all the source files matching the pattern will be copied. The entire
        # temporary sub-directory will then be copied back to this host and renamed as the original
        # destination directory plus the name of the host from which the files originated. Finally
        # delete this temporary sub-directory to remove the files from the remote hosts.
        rcopy_dest, tmp_copy_dir = os.path.split(destination)
        if source == os.path.join(os.sep, "etc", "daos"):
            # Use a temporary sub-directory in a directory where the user has permissions
            tmp_copy_dir = os.path.join(
                os.environ.get("DAOS_TEST_LOG_DIR", DEFAULT_DAOS_TEST_LOG_DIR), tmp_copy_dir)
            sudo_command = "sudo -n "
        else:
            tmp_copy_dir = os.path.join(source, tmp_copy_dir)
            sudo_command = ""

        # Create a temporary remote directory
        command = f"mkdir -p {tmp_copy_dir}"
        if not run_remote(logger, hosts, command).passed:
            message = f"Error creating temporary remote copy directory {tmp_copy_dir}"
            self._fail_test(self.result.tests[-1], "Process", message)
            return 16

        # Move all the source files matching the pattern into the temporary remote directory
        other = f"-print0 | xargs -0 -r0 -I '{{}}' {sudo_command}mv '{{}}' {tmp_copy_dir}/"
        if not run_remote(logger, hosts, find_command(source, pattern, depth, other)).passed:
            message = f"Error moving files to temporary remote copy directory {tmp_copy_dir}"
            self._fail_test(self.result.tests[-1], "Process", message)
            return 16

        # Clush -rcopy the temporary remote directory to this host
        command = ["clush", "-w", str(hosts), "-pv", "--rcopy", tmp_copy_dir, "--dest", rcopy_dest]
        return_code = 0
        try:
            run_local(logger, " ".join(command), check=True, timeout=timeout)

        except RunException:
            message = f"Error copying remote files to {destination}"
            self._fail_test(self.result.tests[-1], "Process", message, sys.exc_info())
            return_code = 16

        finally:
            # Remove the temporary remote directory on each host
            command = f"{sudo_command}rm -fr {tmp_copy_dir}"
            if not run_remote(logger, hosts, command).passed:
                message = f"Error removing temporary remote copy directory {tmp_copy_dir}"
                self._fail_test(self.result.tests[-1], "Process", message)
                return_code = 16

        return return_code

    def _process_core_files(self, test_job_results, test):
        """Generate a stacktrace for each core file detected.

        Args:
            test_job_results (str): the location of the core files
            test (TestInfo): the test information

        Returns:
            int: status code: 2048 = Core file exist; 256 = failure; 0 = success

        """
        core_file_processing = CoreFileProcessing(logger)
        try:
            corefiles_processed = core_file_processing.process_core_files(test_job_results, True,
                                                                          test=str(test))

        except CoreFileException:
            message = "Errors detected processing test core files"
            self._fail_test(self.result.tests[-1], "Process", message, sys.exc_info())
            return 256

        except Exception:       # pylint: disable=broad-except
            message = "Unhandled error processing test core files"
            self._fail_test(self.result.tests[-1], "Process", message, sys.exc_info())
            return 256

        if corefiles_processed > 0 and str(test) not in TEST_EXPECT_CORE_FILES:
            message = "One or more core files detected after test execution"
            self._fail_test(self.result.tests[-1], "Process", message, None)
            return 2048
        if corefiles_processed == 0 and str(test) in TEST_EXPECT_CORE_FILES:
            message = "No core files detected when expected"
            self._fail_test(self.result.tests[-1], "Process", message, None)
            return 256
        return 0

    def _rename_avocado_test_dir(self, test, jenkinslog):
        """Append the test name to its avocado job-results directory name.

        Args:
            test (TestInfo): the test information
            jenkinslog (bool): whether to update the results.xml with the Jenkins test names

        Returns:
            int: status code: 0 = success, 1024 = failure

        """
        avocado_logs_dir = self.avocado.get_logs_dir()
        test_logs_lnk = os.path.join(avocado_logs_dir, "latest")
        test_logs_dir = os.path.realpath(test_logs_lnk)

        logger.debug("=" * 80)
        logger.info("Renaming the avocado job-results directory")

        # Create the new avocado job-results test directory name
        new_test_logs_dir = "-".join([test_logs_dir, get_test_category(test.test_file)])
        if jenkinslog:
            new_test_logs_dir = os.path.join(avocado_logs_dir, test.directory, test.python_file)
            if self.repeat > 1:
                # When repeating tests ensure Jenkins-style avocado log directories
                # are unique by including the repeat count in the path
                new_test_logs_dir = os.path.join(
                    avocado_logs_dir, test.directory, test.python_file, test.name.repeat_str)
            try:
                os.makedirs(new_test_logs_dir)
            except OSError:
                message = f"Error creating {new_test_logs_dir}"
                self._fail_test(self.result.tests[-1], "Process", message, sys.exc_info())
                return 1024

        # Rename the avocado job-results test directory and update the 'latest' symlink
        logger.info("Renaming test results from %s to %s", test_logs_dir, new_test_logs_dir)
        try:
            os.rename(test_logs_dir, new_test_logs_dir)
            os.remove(test_logs_lnk)
            os.symlink(new_test_logs_dir, test_logs_lnk)
            logger.debug("Renamed %s to %s", test_logs_dir, new_test_logs_dir)
        except OSError:
            message = f"Error renaming {test_logs_dir} to {new_test_logs_dir}"
            self._fail_test(self.result.tests[-1], "Process", message, sys.exc_info())
            return 1024

        # Update the results.xml file with the new functional test class name
        if jenkinslog:
            xml_file = os.path.join(new_test_logs_dir, "results.xml")
            logger.debug("Updating the 'classname' field in the %s", xml_file)
            try:
                with open(xml_file, encoding="utf-8") as xml_buffer:
                    xml_data = xml_buffer.read()
            except OSError:
                message = f"Error reading {xml_file}"
                self._fail_test(self.result.tests[-1], "Process", message, sys.exc_info())
                return 1024

            # Save it for the Launchable [de-]mangle
            org_xml_data = xml_data

            # First, mangle the in-place file for Jenkins to consume
            xml_data = re.sub("classname=\"", f"classname=\"FTEST_{test.directory}.", xml_data)
            try:
                with open(xml_file, "w", encoding="utf-8") as xml_buffer:
                    xml_buffer.write(xml_data)
            except OSError:
                message = f"Error writing {xml_file}"
                self._fail_test(self.result.tests[-1], "Process", message, sys.exc_info())
                return 1024

            # Now mangle (or rather unmangle back to canonical xunit1 format)
            # another copy for Launchable
            xml_file = xml_file[0:-11] + "xunit1_results.xml"
            logger.debug("Updating the xml data for the Launchable %s file", xml_file)
            xml_data = org_xml_data
            org_name = r'(name=")\d+-\.\/.+\.(test_[^;]+);[^"]+(")'
            new_name = rf'\1\2\3 file="{test.test_file}"'
            xml_data = re.sub(org_name, new_name, xml_data)
            try:
                with open(xml_file, "w", encoding="utf-8") as xml_buffer:
                    xml_buffer.write(xml_data)
            except OSError:
                message = f"Error writing {xml_file}"
                self._fail_test(self.result.tests[-1], "Process", message, sys.exc_info())
                return 1024

        return 0

    def _summarize_run(self, status):
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
                if self.mode == "ci" or (self.mode == "normal" and bit_code == 1) or bit_code == 8:
                    # In CI mode the errors are reported in the results.xml, so always return 0
                    # In normal mode avocado test failures do not yield a non-zero exit status
                    # Interrupted avocado tests do not yield a non-zero exit status
                    continue
                return_code = 1
        return return_code


def main():
    """Launch DAOS functional tests."""
    # Parse the command line arguments
    description = [
        "DAOS functional test launcher",
        "",
        "Launches tests by specifying a test tag.  For example:",
        "\tbadconnect  --run pool connect tests that pass NULL ptrs, etc.",
        "\tbadevict    --run pool client evict tests that pass NULL ptrs, "
        "etc.",
        "\tbadexclude  --run pool target exclude tests that pass NULL ptrs, "
        "etc.",
        "\tbadparam    --run tests that pass NULL ptrs, etc.",
        "\tbadquery    --run pool query tests that pass NULL ptrs, etc.",
        "\tmulticreate --run tests that create multiple pools at once",
        "\tmultitarget --run tests that create pools over multiple servers",
        "\tpool        --run all pool related tests",
        "\tpoolconnect --run all pool connection related tests",
        "\tpooldestroy --run all pool destroy related tests",
        "\tpoolevict   --run all client pool eviction related tests",
        "\tpoolinfo    --run all pool info retrieval related tests",
        "\tquick       --run tests that complete quickly, with minimal "
        "resources",
        "",
        "Multiple tags can be specified:",
        "\ttag_a,tag_b -- run all tests with both tag_a and tag_b",
        "\ttag_a tag_b -- run all tests with either tag_a or tag_b",
        "",
        "Specifying no tags will run all of the available tests.",
        "",
        "Tests can also be launched by specifying a path to the python script "
        "instead of its tag.",
        "",
        "The placeholder server and client names in the yaml file can also be "
        "replaced with the following options:",
        "\tlaunch.py -ts node1,node2 -tc node3 <tag>",
        "\t  - Use node[1-2] to run the daos server in each test",
        "\t  - Use node3 to run the daos client in each test",
        "\tlaunch.py -ts node1,node2 <tag>",
        "\t  - Use node[1-2] to run the daos server or client in each test",
        "\tlaunch.py -ts node1,node2 -d <tag>",
        "\t  - Use node[1-2] to run the daos server or client in each test",
        "\t  - Discard of any additional server or client placeholders for "
        "each test",
        "",
        "You can also specify the sparse flag -s to limit output to "
        "pass/fail.",
        "\tExample command: launch.py -s pool"
    ]
    parser = ArgumentParser(
        prog="launcher.py",
        formatter_class=RawDescriptionHelpFormatter,
        description="\n".join(description))
    parser.add_argument(
        "-a", "--archive",
        action="store_true",
        help="archive host log files in the avocado job-results directory")
    parser.add_argument(
        "-c", "--clean",
        action="store_true",
        help="remove daos log files from the test hosts prior to the test")
    parser.add_argument(
        "-dsd", "--disable_stop_daos",
        action="store_true",
        help="disable stopping DAOS servers and clients between running tests")
    parser.add_argument(
        "-e", "--extra_yaml",
        action="append",
        default=None,
        type=str,
        help="additional yaml file to include with the test yaml file. Any "
             "entries in the extra yaml file can be used to replace an "
             "existing entry in the test yaml file.")
    parser.add_argument(
        "--failfast",
        action="store_true",
        help="stop the test suite after the first failure")
    parser.add_argument(
        "-i", "--include_localhost",
        action="store_true",
        help="include the local host when cleaning and archiving")
    parser.add_argument(
        "-ins", "--insecure_mode",
        action="store_true",
        help="Launch test with insecure-mode")
    parser.add_argument(
        "-j", "--jenkinslog",
        action="store_true",
        help="rename the avocado test logs directory for publishing in Jenkins")
    parser.add_argument(
        "-l", "--list",
        action="store_true",
        help="list the python scripts that match the specified tags")
    parser.add_argument(
        "-m", "--modify",
        action="store_true",
        help="modify the test yaml files but do not run the tests")
    parser.add_argument(
        "-mo", "--mode",
        choices=['normal', 'manual', 'ci'],
        default='normal',
        help="provide the mode of test to be run under. Default is normal, "
             "in which the final return code of launch.py is still zero if "
             "any of the tests failed. 'manual' is where the return code is "
             "non-zero if any of the tests as part of launch.py failed.")
    parser.add_argument(
        "-na", "--name",
        action="store",
        default="_".join(os.environ.get("STAGE_NAME", "Functional Manual").split()),
        type=str,
        help="avocado job-results directory name in which to place the launch log files."
             "If a directory with this name already exists it will be renamed with a '_old' suffix")
    parser.add_argument(
        "-n", "--nvme",
        action="store",
        help="comma-separated list of NVMe device PCI addresses to use as "
             "replacement values for the bdev_list in each test's yaml file.  "
             "Using the 'auto[:<filter>]' keyword will auto-detect any VMD "
             "controller or NVMe PCI address list on each of the '--test_servers' "
             "hosts - the optional '<filter>' can be used to limit auto-detected "
             "NVMe addresses, e.g. 'auto:Optane' for Intel Optane NVMe devices.  "
             "To limit the device detection to either VMD controller or NVMe "
             "devices the 'auto_vmd[:filter]' or 'auto_nvme[:<filter>]' keywords "
             "can be used, respectively.  When using 'filter' with VMD controllers, "
             "the filter is applied to devices managed by the controller, therefore "
             "only selecting controllers that manage the matching devices.")
    parser.add_argument(
        "-o", "--override",
        action="store_true",
        help="override the quantity of replacement values used in the test yaml file.")
    parser.add_argument(
        "-oc", "--overwrite_config",
        action="store_true",
        help="overwrite the avocado config files.")
    parser.add_argument(
        "-p", "--process_cores",
        action="store_true",
        help="process core files from tests")
    parser.add_argument(
        "-pr", "--provider",
        action="store",
        choices=[None] + list(PROVIDER_KEYS.values()),
        default=None,
        type=str,
        help="default provider to use in the test daos_server config file, "
             f"e.g. {', '.join(list(PROVIDER_KEYS.values()))}")
    parser.add_argument(
        "-r", "--rename",
        action="store_true",
        help="rename the avocado test logs directory to include the test name")
    parser.add_argument(
        "-re", "--repeat",
        action="store",
        default=1,
        type=int,
        help="number of times to repeat test execution")
    parser.add_argument(
        "-s", "--sparse",
        action="store_true",
        help="limit output to pass/fail")
    parser.add_argument(
        "-sc", "--slurm_control_node",
        action="store",
        default=str(get_local_host()),
        type=str,
        help="slurm control node where scontrol commands will be issued to check for the existence "
             "of any slurm partitions required by the tests")
    parser.add_argument(
        "-ss", "--slurm_setup",
        action="store_true",
        help="setup any slurm partitions required by the tests")
    parser.add_argument(
        "tags",
        nargs="*",
        type=str,
        help="test category or file to run")
    parser.add_argument(
        "-tc", "--test_clients",
        action="store",
        help="comma-separated list of hosts to use as replacement values for "
             "client placeholders in each test's yaml file")
    parser.add_argument(
        "-th", "--logs_threshold",
        action="store",
        help="collect log sizes and report log sizes that go past provided"
             "threshold. e.g. '-th 5M'"
             "Valid threshold units are: B, K, M, G, T")
    parser.add_argument(
        "-tm", "--timeout_multiplier",
        action="store",
        default=None,
        type=float,
        help="a multiplier to apply to each timeout value found in the test yaml")
    parser.add_argument(
        "-ts", "--test_servers",
        action="store",
        help="comma-separated list of hosts to use as replacement values for "
             "server placeholders in each test's yaml file.  If the "
             "'--test_clients' argument is not specified, this list of hosts "
             "will also be used to replace client placeholders.")
    parser.add_argument(
        "-v", "--verbose",
        action="count",
        default=0,
        help="verbosity output level. Specify multiple times (e.g. -vv) for "
             "additional output")
    parser.add_argument(
        "-y", "--yaml_directory",
        action="store",
        default=None,
        help="directory in which to write the modified yaml files. A temporary "
             "directory - which only exists for the duration of the launch.py "
             "command - is used by default.")
    args = parser.parse_args()

    # Setup the Launch object
    launch = Launch(args.name, args.mode)

    # Override arguments via the mode
    if args.mode == "ci":
        args.archive = True
        args.clean = True
        args.include_localhost = True
        args.jenkinslog = True
        args.process_cores = True
        args.rename = True
        args.sparse = True
        if not args.logs_threshold:
            args.logs_threshold = DEFAULT_LOGS_THRESHOLD
        args.slurm_setup = True

    # Perform the steps defined by the arguments specified
    try:
        status = launch.run(args)
    except Exception:       # pylint: disable=broad-except
        message = "Unknown exception raised during launch.py execution"
        status = launch.get_exit_status(1, message, "Unknown", sys.exc_info())
    sys.exit(status)


if __name__ == "__main__":
    main()
