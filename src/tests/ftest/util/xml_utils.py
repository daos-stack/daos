"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time

from avocado.plugins.xunit import XUnitResult


class TestResult():
    """Provides the necessary test result data to generate a xml file."""

    PASS = "PASS"
    WARN = "WARN"
    SKIP = "SKIP"
    FAIL = "FAIL"
    ERROR = "ERROR"
    CANCEL = "CANCEL"
    INTERRUPT = "INTERRUPT"

    def __init__(self, class_name, name, log_file):
        """Initialize a TestResult object.

        Args:
            class_name (str): the test class name, e.g. FTEST_<name>
            name (str): the test name
            log_file (str): the log file for a single test
        """
        self.class_name = class_name
        self.name = name
        self.logfile = log_file
        self.time_start = -1
        self.time_end = -1
        self.time_elapsed = -1
        self.status = None
        self.fail_class = None
        self.fail_reason = None
        self.traceback = None

    def get(self, name, default=None):
        """Get the value of the attribute name.

        Args:
            name (str): TimedResult attribute name to get
            default (object, optional): value to return if name is not defined. Defaults to None.

        Returns:
            object: the attribute value or default if not defined

        """
        return getattr(self, name, default)

    def start(self):
        """Mark the start of the test."""
        if self.time_start == -1:
            self.time_start = time.time()

    def end(self):
        """Mark the end of the test."""
        self.time_end = time.time()
        self.time_elapsed = self.time_end - self.time_start


class Results():
    # pylint: disable=too-few-public-methods
    """Provides the necessary result data to generate a xml file."""

    def __init__(self, log_file):
        """Initialize a Results object.

        Args:
            log_file (str): the log file location for all of the tests
        """
        self.logfile = log_file
        self.tests = []

    @property
    def tests_total(self):
        """Get the total number of tests.

        Returns:
            int: total number of tests
        """
        return len(self.tests)

    @property
    def tests_total_time(self):
        """Get the total test time.

        Returns:
            int: total duration of all the tests
        """
        return sum(test.time_elapsed for test in self.tests)

    @property
    def errors(self):
        """Get the total test time.

        Returns:
            int: total duration of all the tests
        """
        test_status = [str(test.status) for test in self.tests]
        return test_status.count(TestResult.ERROR)

    @property
    def interrupted(self):
        """Get the total test time.

        Returns:
            int: total duration of all the tests
        """
        test_status = [str(test.status) for test in self.tests]
        return test_status.count(TestResult.INTERRUPT)

    @property
    def failed(self):
        """Get the total test time.

        Returns:
            int: total duration of all the tests
        """
        test_status = [str(test.status) for test in self.tests]
        return test_status.count(TestResult.FAIL)

    @property
    def skipped(self):
        """Get the total test time.

        Returns:
            int: total duration of all the tests
        """
        test_status = [str(test.status) for test in self.tests]
        return test_status.count(TestResult.SKIP)

    @property
    def cancelled(self):
        """Get the total test time.

        Returns:
            int: total duration of all the tests
        """
        test_status = [str(test.status) for test in self.tests]
        return test_status.count(TestResult.CANCEL)


class TestJob():
    # pylint: disable=too-few-public-methods
    """Provides the necessary job data to generate a xml file."""

    def __init__(self, name, enabled=None, output=None, max_chars=100000):
        """Initialize a DaosTestJob object.

        Args:
            name (str): name of the daos_test subtest
            enabled (str): if set to 'on' results will be written to {job_dir}/results.xml. Defaults
                to None.
            log_dir (str): if specified the results will also be written to this xml file.  Used for
                writing xml files with different names and/or file locations. Defaults to None.
        """
        self.config = {
            "job.run.result.xunit.enabled": enabled,
            "job.run.result.xunit.output": output,
            "job.run.result.xunit.max_test_log_chars": max_chars,
            "job.run.result.xunit.job_name": name,
        }
        self.logdir = None


def create_xml(job, result):
    """Create a xml file for the specified test results.

    Location and filename of the xml file is controlled by the job argument.

    Args:
        job (TestJob): information about the job producing the results
        result (Results): the test results to include in the xml file
    """
    result_xml = XUnitResult()
    result_xml.render(result, job)
