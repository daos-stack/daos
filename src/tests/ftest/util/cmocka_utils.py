"""
(C) Copyright 2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os

from agent_utils import include_local_host
from command_utils import ExecutableCommand
from command_utils_base import EnvironmentVariables
from exception_utils import CommandFailure
from general_utils import get_clush_command, run_command, run_remote
from xml_utils import TestResult, Results, TestJob, create_xml


class CmockaUtils():
    """Utilities for running test that generate cmocka xml results."""

    def __init__(self, hosts, class_name, test_name, outputdir, test_dir):
        """Initialize a CmockaUtils object.

        Args:
            hosts (str): hosts on which to run the cmocka tests
            class_name (str): class name of the cmocka test, e.g. FTEST_<test_dir>.<test_name>
            test_name (str): name of the cmocka test
            outputdir (str): final location for cmocka xml files on this host
            test_dir (str): directory common to all hosts for storing remote cmocka xml files
        """
        self.hosts = hosts
        self.class_name = class_name
        self.test_name = test_name
        self.outputdir = outputdir
        self._using_local_host = False

        # Use the local host if no hosts have been specified.
        if not self.hosts:
            self.hosts = include_local_host(self.hosts)
            self._using_local_host = True

        self.cmocka_dir = self._get_cmocka_dir(test_dir)

    def _get_cmocka_dir(self, test_dir):
        """Get the directory in which to write cmocka xml results.

        For tests running locally use a directory that will place the cmocka results directly in
        the avocado job-results/*/test-results/*/data/ directory (self.outputdir).

        For tests running remotely use a 'cmocka' subdirectory in the DAOS_TEST_LOG_DIR directory as
        this directory should exist on all hosts.  After the command runs these remote files will
        need to be copied back to this host and placed in the self.outputdir directory in order for
        them to be included as part of the Jenkins test results - see _collect_cmocka_results().

        Args:
            test_dir (str): directory common to all hosts

        Returns:
            str: the cmocka directory to use with the CMOCKA_XML_FILE env

        """
        cmocka_dir = self.outputdir
        if not self._using_local_host:
            cmocka_dir = os.path.join(test_dir, "cmocka")
            command = " ".join(["mkdir", "-p", cmocka_dir])
            run_remote(include_local_host(self.hosts), command)
        return cmocka_dir

    def get_cmocka_env(self):
        """Get the environment variables to use when running the daos_test command.

        Returns:
            EnvironmentVariables: the environment variables for the daos_test command

        """
        return EnvironmentVariables({
            "CMOCKA_XML_FILE": os.path.join(self.cmocka_dir, "%g_cmocka_results.xml"),
            "CMOCKA_MESSAGE_OUTPUT": "xml",
        })

    def get_cmocka_command(self, command):
        """Get an ExecutableCommand representing the provided command string.

        Adds detection of any bad words in the command output that, if found, will result in a
        command failure.

        Args:
            command (str): the command string to use to create the ExecutableCommand

        Returns:
            ExecutableCommand: the object setup to run the command

        """
        bad_words = ["Process received signal", "stack smashing detected", "End of error message"]
        return ExecutableCommand(namespace=None, command=command, check_results=bad_words)

    def run_cmocka_test(self, test, command):
        """Run the daos_test command.

        After the command completes, copy any remote cmocka results that may exist back to this host
        and generate a cmocka result if one is missing or the command failed before generating one.

        Args:
            test (Test): the avocado test class
            command (ExecutableCommand): the command to run
        """
        job_command = command.job if hasattr(command, "job") else command
        error_message = None
        error_exception = None
        try:
            command.run()

        except CommandFailure as error:
            error_message = "Error detected running {}".format(job_command)
            error_exception = error
            test.log.exception(error_message)
            test.fail(error_message)

        finally:
            self._collect_cmocka_results(test)
            if not self._check_cmocka_files():
                if error_message is None:
                    error_message = "Missing cmocka results for {} in {}".format(
                        job_command, self.cmocka_dir)
                self._generate_cmocka_files(test.logfile, error_message, error_exception)

    def _collect_cmocka_results(self, test):
        """Collect any remote cmocka files.

        Args:
            test (Test): the avocado test class
        """
        if self._using_local_host:
            # No remote hosts where used to run the daos_test command, so nothing to do
            return

        # List any remote cmocka files
        test.log.debug("Remote %s directories:", self.cmocka_dir)
        ls_command = "ls -alR {0}".format(self.cmocka_dir)
        clush_ls_command = "{0} {1}".format(
            get_clush_command(self.hosts, "-B -S"), ls_command)
        run_remote(self.hosts, clush_ls_command)

        # Copy any remote cmocka files back to this host
        command = "{0} --rcopy {1} --dest {1}".format(
            get_clush_command(self.hosts), self.cmocka_dir)
        try:
            run_command(command)

        finally:
            test.log.debug("Local %s directory after clush:", self.cmocka_dir)
            run_command(ls_command)
            # Move local files to the avocado test variant data directory
            for cmocka_node_dir in os.listdir(self.cmocka_dir):
                cmocka_node_path = os.path.join(self.cmocka_dir, cmocka_node_dir)
                if os.path.isdir(cmocka_node_path):
                    for cmocka_file in os.listdir(cmocka_node_path):
                        cmocka_file_path = os.path.join(cmocka_node_path, cmocka_file)
                        if "_cmocka_results." in cmocka_file:
                            command = "mv {0} {1}".format(cmocka_file_path, self.outputdir)
                            run_command(command)

    def _check_cmocka_files(self):
        """Determine if cmocka files exist in the expected location.

        Returns:
            bool: True if cmocka files exist in the expected location; False otherwise
        """
        for item in os.listdir(self.outputdir):
            if "cmocka_results.xml" in item:
                return True
        return False

    def _generate_cmocka_files(self, log_file, error_message, error_exception):
        """Generate a cmocka xml file.

        Args:
            error_message (str): a description of the test failure
            error_exception (Exception): the exception raised when the failure occurred
        """
        # Create a failed test result
        test_result = TestResult(self.class_name, self.test_name, log_file)
        test_result.status = TestResult.ERROR
        test_result.fail_class = "Missing file" if error_exception is None else "Failed command"
        test_result.fail_reason = error_message
        test_result.traceback = error_exception
        test_result.time_elapsed = 0

        cmocka_xml = os.path.join(self.outputdir, "{}_cmocka_results.xml".format(self.test_name))
        job = TestJob(self.test_name, output=cmocka_xml)
        result = Results(log_file)
        result.tests.append(test_result)
        create_xml(job, result)
