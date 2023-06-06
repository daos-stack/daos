"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time
from argparse import Namespace


class TestName():
    # pylint: disable=too-few-public-methods
    """Provides the necessary test name data to generate a xml/html results file."""

    def __init__(self, name, uid, variant):
        """Initialize a TestName object.

        Args:
            name (str): the test name
            uid (str): the test uid
            variant (str): the test variant
        """
        self.name = name
        self.uid = uid
        self.variant = variant

    def __str__(self):
        """Get the test name as a string.

        Returns:
            str: the test name including the uid, name, and variant

        """
        return "{}-{}{}".format(self.uid, self.name, self.variant)


class TestResult():
    """Provides the necessary test result data to generate a xml/html results file."""

    PASS = "PASS"
    WARN = "WARN"
    SKIP = "SKIP"
    FAIL = "FAIL"
    ERROR = "ERROR"
    CANCEL = "CANCEL"
    INTERRUPT = "INTERRUPT"

    def __init__(self, class_name, name, log_file, log_dir):
        """Initialize a TestResult object.

        Args:
            class_name (str): the test class name, e.g. FTEST_<name>
            name (TestName): the test uid, name, and variant
            log_file (str): the log file for a single test
            log_dir (str): the log file directory for a single test
        """
        self.class_name = class_name
        self.name = name
        self.logfile = log_file
        self.logdir = log_dir
        self._time_split = 0
        self.time_start = -1
        self.time_end = -1
        self.time_elapsed = -1
        self.status = None
        self.fail_class = None
        self.fail_reason = None
        self.fail_count = 0
        self.traceback = None

    def __getitem__(self, name, default=None):
        """Get the value of the attribute name.

        Args:
            name (str): name of the class attribute to get
            default (object, optional): value to return if name is not defined. Defaults to None.

        Returns:
            object: the attribute value or default if not defined

        """
        return self.get(name, default)

    def __contains__(self, item):
        """Determine if the attribute exists in this class, e.g. 'item' in self.

        This is used when creating the html test result.

        Args:
            item (str): attribute name

        Returns:
            bool: True if the attribute name has a value in this object; False otherwise

        """
        try:
            getattr(self, item)
        except (AttributeError, TypeError):
            return False
        return True

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

    def start(self):
        """Mark the start of the test."""
        self._time_split = time.time()
        if self.time_start == -1:
            # Set the start time and initialize the elapsed time if this is the first start call
            self.time_start = self._time_split
            self.time_elapsed = 0

    def end(self):
        """Mark the end of the test."""
        self.time_end = time.time()
        # Increase the elapsed time by the delta between the last start call and this end call
        self.time_elapsed += self.time_end - self._time_split


class Results():
    # pylint: disable=too-few-public-methods
    """Provides the necessary result data to generate a xml/html results file."""

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
    def passed(self):
        """Get the total number of passed tests.

        Returns:
            int: total number of passed tests
        """
        test_status = [str(test.status) for test in self.tests]
        return test_status.count(TestResult.PASS)

    @property
    def warned(self):
        """Get the total number of warned tests.

        Returns:
            int: total number of warned tests
        """
        test_status = [str(test.status) for test in self.tests]
        return test_status.count(TestResult.WARN)

    @property
    def errors(self):
        """Get the total number of tests with errors.

        Returns:
            int: total number of tests with errors

        """
        test_status = [str(test.status) for test in self.tests]
        return test_status.count(TestResult.ERROR)

    @property
    def interrupted(self):
        """Get the total number of interrupted tests.

        Returns:
            int: total number of interrupted tests

        """
        test_status = [str(test.status) for test in self.tests]
        return test_status.count(TestResult.INTERRUPT)

    @property
    def failed(self):
        """Get the total number of failed tests.

        Returns:
            int: total number of failed tests

        """
        test_status = [str(test.status) for test in self.tests]
        return test_status.count(TestResult.FAIL)

    @property
    def skipped(self):
        """Get the total number of skipped tests.

        Returns:
            int: total number of skipped tests

        """
        test_status = [str(test.status) for test in self.tests]
        return test_status.count(TestResult.SKIP)

    @property
    def cancelled(self):
        """Get the total number of cancelled tests.

        Returns:
            int: total number of cancelled tests

        """
        test_status = [str(test.status) for test in self.tests]
        return test_status.count(TestResult.CANCEL)

    @property
    def completed(self):
        """Get the total number of completed tests.

        Returns:
            int: total number of tests that were not skipped or cancelled

        """
        return self.tests_total - self.skipped - self.cancelled

    @property
    def succeeded(self):
        """Get the total number of tests that succeeded.

        Returns:
            int: total number of tests that passed or warned

        """
        return self.passed + self.warned

    @property
    def rate(self):
        """Get the pass rate for the tests.

        Returns:
            float: pass rate of all the tests

        """
        if not self.completed:
            return 0.0
        return 100 * (float(self.succeeded) / float(self.completed))


class Job():
    # pylint: disable=too-few-public-methods
    """Provides the necessary job data to generate a xml/html results file."""

    def __init__(self, name, xml_enabled=None, xml_output=None, html_enabled=None,
                 html_output=None, log_dir=None, max_chars=100000):
        """Initialize a DaosTestJob object.

        Args:
            name (str): name of the daos_test subtest
            xml_enabled (str, optional): if set to 'on' results will be written to
                {logdir}/results.xml. Defaults to None.
            xml_output (str, optional): optional full path of an xml file to generate.  Used for
                writing xml files with different names and/or file locations. Defaults to None.
            html_enabled (str, optional): if set to 'on' results will be written to
                {logdir}/results.html. Defaults to None.
            html_output (str, optional): optional full path of an html file to generate.  Used
                for writing html files with different names and/or file locations. Defaults to None.
            log_dir (str, optional): directory in which to write the results.[xml|html] if
                [xml|html]_enabled is set to 'on'. Defaults to None.
            max_chars (int, optional): maximum number of characters of each test log to include in
                with any test errors. Defaults to 100000.
        """
        # For newer avocado versions
        self.config = {
            "job.run.result.xunit.enabled": xml_enabled,
            "job.run.result.xunit.output": xml_output,
            "job.run.result.xunit.max_test_log_chars": max_chars,
            "job.run.result.xunit.job_name": name,
            "job.run.result.html.enabled": html_enabled,
            "job.run.result.html.open_browser": False,
            "job.run.result.html.output": html_output,
            "stdout_claimed_by": None,
        }
        self.logdir = log_dir

        # For older avocado versions
        self.args = Namespace(
            xunit_job_result=('on' if xml_enabled else 'off'),
            xunit_output=xml_output,
            xunit_max_test_log_chars=max_chars,
            xunit_job_name=name,
            html_job_result=('on' if html_enabled else 'off'),
            html_output=html_output,
            open_browser=False,
            stdout_claimed_by=None)

        # If set to either 'RUNNING', 'ERROR', or 'FAIL' an html result will not be generated
        self.status = "COMPLETE"


def sanitize_results(results):
    """Ensure each test status is set and all failure attributes are set for test failures.

    Args:
        results (Results): the test results to sanitize

    Returns:
        Results: sanitized test results
    """
    for test in results.tests:
        if not test.status or test.status == TestResult.FAIL:
            test.status = TestResult.FAIL
            if not test.fail_class:
                test.fail_class = 'Missing fail class'
            if not test.fail_reason:
                test.fail_reason = 'Missing fail reason'
    return results


def create_xml(job, results):
    """Create a xml file for the specified test results.

    Location and filename of the xml file is controlled by the job argument.

    Args:
        job (Job): information about the job producing the results
        results (Results): the test results to include in the xml file
    """
    # When SRE-439 is fixed we should be able to move these imports to the top of the file
    # pylint: disable=import-outside-toplevel
    from avocado.plugins.xunit import XUnitResult
    result_xml = XUnitResult()
    result_xml.render(sanitize_results(results), job)


def create_html(job, results):
    """Create a html file for the specified test results.

    Location and filename of the html file is controlled by the job argument.

    Args:
        job (Job): information about the job producing the results
        results (Results): the test results to include in the html file
    """
    # When SRE-439 is fixed we should be able to move these imports to the top of the file
    # pylint: disable=import-outside-toplevel
    from avocado_result_html import HTMLResult
    result_html = HTMLResult()
    result_html.render(sanitize_results(results), job)
