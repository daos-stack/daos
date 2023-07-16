"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
import os

from collection_utils import archive_files
from run_utils import run_remote


class CodeCoverage():
    """Test bullseye code coverage class."""

    def __init__(self, test_env):
        """Initialize a CodeCoverage object.

        Args:
            test_env (TestEnvironment): the current test environment.
        """
        self.__hosts = None
        self.__test_env = test_env

    def check(self, hosts):
        """Determine if bullseye code coverage collection is enabled.

        Args:
            hosts (NodeSet): hosts on which to check for bullseye code coverage source files
        """
        log = getLogger()
        log.debug("Checking for bullseye code coverage configuration")
        result = run_remote(hosts, " ".join(["ls", "-al", self.__test_env.bullseye_src]))
        if not result.passed:
            log.info("Bullseye code coverage collection not configured on %s", result.failed_hosts)
            self.__hosts = None
        else:
            log.info("Bullseye code coverage collection configured on %s", hosts)
            self.__hosts = hosts

    def setup(self, result):
        """Set up the hosts for bullseye code coverage collection.

        Args:
            result (TestResult): the test result used to update the status of the runner

        Returns:
            bool: False if there is a problem setting up bullseye code coverage; True otherwise

        """
        if self.__hosts:
            log = getLogger()
            log.debug("-" * 80)
            log.info("Setting up bullseye code coverage on %s:", self.__hosts)

            log.debug("Removing any existing %s file", self.__test_env.bullseye_file)
            command = ["rm", "-fr", self.__test_env.bullseye_file]
            if not run_remote(self.__hosts, " ".join(command)).passed:
                message = "Error removing bullseye code coverage file on at least one host"
                result.fail_test("Run", message, None)
                return False

            log.debug("Copying %s bullseye code coverage source file", self.__test_env.bullseye_src)
            command = ["cp", self.__test_env.bullseye_src, self.__test_env.bullseye_file]
            if not run_remote(self.__hosts, " ".join(command)).passed:
                message = "Error copying bullseye code coverage file on at least one host"
                result.fail_test("Run", message, None)
                return False

            log.debug(
                "Updating %s bullseye code coverage file permissions",
                self.__test_env.bullseye_file)
            command = ["chmod", "777", self.__test_env.bullseye_file]
            if not run_remote(self.__hosts, " ".join(command)).passed:
                message = "Error updating bullseye code coverage file on at least one host"
                result.fail_test("Run", message, None)
                return False
        return True

    def finalize(self, job_results_dir, result):
        """Retrieve the bullseye code coverage collection information from the hosts.

        Args:
            job_results_dir (str): avocado job-results directory
            result (TestResult): the test result used to update the status of the runner

        Returns:
            bool: False if there is a problem retrieving bullseye code coverage; True otherwise

        """
        if not self.__hosts:
            return True

        log = getLogger()
        bullseye_path, bullseye_file = os.path.split(self.__test_env.bullseye_file)
        bullseye_dir = os.path.join(job_results_dir, "bullseye_coverage_logs")
        status = archive_files(
            "bullseye coverage log files", self.__hosts, bullseye_path,
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
                            log.debug("Renaming %s to %s", old_file, new_file)
                            os.rename(old_file, new_file)
        return status == 0
