"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
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

    def finish_test(self, logger, message, fail_class=None, exc_info=None):
        """Mark the end of the test result with a status.

        Args:
            logger (Logger): logger for the messages produced by this method
            message (str): exit message or reason for failure
            fail_class (str, optional): failure category.
            exc_info (OptExcInfo, optional): return value from sys.exc_info().
        """
        if fail_class is None:
            self.pass_test(logger, message)
        else:
            self.fail_test(logger, fail_class, message, exc_info)
        self.end()

    def pass_test(self, logger, message=None):
        """Set the test result as passed.

        Args:
            logger (Logger): logger for the messages produced by this method
            message (str, optional): explanation of test passing. Defaults to None.
        """
        if message is not None:
            logger.debug(message)
        self.__set_test_status(logger, TestResult.PASS, None, None)

    def warn_test(self, logger, fail_class, fail_reason, exc_info=None):
        """Set the test result as warned.

        Args:
            logger (Logger): logger for the messages produced by this method
            fail_class (str): failure category.
            fail_reason (str): failure description.
            exc_info (OptExcInfo, optional): return value from sys.exc_info(). Defaults to None.
        """
        logger.warning(fail_reason)
        self.__set_test_status(logger, TestResult.WARN, fail_class, fail_reason, exc_info)

    def fail_test(self, logger, fail_class, fail_reason, exc_info=None):
        """Set the test result as failed.

        Args:
            logger (Logger): logger for the messages produced by this method
            fail_class (str): failure category.
            fail_reason (str): failure description.
            exc_info (OptExcInfo, optional): return value from sys.exc_info(). Defaults to None.
        """
        logger.error(fail_reason)
        self.__set_test_status(logger, TestResult.ERROR, fail_class, fail_reason, exc_info)

    def __set_test_status(self, logger, status, fail_class, fail_reason, exc_info=None):
        """Set the test result.

        Args:
            logger (Logger): logger for the messages produced by this method
            status (str): TestResult status to set.
            fail_class (str): failure category.
            fail_reason (str): failure description.
            exc_info (OptExcInfo, optional): return value from sys.exc_info(). Defaults to None.
        """
        if exc_info is not None:
            logger.debug("Stacktrace", exc_info=True)

        if status == TestResult.PASS:
            # Do not override a possible WARN status
            if self.status is None:
                self.status = status
            return

        if self.fail_count == 0 or self.status == TestResult.WARN and status == TestResult.ERROR:
            # Update the test result with the information about the first ERROR.
            # Elevate status from WARN to ERROR if WARN came first.
            self.status = status
            self.fail_class = fail_class
            self.fail_reason = fail_reason
            if exc_info is not None:
                try:
                    # pylint: disable=import-outside-toplevel
                    from avocado.utils.stacktrace import prepare_exc_info
                    self.traceback = prepare_exc_info(exc_info)
                except Exception:       # pylint: disable=broad-except
                    pass

        if self.fail_count > 0:
            # Additional ERROR/WARN only update the test result fail reason with a fail counter
            plural = "s" if self.fail_count > 1 else ""
            fail_reason = self.fail_reason.split(" (+")[0:1]
            fail_reason.append(f"{self.fail_count} other failure{plural})")
            self.fail_reason = " (+".join(fail_reason)

        self.fail_count += 1


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

    def add_test(self, class_name, test_name, log_file):
        """Add a new test result.

        Args:
            class_name (str): the test class name
            test_name (TestName): the test uid, name, and variant
            log_file (str): the log file for a single test

        Returns:
            TestResult: the test result for this test
        """
        self.tests.append(TestResult(class_name, test_name, log_file, self.logfile))
        return self.tests[-1]


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

    def generate_results(self, logger, results):
        """Generate the results.xml and results.html for this job.

        Args:
            logger (Logger): logger for the messages produced by this method
            results (Results): the test results to use to generate the files
        """
        for key, create_method in {"results.xml": create_xml, "results.html": create_html}.items():
            output = os.path.join(self.logdir, key)
            try:
                logger.debug("Creating %s: %s", key, output)
                create_method(self, results)
            except Exception as error:      # pylint: disable=broad-except
                logger.error("Unable to create %s file: %s", key, error)
            else:
                if not os.path.exists(output):
                    logger.error("The %s does not exist: %s", key, output)


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


class LaunchTestName():
    """Define a launch.py test name compatible with avocado's result render classes."""

    def __init__(self, name, order, repeat):
        """Initialize a LaunchTestName object.

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
            LaunchTestName: a copy of this LaunchTestName object
        """
        return LaunchTestName(self.name, self.order, self.repeat)
