"""
  (C) Copyright 2022-2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import sys
from collections import OrderedDict

# pylint: disable=import-error,no-name-in-module
from util.collection_utils import archive_files
from util.run_utils import find_command, run_remote


class CodeCoverage():
    """Test bullseye code coverage class."""

    def __init__(self, test_env):
        """Initialize a CodeCoverage object.

        Args:
            test_env (TestEnvironment): the current test environment.
        """
        self.__test_env = test_env
        self.__code_coverage = {
            "bullseye": {
                "hosts": None,
                "check_method": self.__check_bullseye,
                "setup_method": self.__setup_bullseye,
                "finalize_method": self.__finalize_bullseye,
            },
            "gcov": {
                "hosts": None,
                "check_method": self.__check_gcov,
                "setup_method": self.__setup_gcov,
                "finalize_method": self.__finalize_gcov,
            }
        }

    def check(self, logger, hosts):
        """Determine if code coverage collection is enabled.

        Args:
            logger (Logger): logger for the messages produced by this method
            hosts (NodeSet): hosts on which to check for code coverage source files
        """
        logger.debug("-" * 80)
        for _type, _info in self.__code_coverage.items():
            logger.debug("Checking for %s code coverage configuration", _type)
            if _info["check_method"](logger, hosts):
                _info["hosts"] = hosts

    def setup(self, logger, result):
        """Set up the hosts for code coverage collection.

        Args:
            logger (Logger): logger for the messages produced by this method
            result (TestResult): the test result used to update the status of the runner

        Returns:
            bool: False if there is a problem setting up code coverage; True otherwise
        """
        status = True
        logger.debug("-" * 80)
        for _type, _info in self.__code_coverage.items():
            if _info["hosts"] is None:
                continue
            logger.debug("Setting up %s code coverage on %s", _type, _info["hosts"])
            status &= _info["setup_method"](logger, _info["hosts"], result)
        return status

    def finalize(self, logger, job_results_dir, result):
        """Retrieve the code coverage collection information from the hosts.

        Args:
            logger (Logger): logger for the messages produced by this method
            job_results_dir (str): avocado job-results directory
            result (TestResult): the test result used to update the status of the runner

        Returns:
            bool: False if there is a problem retrieving code coverage; True otherwise
        """
        status = True
        logger.debug("-" * 80)
        for _type, _info in self.__code_coverage.items():
            if _info["hosts"] is None:
                continue
            logger.debug("Collecting %s code coverage information on %s", _type, _info["hosts"])
            status &= _info["finalize_method"](logger, _info["hosts"], job_results_dir, result)
        return status

    def __check_bullseye(self, logger, hosts):
        """Determine if bullseye code coverage collection is enabled.

        Args:
            logger (Logger): logger for the messages produced by this method
            hosts (NodeSet): hosts on which to check for bullseye code coverage source files

        Returns:
            bool: True if bullseye code coverage is enabled; False otherwise
        """
        result = run_remote(logger, hosts, " ".join(["ls", "-al", self.__test_env.bullseye_src]))
        if not result.passed:
            logger.info(
                "Bullseye code coverage collection not configured on %s", result.failed_hosts)
            return False
        logger.info("Bullseye code coverage collection configured on %s", hosts)
        return True

    def __setup_bullseye(self, logger, hosts, result):
        """Set up the hosts for bullseye code coverage collection.

        Args:
            logger (Logger): logger for the messages produced by this method
            hosts (NodeSet): hosts on which to setup code coverage
            result (TestResult): the test result used to update the status of the runner

        Returns:
            bool: False if there is a problem setting up bullseye code coverage; True otherwise
        """
        logger.debug("Removing any existing %s file", self.__test_env.bullseye_file)
        command = ["rm", "-fr", self.__test_env.bullseye_file]
        if not run_remote(logger, hosts, " ".join(command)).passed:
            message = "Error removing bullseye code coverage file on at least one host"
            result.fail_test(logger, "Run", message, None)
            return False

        logger.debug("Copying %s bullseye code coverage source file", self.__test_env.bullseye_src)
        command = ["cp", self.__test_env.bullseye_src, self.__test_env.bullseye_file]
        if not run_remote(logger, hosts, " ".join(command)).passed:
            message = "Error copying bullseye code coverage file on at least one host"
            result.fail_test(logger, "Run", message, None)
            return False

        logger.debug(
            "Updating %s bullseye code coverage file permissions", self.__test_env.bullseye_file)
        command = ["chmod", "777", self.__test_env.bullseye_file]
        if not run_remote(logger, hosts, " ".join(command)).passed:
            message = "Error updating bullseye code coverage file on at least one host"
            result.fail_test(logger, "Run", message, None)
            return False

        return True

    def __finalize_bullseye(self, logger, hosts, job_results_dir, result):
        """Retrieve the bullseye code coverage collection information from the hosts.

        Args:
            logger (Logger): logger for the messages produced by this method
            hosts (NodeSet): hosts on which to retrieve code coverage information
            job_results_dir (str): avocado job-results directory
            result (TestResult): the test result used to update the status of the runner

        Returns:
            bool: False if there is a problem retrieving bullseye code coverage; True otherwise

        """
        bullseye_path, bullseye_file = os.path.split(self.__test_env.bullseye_file)
        bullseye_dir = os.path.join(job_results_dir, "bullseye_coverage_logs")
        status = archive_files(
            logger, "bullseye coverage log files", hosts, bullseye_path,
            "".join([bullseye_file, "*"]), bullseye_dir, 1, None, 900, result)

        # Rename bullseye_coverage_logs.host/test.cov.* to bullseye_coverage_logs/test.host.cov.*
        for item in os.listdir(job_results_dir):
            item_full = os.path.join(job_results_dir, item)
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
        return status == 0

    def __check_gcov(self, logger, hosts):
        """Determine if gcov code coverage collection is enabled.

        Args:
            logger (Logger): logger for the messages produced by this method
            hosts (NodeSet): hosts on which to check for gcov code coverage source files

        Returns:
            bool: True if gcov code coverage is enabled; False otherwise
        """
        logger.info("Gcov code coverage collection configured on %s", hosts)
        return True
        source = os.environ.get("GCOV_PREFIX", "/tmp")
        result = run_remote(logger, hosts, find_command(source, "*.gcno"))
        if not result.passed:
            logger.info(
                "Gcov code coverage collection not configured on %s", result.failed_hosts)
            return False
        logger.info("Gcov code coverage collection configured on %s", hosts)
        return True

    def __setup_gcov(self, logger, hosts, result):
        """Set up the hosts for gcov code coverage collection.

        Args:
            logger (Logger): logger for the messages produced by this method
            hosts (NodeSet): hosts on which to setup code coverage
            result (TestResult): the test result used to update the status of the runner

        Returns:
            bool: False if there is a problem setting up gcov code coverage; True otherwise
        """
        source = os.environ.get("GCOV_PREFIX", "/tmp")
        other = ["-print", "-delete"]
        logger.debug("Removing any existing *.gcda files in %s", source)
        if not run_remote(logger, hosts, find_command(source, "*.gcda", None, other)).passed:
            message = "Error removing gcov code coverage data files on at least one host"
            result.fail_test(logger, "Run", message, None)
            return False
        return True

    def __finalize_gcov(self, logger, hosts, job_results_dir, result):
        """Retrieve the gcov code coverage collection information from the hosts.

        Args:
            logger (Logger): logger for the messages produced by this method
            hosts (NodeSet): hosts on which to retrieve code coverage information
            job_results_dir (str): avocado job-results directory
            result (TestResult): the test result used to update the status of the runner

        Returns:
            bool: False if there is a problem retrieving gcov code coverage; True otherwise
        """
        status = 0
        source = os.environ.get("GCOV_PREFIX", "/tmp")
        # Create a code coverage report on each host
        _report = os.path.join(job_results_dir, "code_coverage", "code_coverage.json")
        _gcovr = os.path.join(os.path.dirname(sys.executable), "gcovr")
        _commands = OrderedDict(
            [("create directory", f"mkdir -p {os.path.dirname(_report)}"),
             ("generate gcov report",
              f"{_gcovr} --json {_report} --gcov-ignore-parse-errors {source}")])
        for command_name, command in _commands.items():
            result = run_remote(logger, hosts, command)
            if not result.passed:
                logger.error("Failed to %s on hosts: %s", command_name, result.failed_hosts)
                status = 1

        # Archive the code coverage reports
        destination = os.path.join(job_results_dir, "code_coverage_report")
        status |= archive_files(
            logger, "gcov coverage reports", hosts, os.path.dirname(_report),
            os.path.basename(_report), destination, 1, None, 900, result)

        # Rename bullseye_coverage_logs.host/test.cov.* to bullseye_coverage_logs/test.host.cov.*
        return status == 0
