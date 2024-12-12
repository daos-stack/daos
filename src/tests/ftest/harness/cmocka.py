"""
  (C) Copyright 2022-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithoutServers
from cmocka_utils import CmockaUtils, get_cmocka_command
from host_utils import get_local_host
from job_manager_utils import get_job_manager


class HarnessCmockaTest(TestWithoutServers):
    """Cmocka harness test cases.

    :avocado: recursive
    """

    def test_no_cmocka_xml(self):
        """Test to verify CmockaUtils detects lack of cmocka file generation.

        If working correctly this test should fail due to a missing cmocka file.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,failure_expected
        :avocado: tags=HarnessCmockaTest,test_no_cmocka_xml
        """
        self._run_cmocka_test(get_cmocka_command("hostname"), False, True)
        self.log.info("Test passed")

    def test_clush_manager_timeout(self):
        """Test to verify CmockaUtils handles timed out process correctly.

        If working correctly this test should fail due to a test timeout and a missing cmocka file.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,failure_expected
        :avocado: tags=HarnessCmockaTest,test_clush_manager_timeout
        """
        self._run_cmocka_test(self._get_manager_command("Clush", "sleep", "60"), True, True)
        self.fail("Test did not timeout")

    def test_orterun_manager_timeout(self):
        """Test to verify CmockaUtils handles timed out process correctly.

        If working correctly this test should fail due to a test timeout and a missing cmocka file.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,failure_expected
        :avocado: tags=HarnessCmockaTest,test_orterun_manager_timeout
        """
        self._run_cmocka_test(self._get_manager_command("Orterun", "sleep", "60"), True, True)
        self.fail("Test did not timeout")

    def test_mpirun_manager_timeout(self):
        """Test to verify CmockaUtils handles timed out process correctly.

        If working correctly this test should fail due to a test timeout and a missing cmocka file.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,failure_expected
        :avocado: tags=HarnessCmockaTest,test_mpirun_manager_timeout
        """
        self._run_cmocka_test(self._get_manager_command("Mpirun", "sleep", "60"), True, True)
        self.fail("Test did not timeout")

    def _run_cmocka_test(self, command, timeout, missing):
        """Run the cmocka test case.

        Args:
            command (ExecutableCommand): the command to run
            timeout (bool): is the test expected to timeout
            missing (bool): is the test expected to be missing a cmocka result
        """
        self.log.info("Running the '%s' command via CmockaUtils", str(command))
        if timeout:
            self.log.info("  This should generate a test timeout failure")
        if missing:
            self.log.info("  This should generate a cmocka xml file with a 'Missing file' error")

        cmocka_utils = CmockaUtils(None, self.test_id, self.outputdir, self.test_dir, self.log)
        try:
            cmocka_utils.run_cmocka_test(self, command)
        finally:
            self._verify_no_cmocka_xml(self.test_id, command)

    def _get_manager_command(self, class_name, executable, parameters):
        """Get a JobManager command object.

        Args:
            class_name (str): JobManager class name
            executable (str): executable to be managed
            parameters (str): parameters for the executable to be managed

        Returns:
            JobManager: the requested JobManager class
        """
        command = get_cmocka_command(executable, parameters)
        manager = get_job_manager(self, class_name, command)
        manager.assign_hosts(get_local_host())
        return manager

    def _verify_no_cmocka_xml(self, name, command):
        """Verify a cmocka xml file was generated with the expected error.

        Args:
            name (str): name of the cmocka test
            command (ExecutableCommand): command for the cmocka test
        """
        # Verify a generated cmocka xml file exists
        expected = os.path.join(self.outputdir, f"{name}_cmocka_results.xml")
        self.log.info("Verifying the existence of the generated cmocka file: %s", expected)
        if not os.path.isfile(expected):
            self.fail(f"No {expected} file found")

        # Verify the generated cmocka xml file contains the expected error
        self.log.info("Verifying contents of the generated cmocka file: %s", expected)
        with open(expected, "r", encoding="utf-8") as file_handle:
            actual_contents = file_handle.readlines()
        if hasattr(command, "job"):
            error_message = f"Missing cmocka results for {str(command.job)} in {self.outputdir}"
        else:
            error_message = f"Missing cmocka results for {str(command)} in {self.outputdir}"
        expected_lines = [
            f"<testsuite errors=\"1\" failures=\"0\" name=\"{name}\" skipped=\"0\" tests=\"1\"",
            f"<testcase classname=\"{name}\" name=\"{self.name}\"",
            f"<error message=\"{error_message}\" type=\"Missing file\">"
        ]
        for index, actual_line in enumerate(actual_contents[1:4]):
            self.log.debug("  expecting: %s", expected_lines[index])
            self.log.debug("  in actual: %s", actual_line[:-1].strip())
            if expected_lines[index] not in actual_line:
                self.fail(f"Badly formed {expected} file")
