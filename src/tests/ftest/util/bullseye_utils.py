"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
import os

from collection_utils import archive_files
from run_utils import run_remote

BULLSEYE_FILE = os.path.join(os.sep, "tmp", "test.cov")
BULLSEYE_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test.cov")


def set_bullseye_environment():
    """Set the bullseye code coverage environment variables."""
    os.environ["COVFILE"] = BULLSEYE_FILE


def setup_bullseye(hosts, runner_result):
    """Set up the hosts for bullseye code coverage collection.

    Args:
        hosts (NodeSet): hosts on which to setup bullseye code coverage files
        runner_result (TestResult): the test result used to update the status of the runner

    Returns:
        bool: False if there is a problem setting up bullseye code coverage files; True otherwise

    """
    if hosts:
        log = getLogger()
        log.debug("-" * 80)
        log.info("Setting up bullseye code coverage on %s:", hosts)

        log.debug("Removing any existing %s file", BULLSEYE_FILE)
        command = ["rm", "-fr", BULLSEYE_FILE]
        if not run_remote(hosts, " ".join(command)).passed:
            message = "Error removing bullseye code coverage file on at least one host"
            runner_result.fail_test("Run", message, None)
            return False

        log.debug("Copying %s bullseye code coverage source file", BULLSEYE_SRC)
        command = ["cp", BULLSEYE_SRC, BULLSEYE_FILE]
        if not run_remote(hosts, " ".join(command)).passed:
            message = "Error copying bullseye code coverage file on at least one host"
            runner_result.fail_test("Run", message, None)
            return False

        log.debug("Updating %s bullseye code coverage file permissions", BULLSEYE_FILE)
        command = ["chmod", "777", BULLSEYE_FILE]
        if not run_remote(hosts, " ".join(command)).passed:
            message = "Error updating bullseye code coverage file on at least one host"
            runner_result.fail_test("Run", message, None)
            return False
    return True


def finalize_bullseye(hosts, job_results_dir, runner_result):
    """Retrieve the bullseye code coverage collection information from the hosts.

    Args:
        hosts (NodeSet): hosts on which to setup bullseye code coverage files
        runner_result (TestResult): the test result used to update the status of the runner

    Returns:
        bool: False if there is a problem retrieving bullseye code coverage files; True otherwise

    """
    if not hosts:
        return True

    log = getLogger()
    bullseye_path, bullseye_file = os.path.split(BULLSEYE_FILE)
    bullseye_dir = os.path.join(job_results_dir, "bullseye_coverage_logs")
    status = archive_files(
        "bullseye coverage log files", hosts, bullseye_path, "".join([bullseye_file, "*"]),
        bullseye_dir, 1, None, 900, runner_result)

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
