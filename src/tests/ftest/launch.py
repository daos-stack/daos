#!/usr/bin/env python3
"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import getpass
import json
import logging
import os
import re
import sys
from argparse import ArgumentParser, ArgumentTypeError, RawDescriptionHelpFormatter
from collections import OrderedDict
from tempfile import TemporaryDirectory

from ClusterShell.NodeSet import NodeSet
from process_core_files import get_core_file_pattern
# pylint: disable=import-error,no-name-in-module
from util.avocado_utils import AvocadoException, AvocadoInfo
from util.code_coverage_utils import CodeCoverage
from util.environment_utils import TestEnvironment, TestEnvironmentException, set_test_environment
from util.host_utils import get_local_host
from util.launch_utils import LaunchException, TestGroup, setup_fuse_config, summarize_run
from util.logger_utils import LOG_FILE_FORMAT, get_console_handler, get_file_handler
from util.network_utils import PROVIDER_ALIAS, SUPPORTED_PROVIDERS
from util.package_utils import find_packages
from util.results_utils import Job, LaunchTestName, Results
from util.run_utils import RunException
from util.storage_utils import StorageException
from util.yaml_utils import YamlException

DEFAULT_LOGS_THRESHOLD = "2150M"    # 2.1G
MAX_CI_REPETITIONS = 10


class LaunchError(Exception):
    """Error when launching Avocado"""


class Launch():
    """Class to launch avocado tests."""

    RESULTS_DIRS = (
        "daos_configs", "daos_logs", "cart_logs", "daos_dumps", "valgrind_logs", "stacktraces")

    def __init__(self, name, mode, slurm_install, slurm_setup):
        """Initialize a Launch object.

        Args:
            name (str): launch job name
            mode (str): execution mode, e.g. "normal", "manual", or "ci"
            slurm_install (bool): whether or not to install slurm RPMs if needed
            slurm_setup (bool): whether or not to enable configuring slurm if needed
        """
        self.name = name
        self.mode = mode
        self.slurm_install = slurm_install
        self.slurm_setup = slurm_setup

        self.avocado = AvocadoInfo()
        self.class_name = f"FTEST_launch.launch-{self.name.lower().replace('.', '-')}"
        self.logdir = None
        self.logfile = None
        self.tests = []
        self.tag_filters = []
        self.repeat = 1
        self.local_host = get_local_host()
        self.user = getpass.getuser()

        # Results tracking settings
        self.job_results_dir = None
        self.job = None
        self.result = None

        # Details about the run
        self.details = OrderedDict()

        # Options for creating slurm partitions
        self.slurm_control_node = NodeSet()
        self.slurm_partition_hosts = NodeSet()

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
        if self.result and self.result.tests:
            self.result.tests[0].finish_test(logger, message, fail_class, exc_info)
        else:
            # Log the status if a Results or TestResult object has not been defined
            if message is not None and fail_class is None:
                logger.debug(message)
            elif message is not None:
                logger.error(message)
            if exc_info is not None:
                logger.debug("Stacktrace", exc_info=True)

        # Write the details to a json file
        self._write_details_json()

        if self.job and self.result:
            # Generate the results.xml and results.html for this run
            self.job.generate_results(logger, self.result)

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

    def _configure(self, overwrite_config=False):
        """Configure launch to start logging and track test results.

        Args:
            overwrite (bool, optional): if true overwrite any existing avocado config files. If
                false do not modify any existing avocado config files. Defaults to False.

        Raises:
            LaunchException: if there are any issues obtaining data from avocado commands
        """
        # Setup the avocado config files to ensure these files are read by avocado
        self.avocado.set_config(overwrite_config)

        # Configure the logfile
        self.avocado.set_version()
        if self.avocado.major < 82:
            raise LaunchError("Avocado version 82 or above required")
        self.logdir = self.avocado.get_directory(os.path.join("launch", self.name.lower()))
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
        logger.info("Running with %s on python %s", self.avocado, sys.version)
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
        self.details["launch host"] = str(self.local_host)

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

    def run(self, args):
        """Perform the actions specified by the command line arguments.

        Args:
            args (argparse.Namespace): command line arguments for this program

        Returns:
            int: exit status for the steps executed
        """
        try:
            status = self._run(args)
        except LaunchError as error:
            return self.get_exit_status(1, error, error)
        except Exception as error:      # pylint: disable=broad-except
            message = f"Unknown exception raised during launch.py execution: {error}"
            status = self.get_exit_status(1, message, "Unknown", sys.exc_info())
        return status

    def _run(self, args):
        # pylint: disable=too-many-return-statements
        """Perform the actions specified by the command line arguments.

        Args:
            args (argparse.Namespace): command line arguments for this program

        Returns:
            int: exit status for the steps executed
        """
        status = 0

        # Setup launch to log and run the requested action
        try:
            self._configure(args.overwrite_config)
        except (AvocadoException, LaunchException):
            message = "Error configuring launch.py to start logging and track test results"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())

        # Add a test result to account for any non-test execution steps
        setup_result = self.result.add_test(
            self.class_name, LaunchTestName("./launch.py", 0, 0), self.logfile)
        setup_result.start()

        # Set the number of times to repeat execution of each test
        if self.mode == "ci" and args.repeat > MAX_CI_REPETITIONS:
            message = "The requested number of test repetitions exceeds the CI limitation."
            setup_result.warn_test(logger, "Setup", message)
            logger.debug(
                "The number of test repetitions has been reduced from %s to %s.",
                args.repeat, MAX_CI_REPETITIONS)
            args.repeat = MAX_CI_REPETITIONS
        self.repeat = args.repeat

        # Record the command line arguments
        logger.debug("Arguments:")
        for key in sorted(args.__dict__.keys()):
            logger.debug("  %s = %s", key, getattr(args, key))

        # A list of server hosts is required
        if not args.test_servers and not args.list:
            return self.get_exit_status(1, "Missing required '--test_servers' argument", "Setup")
        logger.info("Testing with hosts:       %s", args.test_servers.union(args.test_clients))
        self.details["test hosts"] = str(args.test_servers.union(args.test_clients))

        # Add the installed packages to the details json
        # pylint: disable=unsupported-binary-operation
        all_hosts = args.test_servers | args.test_clients | self.local_host
        self.details["installed packages"] = find_packages(
            logger, all_hosts, "'^(daos|libfabric|mercury|ior|openmpi|mpifileutils)-'")

        # Setup the test environment
        test_env = TestEnvironment()
        try:
            if args.list:
                set_test_environment(logger)
            else:
                set_test_environment(
                    logger, test_env, args.test_servers, args.test_clients, args.provider,
                    args.insecure_mode, self.details)
        except TestEnvironmentException as error:
            message = f"Error setting up test environment: {str(error)}"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())

        # Define the directory in which to create modified test yaml files
        if args.yaml_directory is None:
            # Create a temporary directory that will exist only during the execution launch.
            # pylint: disable=consider-using-with
            temp_dir = TemporaryDirectory()
            yaml_dir = temp_dir.name
        else:
            # Use the user-specified directory, which will exist after launch completes.
            yaml_dir = args.yaml_directory
            if not os.path.exists(yaml_dir):
                os.mkdir(yaml_dir)
        logger.info("Modified test yaml files being created in: %s", yaml_dir)

        # Define the test configs specified by the arguments
        group = TestGroup(
            self.avocado, test_env, args.test_servers, args.test_clients, args.slurm_control_node,
            args.tags, args.nvme, yaml_dir, args.yaml_extension)
        try:
            group.list_tests(logger, args.verbose)
        except RunException:
            message = f"Error detecting tests that match tags: {' '.join(args.tags)}"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())

        # Verify at least one test was requested
        if not group.tests:
            message = f"No tests found for tags: {' '.join(args.tags)}"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())

        # Done if just listing tests matching the tags
        if args.list and not args.modify:
            return self.get_exit_status(0, "Listing tests complete")

        # Setup the fuse configuration
        try:
            setup_fuse_config(logger, args.test_servers | args.test_clients)
        except LaunchException:
            # Warn but don't fail
            message = "Issue detected setting up the fuse configuration"
            setup_result.warn_test(logger, "Setup", message, sys.exc_info())

        # Get the core file pattern information
        core_files = {}
        if args.process_cores:
            try:
                all_hosts = args.test_servers | args.test_clients | self.local_host
                core_files = get_core_file_pattern(logger, all_hosts)
            except LaunchException:
                message = "Error obtaining the core file pattern information"
                return self.get_exit_status(1, message, "Setup", sys.exc_info())
        else:
            logger.debug("Not collecting core files")

        # Determine if bullseye code coverage collection is enabled
        code_coverage = CodeCoverage(test_env)
        # pylint: disable=unsupported-binary-operation
        code_coverage.check(logger, args.test_servers | self.local_host)

        # Update the test yaml files for the tests in this test group
        try:
            group.update_test_yaml(
                logger, args.scm_size, args.scm_mount, args.extra_yaml,
                args.timeout_multiplier, args.override, args.verbose, args.include_localhost)
        except (RunException, YamlException):
            message = "Error modifying the test yaml files"
            status |= self.get_exit_status(1, message, "Setup", sys.exc_info())
        except StorageException:
            message = "Error detecting storage information for test yaml files"
            status |= self.get_exit_status(1, message, "Setup", sys.exc_info())

        if args.modify:
            return self.get_exit_status(0, "Modifying test yaml files complete")

        # Configure slurm if any tests use partitions
        test_status = group.setup_slurm(
            logger, self.slurm_setup, self.slurm_install, self.user, self.result)

        # Split the timer for the test result to account for any non-test execution steps as not
        # to double report the test time accounted for in each individual test result
        setup_result.end()

        # Run the tests in this test group
        test_status |= group.run_tests(
            logger, self.result, self.repeat, self.slurm_setup, args.sparse, args.failfast,
            not args.disable_stop_daos, args.archive, args.rename, args.jenkinslog, core_files,
            args.logs_threshold, args.user_create, code_coverage, self.job_results_dir,
            self.logdir, args.clear_mounts)

        # Convert the test status to a launch.py status
        status |= summarize_run(logger, self.mode, test_status)

        # Record the group details
        self.details.update(group.details)

        # Restart the timer for the test result to account for any non-test execution steps
        setup_result.start()

        # Return the appropriate return code and mark the test result to account for any non-test
        # execution steps complete
        return self.get_exit_status(status, "Executing tests complete")


def __arg_type_file(val):
    """Parse a file argument.

    Args:
        val (str): path to a file

    Returns:
        str: the file path

    Raises:
        ArgumentTypeError: if val is not a file
    """
    if not os.path.isfile(val):
        raise ArgumentTypeError(f'File not found: {val}')
    return val


def __arg_type_nodeset(val):
    """Parse a NodeSet argument.

    Args:
        val (str): string representation of a NodeSet to parse

    Returns:
        NodeSet: the NodeSet

    Raises:
        ArgumentTypeError: if val cannot be parsed as a NodeSet
    """
    try:
        return NodeSet(val)
    except Exception as err:  # pylint: disable=broad-except
        raise ArgumentTypeError(f'Invalid NodeSet: {val}') from err


def __arg_type_find_size(val):
    """Parse a find -size argument.

    Args:
        val (str): string representation of find -size argument

    Returns:
        str: the find -size argument

    Raises:
        ArgumentTypeError: if val cannot be parsed as a find -size argument
    """
    if not re.match(r'^[0-9]+[bcwkMG]?$', val):
        raise ArgumentTypeError(f'Invalid find -size argument: {val}')
    return val


def __arg_type_mount_point(val):
    """Parse a mount point argument.

    The mount point does not need to exist on this host.

    Args:
        val (str): the mount point to parse

    Raises:
        ArgumentTypeError: if the value is not a string starting with '/'

    Returns:
        str: the mount point
    """
    try:
        if val.startswith(os.sep):
            return val
    except Exception as err:  # pylint: disable=broad-except
        raise ArgumentTypeError(f'Invalid mount point: {val}') from err


def main():
    """Launch DAOS functional tests."""
    # Parse the command line arguments
    description = [
        "DAOS functional test launcher",
        "",
        "Launches tests by specifying a test tag.  For example:",
        "\tpool        --run all pool related tests",
        "\tcontainer   --run all container related tests",
        "\tcontol      --run all control plane related tests",
        "",
        "Multiple tags can be specified:",
        "\ttag_a,tag_b -- run all tests with both tag_a and tag_b",
        "\ttag_a tag_b -- run all tests with either tag_a or tag_b",
        "",
        "Specifying no tags will run all of the available tests.",
        "",
        "Tests can also be launched by specifying a path to the python script instead of its tag.",
        "",
        "The placeholder server and client names in the yaml file can also be replaced with the "
        "following options:",
        "\tlaunch.py -ts node[1-2] -tc node3 <tag>",
        "\t  - Use node[1-2] to run the daos server in each test",
        "\t  - Use node3 to run the daos client in each test",
        "\tlaunch.py -ts node[1-2] <tag>",
        "\t  - Use node[1-2] to run the daos server or client in each test",
        "\tlaunch.py -ts node1 -tc node2 -o <tag>",
        "\t  - Use node1 to run the daos server in each test",
        "\t  - Use node2 to run the daos client in each test",
        "\t  - Override the number of servers and clients specified by the test",
        "",
        "You can also specify the sparse flag -s to limit output to pass/fail.",
        "\tExample command: launch.py -s pool",
        "",
        "The placeholder server storage configuration in the test yaml can also be replaced with "
        "the following --nvme argument options:",
        "\tauto[:filter]",
        "\t\treplace any test bdev_list placeholders with any NVMe disk or VMD controller address",
        "\t\tfound to exist on all server hosts. If 'filter' is specified use it to find devices",
        "\t\twith the 'filter' in the device description. If generating automatic storage extra",
        "\t\tfiles, use a 'class: dcpm' first storage tier.",
        "\tauto_md_on_ssd[:filter]",
        "\t\treplace any test bdev_list placeholders with any NVMe disk or VMD controller address",
        "\t\tfound to exist on all server hosts. If 'filter' is specified use it to find devices",
        "\t\twith the 'filter' in the device description. If generating automatic storage extra",
        "\t\tfiles, use a 'class: ram' first storage tier.",
        "\tauto_nvme[:filter]",
        "\t\treplace any test bdev_list placeholders with any NVMe disk found to exist on all ",
        "\t\tserver hosts. If a 'filter' is specified use it to find devices with the 'filter' ",
        "\t\tin the device description. If generating automatic storage extra files, use a ",
        "\t\t'class: dcpm' first storage tier.",
        "\tauto_vmd[:filter]",
        "\t\treplace any test bdev_list placeholders with any VMD controller address found to ",
        "\t\texist on all server hosts. If a 'filter' is specified use it to find devices with the",
        "\t\t'filter' in the device description. If generating automatic storage extra files, use",
        "\t\ta 'class: dcpm' first storage tier.",
        "\t<address>[,<address>]",
        "\t\treplace any test bdev_list placeholders with the addresses specified as long as the",
        "\t\taddress exists on each server host. If generating automatic storage extra files, use ",
        "\t\ta 'class: dcpm' first storage tier."
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
        "-c", "--clear_mounts",
        action="append",
        default=[],
        type=__arg_type_mount_point,
        help="mount points to remove before running each test")
    parser.add_argument(
        "-dsd", "--disable_stop_daos",
        action="store_true",
        help="disable stopping DAOS servers and clients between running tests")
    parser.add_argument(
        "-e", "--extra_yaml",
        action="append",
        default=None,
        type=__arg_type_file,
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
        help="Detect available disk options for replacing the devices specified in the server "
             "storage yaml configuration file. Supported options include:  auto[:filter], "
             "auto_md_on_ssd[:filter], auto_nvme[:filter], auto_vmd[:filter], or "
             "<address>[,<address>]")
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
        choices=[None] + list(SUPPORTED_PROVIDERS) + list(PROVIDER_ALIAS.keys()),
        default=None,
        type=str,
        help="default provider to use in the test daos_server config file, "
             f"e.g. {', '.join(list(SUPPORTED_PROVIDERS) + list(PROVIDER_ALIAS.keys()))}")
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
        type=__arg_type_nodeset,
        default=get_local_host(),
        help="slurm control node where scontrol commands will be issued to check for the existence "
             "of any slurm partitions required by the tests")
    parser.add_argument(
        "-si", "--slurm_install",
        action="store_true",
        help="enable installing slurm RPMs if required by the tests")
    parser.add_argument(
        "--scm_mount",
        action="store",
        default="/mnt/daos",
        type=str,
        help="the scm_mount base path to use in each server engine tier 0 storage config when "
             "generating an automatic storage config (test yaml includes 'storage: auto'). The "
             "engine number will be added at the end of this string, e.g. '/mnt/daos0'.")
    parser.add_argument(
        "-ss", "--slurm_setup",
        action="store_true",
        help="enable setting up slurm partitions if required by the tests")
    parser.add_argument(
        "--scm_size",
        action="store",
        default=0,
        type=int,
        help="the scm_size value (in GiB units) to use in each server engine tier 0 ram storage "
             "config when generating an automatic storage config (test yaml includes 'storage: "
             "auto'). Set value to '0' to automatically determine the optimal ramdisk size")
    parser.add_argument(
        "tags",
        nargs="*",
        type=str,
        help="test category or file to run")
    parser.add_argument(
        "-tc", "--test_clients",
        action="store",
        type=__arg_type_nodeset,
        default=NodeSet(),
        help="comma-separated list of hosts to use as replacement values for "
             "client placeholders in each test's yaml file")
    parser.add_argument(
        "-th", "--logs_threshold",
        action="store",
        type=__arg_type_find_size,
        help="collect log sizes and report log sizes that go past provided"
             "threshold. e.g. '-th 5M'"
             "Valid threshold units are: b, c, w, k, M, G for find -size")
    parser.add_argument(
        "-tm", "--timeout_multiplier",
        action="store",
        default=None,
        type=float,
        help="a multiplier to apply to each timeout value found in the test yaml")
    parser.add_argument(
        "-ts", "--test_servers",
        action="store",
        type=__arg_type_nodeset,
        default=NodeSet(),
        help="comma-separated list of hosts to use as replacement values for "
             "server placeholders in each test's yaml file.  If the "
             "'--test_clients' argument is not specified, this list of hosts "
             "will also be used to replace client placeholders.")
    parser.add_argument(
        "-u", "--user_create",
        action="store_true",
        help="create additional users defined by each test's yaml file")
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
    parser.add_argument(
        "-ye", "--yaml_extension",
        action="store",
        default=None,
        help="extension used to run custom test yaml files. If a test yaml file "
             "exists with the specified extension - e.g. dtx/basic.custom.yaml "
             "for --yaml_extension=custom - this file will be used instead of the "
             "standard test yaml file.")
    args = parser.parse_args()

    # Override arguments via the mode
    if args.mode == "ci":
        args.archive = True
        args.include_localhost = True
        args.jenkinslog = True
        args.overwrite_config = True    # to ensure CI expected path is used for test results
        args.process_cores = True
        args.rename = True
        args.sparse = True
        if not args.logs_threshold:
            args.logs_threshold = DEFAULT_LOGS_THRESHOLD
        args.slurm_install = True
        args.slurm_setup = True
        args.user_create = True
        args.clear_mounts.append("/mnt/daos")
        args.clear_mounts.append("/mnt/daos0")
        args.clear_mounts.append("/mnt/daos1")

    # Setup the Launch object
    launch = Launch(args.name, args.mode, args.slurm_install, args.slurm_setup)

    # Perform the steps defined by the arguments specified
    sys.exit(launch.run(args))


if __name__ == "__main__":
    # Set up a logger for the console messages. Initially configure the console handler to report
    # debug messages until a file logger can be established to handle the debug messages. After
    # which the console logger will be updated to handle info messages.
    logger = logging.getLogger(__name__)
    logger.setLevel(logging.DEBUG)
    logger.addHandler(get_console_handler("%(message)s", logging.DEBUG))
    main()
else:
    logger = logging.getLogger()
