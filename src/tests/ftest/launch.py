#!/usr/bin/env python3
"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines

from argparse import ArgumentParser, RawDescriptionHelpFormatter
from collections import OrderedDict
from tempfile import TemporaryDirectory
import getpass
import json
import logging
import os
import re
import sys

# When SRE-439 is fixed we should be able to include these import statements here
# from avocado.core.settings import settings
# from avocado.core.version import MAJOR, MINOR
# from avocado.utils.stacktrace import prepare_exc_info
from ClusterShell.NodeSet import NodeSet

# When SRE-439 is fixed we should be able to include these import statements here
# from util.distro_utils import detect
# pylint: disable=import-error,no-name-in-module
from slurm_setup import SlurmSetup, SlurmSetupException

# Update the path to support utils files that import other utils files
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "util"))
# pylint: disable=import-outside-toplevel
from code_coverage_utils import CodeCoverage                                            # noqa: E402
from data_utils import list_unique, dict_extract_values                                 # noqa: E402
from host_utils import get_local_host                                                   # noqa: E402
from launch_utils import LaunchException, AvocadoInfo, TestInfo, TestRunner, \
    fault_injection_enabled                                                             # noqa: E402
from logger_utils import get_console_handler, get_file_handler                          # noqa: E402
from package_utils import find_packages                                                 # noqa: E402
from results_utils import Job, Results, LaunchTestName                                  # noqa: E402
from run_utils import run_local, run_remote, RunException                               # noqa: E402
from storage_utils import StorageInfo, StorageException                                 # noqa: E402
from test_env_utils import TestEnvironment, TestEnvironmentException, set_test_environment, \
    PROVIDER_KEYS, PROVIDER_ALIAS                                                       # noqa: E402
from yaml_utils import get_yaml_data, YamlUpdater, YamlException                        # noqa: E402

DEFAULT_LOGS_THRESHOLD = "2150M"    # 2.1G
LOG_FILE_FORMAT = "%(asctime)s %(levelname)-5s %(funcName)30s: %(message)s"
MAX_CI_REPETITIONS = 10


# Set up a logger for the console messages. Initially configure the console handler to report debug
# messages until a file logger can be established to handle the debug messages. After which the
# console logger will be updated to handle info messages.
logger = logging.getLogger()
logger.setLevel(logging.DEBUG)
logger.addHandler(get_console_handler("%(message)s", logging.DEBUG))


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
        clean_name = self.name.lower().replace('.', '-').replace(' ', '_')
        self.class_name = f"FTEST_launch.launch-{clean_name}"
        self.logdir = None
        self.logfile = None
        self.tests = []
        self.tag_filters = []
        self.repeat = 1
        self.local_host = get_local_host()
        self.user = getpass.getuser()

        # Functional test environment variables
        self.test_env = TestEnvironment()

        # Results tracking settings
        self.job_results_dir = None
        self.job = None
        self.result = None

        # Options for bullseye code coverage
        self.code_coverage = CodeCoverage(self.test_env)

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
            self.result.tests[0].finish_test(message, fail_class, exc_info)
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
            self.job.generate_results(self.result)

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
        # Setup launch to log and run the requested action
        try:
            self._configure(args.overwrite_config)
        except LaunchException:
            message = "Error configuring launch.py to start logging and track test results"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())

        # Add a test result to account for any non-test execution steps
        setup_result = self.result.add_test(
            self.class_name, LaunchTestName("./launch.py", 0, 0), self.logfile)
        setup_result.start()

        # Set the number of times to repeat execution of each test
        if self.mode == "ci" and args.repeat > MAX_CI_REPETITIONS:
            message = "The requested number of test repetitions exceeds the CI limitation."
            setup_result.warn_test("Setup", message)
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
            if args.list:
                set_test_environment(None)
            else:
                set_test_environment(
                    self.test_env, args.test_servers, args.test_clients, args.provider,
                    args.insecure_mode, self.details)
        except TestEnvironmentException as error:
            message = f"Error setting up test environment: {str(error)}"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())

        # Process the tags argument to determine which tests to run - populates self.tests
        try:
            self.list_tests(args.tags, args.yaml_extension)
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
            self.setup_test_files(yaml_dir, args)
        except (RunException, YamlException):
            message = "Error modifying the test yaml files"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())
        except StorageException:
            message = "Error detecting storage information for test yaml files"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())
        if args.modify:
            return self.get_exit_status(0, "Modifying test yaml files complete")

        try:
            self.setup_fuse_config(args.test_servers | args.test_clients)
        except LaunchException:
            # Warn but don't fail
            message = "Issue detected setting up the fuse configuration"
            setup_result.warn_test("Setup", message, sys.exc_info())

        # Get the core file pattern information
        try:
            core_files = self._get_core_file_pattern(
                args.test_servers, args.test_clients, args.process_cores)
        except LaunchException:
            message = "Error obtaining the core file pattern information"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())

        # Add the installed packages to the details json
        # pylint: disable=unsupported-binary-operation
        all_hosts = args.test_servers | args.test_clients | self.local_host
        self.details["installed packages"] = find_packages(
            all_hosts, "'^(daos|libfabric|mercury|ior|openmpi|mpifileutils|)-'")

        # Split the timer for the test result to account for any non-test execution steps as not to
        # double report the test time accounted for in each individual test result
        setup_result.end()

        # Determine if bullseye code coverage collection is enabled
        # pylint: disable=unsupported-binary-operation
        self.code_coverage.check(args.test_servers | self.local_host)

        # Define options for creating any slurm partitions required by the tests
        try:
            self.slurm_control_node = NodeSet(args.slurm_control_node)
        except TypeError:
            message = f"Invalid '--slurm_control_node={args.slurm_control_node}' argument"
            return self.get_exit_status(1, message, "Setup", sys.exc_info())
        self.slurm_partition_hosts.add(args.test_clients or args.test_servers)

        # Execute the tests
        status = self.run_tests(
            args.sparse, args.failfast, not args.disable_stop_daos, args.archive, args.rename,
            args.jenkins_xml, core_files, args.logs_threshold, args.user_create)

        # Restart the timer for the test result to account for any non-test execution steps
        setup_result.start()

        # Return the appropriate return code and mark the test result to account for any non-test
        # execution steps complete
        return self.get_exit_status(status, "Executing tests complete")

    def _configure(self, overwrite_config=False):
        """Configure launch to start logging and track test results.

        Args:
            overwrite (bool, optional): if true overwrite any existing avocado config files. If
                false do not modify any existing avocado config files. Defaults to False.

        Raises:
            LaunchException: if there are any issues obtaining data from avocado commands

        """
        # Setup the avocado config files to ensure these files are read by avocado
        self.avocado.set_config(self.name, overwrite_config)

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

    def list_tests(self, tags, yaml_extension=None):
        """List the test files matching the tags.

        Populates the self.tests list and defines the self.tag_filters list to use when running
        tests.

        Args:
            tags (list): a list of tags or test file names
            yaml_extension (str, optional): optional test yaml file extension to use when creating
                the TestInfo object.

        Raises:
            RunException: if there is a problem listing tests

        """
        logger.debug("-" * 80)
        self.tests = []
        self.tag_filters = []

        # Determine if fault injection is enabled
        fault_tag = "-faults"
        fault_filter = f"--filter-by-tags={fault_tag}"
        faults_enabled = fault_injection_enabled()

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
        output = run_local(" ".join(command), check=True)
        unique_test_files = set(re.findall(self.avocado.get_list_regex(), output.stdout))
        for index, test_file in enumerate(unique_test_files):
            self.tests.append(TestInfo(test_file, index + 1, yaml_extension))
            logger.info("  %s", self.tests[-1])

    def setup_test_files(self, yaml_dir, args):
        """Set up the test yaml files with any placeholder replacements.

        Args:
            yaml_dir (str): directory in which to write the modified yaml files
            args (argparse.Namespace): command line arguments for this program

        Raises:
            RunException: if there is a problem updating the test yaml files
            YamlException: if there is an error getting host information from the test yaml files
            StorageException: if there is an error getting storage information

        """
        # Detect available disk options for test yaml replacement
        # Supported --nvme options:
        #
        #   auto[:filter]           = replace any test bdev_list placeholders with any NVMe disk or
        #                             VMD controller address found to exist on all server hosts. If
        #                             a 'filter' is specified use it to find devices with the
        #                             'filter' in the device description. If generating automatic
        #                             storage extra files, use a 'class: dcpm' first storage tier.
        #
        #   auto_md_on_ssd[:filter] = replace any test bdev_list placeholders with any NVMe disk or
        #                             VMD controller address found to exist on all server hosts. If
        #                             a 'filter' is specified use it to find devices with the
        #                             'filter' in the device description. If generating automatic
        #                             storage extra files, use a 'class: ram' first storage tier.
        #
        #   auto_nvme[:filter]      = replace any test bdev_list placeholders with any NVMe disk
        #                             found to exist on all server hosts. If a 'filter' is specified
        #                             use it to find devices with the 'filter' in the device
        #                             description. If generating automatic storage extra files, use
        #                             a 'class: dcpm' first storage tier.
        #
        #   auto_vmd[:filter]       = replace any test bdev_list placeholders with any VMD
        #                             controller address found to exist on all server hosts. If a
        #                             'filter' is specified use it to find devices with the 'filter'
        #                             in the device description. If generating automatic storage
        #                             extra files, use a 'class: dcpm' first storage tier.
        #
        #   <address>[,<address>]   = replace any test bdev_list placeholders with the addresses
        #                             specified as long as the address exists on each server host.
        #                             If generating automatic storage extra files, use a
        #                             'class: dcpm' first storage tier.
        #
        storage = None
        storage_info = StorageInfo(logger, args.test_servers)
        tier_0_type = "pmem"
        control_metadata = None
        max_nvme_tiers = 1
        if args.nvme:
            kwargs = {"device_filter": f"'({'|'.join(args.nvme.split(','))})'"}
            if args.nvme.startswith("auto"):
                # Separate any optional filter from the key
                nvme_args = args.nvme.split(":")
                kwargs["device_filter"] = nvme_args[1] if len(nvme_args) > 1 else None
            logger.debug("-" * 80)
            storage_info.scan(**kwargs)

            # Determine which storage device types to use when replacing keywords in the test yaml
            if args.nvme.startswith("auto_nvme"):
                storage = ",".join([dev.address for dev in storage_info.disk_devices])
            elif args.nvme.startswith("auto_vmd") or storage_info.controller_devices:
                storage = ",".join([dev.address for dev in storage_info.controller_devices])
            else:
                storage = ",".join([dev.address for dev in storage_info.disk_devices])

            # Change the auto-storage extra yaml format if md_on_ssd is requested
            if args.nvme.startswith("auto_md_on_ssd"):
                tier_0_type = "ram"
                max_nvme_tiers = 5
                control_metadata = os.path.join(self.test_env.log_dir, 'control_metadata')

        self.details["storage"] = storage_info.device_dict()

        updater = YamlUpdater(
            logger, args.test_servers, args.test_clients, storage, args.timeout_multiplier,
            args.override, args.verbose)

        # Replace any placeholders in the extra yaml file, if provided
        if args.extra_yaml:
            common_extra_yaml = [
                updater.update(extra, yaml_dir) or extra for extra in args.extra_yaml]
            for test in self.tests:
                test.extra_yaml.extend(common_extra_yaml)

        # Generate storage configuration extra yaml files if requested
        self._add_auto_storage_yaml(
            storage_info, yaml_dir, tier_0_type, args.scm_size, args.scm_mount, max_nvme_tiers,
            control_metadata)

        # Replace any placeholders in the test yaml file
        for test in self.tests:
            new_yaml_file = updater.update(test.yaml_file, yaml_dir)
            if new_yaml_file:
                if args.verbose > 0:
                    # Optionally display a diff of the yaml file
                    run_local(f"diff -y {test.yaml_file} {new_yaml_file}", check=False)
                test.yaml_file = new_yaml_file

            # Display the modified yaml file variants with debug
            command = ["avocado", "variants", "--mux-yaml", test.yaml_file]
            if test.extra_yaml:
                command.extend(test.extra_yaml)
            command.extend(["--summary", "3"])
            run_local(" ".join(command))

            # Collect the host information from the updated test yaml
            test.set_yaml_info(args.include_localhost)

    def _add_auto_storage_yaml(self, storage_info, yaml_dir, tier_0_type, scm_size, scm_mount,
                               max_nvme_tiers, control_metadata):
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

    @staticmethod
    def setup_fuse_config(hosts):
        """Set up the system fuse config file.

        Args:
            hosts (NodeSet): hosts to setup

        Raises:
            LaunchException: if setup fails

        """
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
            if not run_remote(hosts, command.format(config)).passed:
                raise LaunchException(f"Failed to setup {config}")

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
        result = run_remote(all_hosts, command)

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

    def run_tests(self, sparse, fail_fast, stop_daos, archive, rename, jenkins_xml, core_files,
                  threshold, user_create):
        # pylint: disable=too-many-arguments
        """Run all the tests.

        Args:
            sparse (bool): whether or not to display the shortened avocado test output
            fail_fast (bool): whether or not to fail the avocado run command upon the first failure
            stop_daos (bool): whether or not to stop daos servers/clients after the test
            archive (bool): whether or not to collect remote files generated by the test
            rename (bool): whether or not to rename the default avocado job-results directory names
            jenkins_xml (bool): whether or not to update the results.xml to use Jenkins-style names
            core_files (dict): location and pattern defining where core files may be written
            threshold (str): optional upper size limit for test log files
            user_create (bool): whether to create extra test users defined by the test

        Returns:
            int: status code to use when exiting launch.py

        """
        return_code = 0
        runner = TestRunner(
            self.avocado, self.result, len(self.tests), self.repeat, self.tag_filters)

        # Display the location of the avocado logs
        logger.info("Avocado job results directory: %s", self.job_results_dir)

        # Configure slurm if any tests use partitions
        return_code |= self.setup_slurm()

        # Configure hosts to collect code coverage
        if not self.code_coverage.setup(self.result.tests[0]):
            return_code |= 128

        # Run each test for as many repetitions as requested
        for repeat in range(1, self.repeat + 1):
            logger.info("-" * 80)
            logger.info("Starting test repetition %s/%s", repeat, self.repeat)

            for index, test in enumerate(self.tests):
                # Define a log for the execution of this test for this repetition
                test_log_file = test.get_log_file(self.logdir, repeat, self.repeat)
                logger.info("-" * 80)
                logger.info("Log file for repetition %s of %s: %s", repeat, test, test_log_file)
                test_file_handler = get_file_handler(test_log_file, LOG_FILE_FORMAT, logging.DEBUG)
                logger.addHandler(test_file_handler)

                # Prepare the hosts to run the tests
                step_status = runner.prepare(
                    test_log_file, test, repeat, user_create, self.slurm_setup,
                    self.slurm_control_node, self.slurm_partition_hosts)
                if step_status:
                    # Do not run this test - update its failure status to interrupted
                    return_code |= step_status
                    continue

                # Run the test with avocado
                return_code |= runner.execute(test, repeat, index + 1, sparse, fail_fast)

                # Archive the test results
                return_code |= runner.process(
                    self.job_results_dir, test, repeat, stop_daos, archive, rename, jenkins_xml,
                    core_files, threshold)

                # Display disk usage after the test is complete
                self.display_disk_space(self.logdir)

                # Stop logging to the test log file
                logger.removeHandler(test_file_handler)

        # Collect code coverage files after all test have completed
        if not self.code_coverage.finalize(self.job_results_dir, self.result.tests[0]):
            return_code |= 16

        # Summarize the run
        return self._summarize_run(return_code)

    def setup_slurm(self):
        """Set up slurm on the hosts if any tests are using partitions.

        Returns:
            int: status code: 0 = success, 128 = failure
        """
        status = 0
        logger.debug("-" * 80)
        logger.info("Setting up slurm partitions if required by tests")
        if not any(test.yaml_info["client_partition"] for test in self.tests):
            logger.debug("  No tests using client partitions detected - skipping slurm setup")
            return status

        if not self.slurm_setup:
            logger.debug("  The 'slurm_setup' argument is not set - skipping slurm setup")
            return status

        status |= self.setup_application_directory()

        slurm_setup = SlurmSetup(logger, self.slurm_partition_hosts, self.slurm_control_node, True)
        logger.debug("-" * 80)
        try:
            if self.slurm_install:
                slurm_setup.install()
            slurm_setup.update_config(self.user, 'daos_client')
            slurm_setup.start_munge(self.user)
            slurm_setup.start_slurm(self.user, True)
        except SlurmSetupException:
            message = "Error setting up slurm"
            self.result.tests[-1].fail_test("Run", message, sys.exc_info())
            status |= 128
        except Exception:       # pylint: disable=broad-except
            message = "Unknown error setting up slurm"
            self.result.tests[-1].fail_test("Run", message, sys.exc_info())
            status |= 128

        return status

    def setup_application_directory(self):
        """Set up the application directory.

        Returns:
            int: status code: 0 = success, 128 = failure
        """
        app_dir = os.environ.get('DAOS_TEST_APP_DIR')
        app_src = os.environ.get('DAOS_TEST_APP_SRC')

        logger.debug("-" * 80)
        logger.debug("Setting up the '%s' application directory", app_dir)
        if not os.path.exists(app_dir):
            # Create the apps directory if it does not already exist
            try:
                logger.debug('  Creating the application directory')
                os.makedirs(app_dir)
            except OSError:
                message = 'Error creating the application directory'
                self.result.tests[-1].fail_test('Run', message, sys.exc_info())
                return 128
        else:
            logger.debug('  Using the existing application directory')

        if app_src and os.path.exists(app_src):
            logger.debug("  Copying applications from the '%s' directory", app_src)
            run_local(f"ls -al '{app_src}'")
            for app in os.listdir(app_src):
                try:
                    run_local(f"cp -r '{os.path.join(app_src, app)}' '{app_dir}'", check=True)
                except RunException:
                    message = 'Error copying files to the application directory'
                    self.result.tests[-1].fail_test('Run', message, sys.exc_info())
                    return 128

        logger.debug("  Applications in '%s':", app_dir)
        run_local(f"ls -al '{app_dir}'")
        return 0

    @staticmethod
    def display_disk_space(path):
        """Display disk space of provided path destination.

        Args:
            log (logger): logger for the messages produced by this method
            path (str): path to directory to print disk space for.
        """
        logger.debug("-" * 80)
        logger.debug("Current disk space usage of '%s'", path)
        try:
            run_local(f"df -h '{path}'", check=False)
        except RunException:
            pass

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
        "-j", "--jenkins_xml",
        action="store_true",
        help="rename the avocado test xml for publishing in Jenkins")
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
        choices=[None] + list(PROVIDER_KEYS.values()) + list(PROVIDER_ALIAS.keys()),
        default=None,
        type=str,
        help="default provider to use in the test daos_server config file, "
             f"e.g. {', '.join(list(PROVIDER_KEYS.values()) + list(PROVIDER_ALIAS.keys()))}")
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
        args.jenkins_xml = True
        args.process_cores = True
        args.rename = True
        args.sparse = True
        if not args.logs_threshold:
            args.logs_threshold = DEFAULT_LOGS_THRESHOLD
        args.slurm_install = True
        args.slurm_setup = True
        args.user_create = True

    # Setup the Launch object
    launch = Launch(args.name, args.mode, args.slurm_install, args.slurm_setup)

    # Perform the steps defined by the arguments specified
    try:
        status = launch.run(args)
    except Exception:       # pylint: disable=broad-except
        message = "Unknown exception raised during launch.py execution"
        status = launch.get_exit_status(1, message, "Unknown", sys.exc_info())
    sys.exit(status)


if __name__ == "__main__":
    main()
