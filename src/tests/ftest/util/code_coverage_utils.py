"""
  (C) Copyright 2022-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

# pylint: disable=import-error,no-name-in-module
from util.collection_utils import archive_files
from util.run_utils import RunException, run_local, run_remote


class CodeCoverage():
    """Test bullseye code coverage class."""

    def __init__(self, test_env):
        """Initialize a CodeCoverage object.

        Args:
            test_env (TestEnvironment): the current test environment.
        """
        self.__hosts = None
        self.__test_env = test_env

    def check(self, logger, hosts):
        """Determine if bullseye code coverage collection is enabled.

        Args:
            logger (Logger): logger for the messages produced by this method
            hosts (NodeSet): hosts on which to check for bullseye code coverage source files
        """
        logger.debug("-" * 80)
        logger.debug("Checking for bullseye code coverage configuration")
        result = run_remote(logger, hosts, " ".join(["ls", "-al", self.__test_env.bullseye_src]))
        if not result.passed:
            logger.info(
                "Bullseye code coverage collection not configured on %s", result.failed_hosts)
            self.__hosts = None
        else:
            logger.info("Bullseye code coverage collection configured on %s", hosts)
            self.__hosts = hosts

    def setup(self, logger, result):
        """Set up the hosts for bullseye code coverage collection.

        Args:
            logger (Logger): logger for the messages produced by this method
            result (TestResult): the test result used to update the status of the runner

        Returns:
            bool: False if there is a problem setting up bullseye code coverage; True otherwise

        """
        if not self.__hosts:
            return True

        logger.debug("-" * 80)
        logger.info("Setting up bullseye code coverage on %s:", self.__hosts)

        logger.debug("Removing any existing %s file", self.__test_env.bullseye_file)
        command = ["rm", "-fr", self.__test_env.bullseye_file]
        if not run_remote(logger, self.__hosts, " ".join(command)).passed:
            message = "Error removing bullseye code coverage file on at least one host"
            result.fail_test(logger, "Run", message, None)
            return False

        logger.debug("Copying %s bullseye code coverage source file", self.__test_env.bullseye_src)
        command = ["cp", self.__test_env.bullseye_src, self.__test_env.bullseye_file]
        if not run_remote(logger, self.__hosts, " ".join(command)).passed:
            message = "Error copying bullseye code coverage file on at least one host"
            result.fail_test(logger, "Run", message, None)
            return False

        logger.debug(
            "Updating %s bullseye code coverage file permissions", self.__test_env.bullseye_file)
        command = ["chmod", "777", self.__test_env.bullseye_file]
        if not run_remote(logger, self.__hosts, " ".join(command)).passed:
            message = "Error updating bullseye code coverage file on at least one host"
            result.fail_test(logger, "Run", message, None)
            return False

        return True

    def finalize(self, logger, job_results_dir, result):
        """Retrieve the bullseye code coverage collection information from the hosts.

        Args:
            logger (Logger): logger for the messages produced by this method
            job_results_dir (str): avocado job-results directory
            result (TestResult): the test result used to update the status of the runner

        Returns:
            bool: False if there is a problem retrieving bullseye code coverage; True otherwise

        """
        if not self.__hosts:
            return True

        logger.debug("-" * 80)
        logger.debug("Collecting bullseye code coverage information on %s:", self.__hosts)

        bullseye_path, bullseye_file = os.path.split(self.__test_env.bullseye_file)
        bullseye_dir = os.path.join(job_results_dir, "bullseye_coverage_logs")
        status = archive_files(
            logger, "bullseye coverage log files", self.__hosts, bullseye_path,
            "".join([bullseye_file, "*"]), bullseye_dir, 1, None, 900, result, None, False)
        if status != 0:
            message = "Error archiving bullseye code coverage files"
            result.fail_test(logger, "Run", message, None)
            return False

        # Merge the bullseye_coverage_logs.*/test.cov into one bullseye_coverage_logs/test.cov
        logger.debug("Merging bullseye code coverage files")
        os.makedirs(bullseye_dir, exist_ok=True)
        command = ["covmerge", "--no-banner", "--file"]
        command.append(os.path.join(bullseye_dir, bullseye_file))
        command.append(os.path.join(".".join([bullseye_dir, '*']), bullseye_file))
        try:
            run_local(logger, " ".join(command), check=True)
        except RunException:
            message = "Error merging bullseye code coverage files"
            result.fail_test(logger, "Run", message, None)
            return False

        return True
