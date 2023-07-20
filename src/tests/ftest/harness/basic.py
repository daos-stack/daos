"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithoutServers
from cmocka_utils import CmockaUtils
from command_utils import SubProcessCommand
from exception_utils import CommandFailure
from job_manager_utils import Orterun, Mpirun


class HarnessBasicTest(TestWithoutServers):
    """Very basic harness test cases.

    :avocado: recursive
    """

    def test_always_fails(self):
        """Simple test of apricot test code.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,harness_basic_test
        :avocado: tags=HarnessBasicTest,always_fails,test_always_fails
        """
        self.fail("NOOP test to do nothing but fail")

    def test_always_fails_hw(self):
        """Simple test of apricot test code.

        :avocado: tags=all
        :avocado: tags=hw,large,medium,small
        :avocado: tags=harness,harness_basic_test
        :avocado: tags=HarnessBasicTest,always_fails,test_always_fails_hw
        """
        self.test_always_fails()

    def test_always_passes(self):
        """Simple test of apricot test code.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,harness_basic_test,always_passes
        :avocado: tags=HarnessBasicTest,test_always_passes
        """
        self.log.info("NOOP test to do nothing but run successfully")

    def test_always_passes_hw(self):
        """Simple test of apricot test code.

        :avocado: tags=all
        :avocado: tags=hw,medium,large
        :avocado: tags=harness,harness_basic_test,always_passes
        :avocado: tags=HarnessBasicTest,test_always_passes_hw
        """
        self.test_always_passes()

    def test_always_passes_hw_provider(self):
        """Simple test of apricot test code.

        :avocado: tags=all
        :avocado: tags=hw,medium,large,provider
        :avocado: tags=harness,harness_basic_test,always_passes
        :avocado: tags=HarnessBasicTest,test_always_passes_hw_provider
        """
        self.test_always_passes()

    def test_load_mpi(self):
        """Simple test of apricot test code to load the openmpi module.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,harness_basic_test,load_mpi
        :avocado: tags=HarnessBasicTest,test_load_mpi
        """
        try:
            Orterun(None)
        except CommandFailure as error:
            self.fail("Orterun initialization failed: {}".format(error))

        try:
            Mpirun(None, mpi_type="mpich")
        except CommandFailure as error:
            self.fail("Mpirun initialization failed: {}".format(error))

    def test_load_mpi_hw(self):
        """Simple test of apricot test code to load the openmpi module.

        :avocado: tags=all
        :avocado: tags=hw,medium,large
        :avocado: tags=harness,harness_basic_test,load_mpi
        :avocado: tags=HarnessBasicTest,test_load_mpi_hw
        """
        self.test_load_mpi()

    def test_sub_process_command(self):
        """Simple test of the SubProcessCommand object.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,harness_basic_test,sub_process_command
        :avocado: tags=HarnessBasicTest,test_sub_process_command
        """
        failed = False
        test_command = ["ls", "-al", os.path.dirname(__file__)]
        command = SubProcessCommand("/run/command/*", " ".join(test_command))
        for sub_process in (False, True):
            for check in ("both", "combined"):
                self.log.info("-" * 80)
                self.log.info(
                    "Running '%s' with output_check='%s' and run_as_subprocess=%s",
                    str(command), check, sub_process)
                command.output_check = check
                command.run_as_subprocess = sub_process
                try:
                    command.run()
                except CommandFailure:
                    self.log.error("The '%s' command failed", str(command), exc_info=True)
                    failed = True
                finally:
                    command.stop()
        if failed:
            self.fail("The '{}' command failed".format(command))
        self.log.info("Test passed")

    def test_no_cmocka_xml(self):
        """Test to verify CmockaUtils detects lack of cmocka file generation.

        If working correctly this test should fail due to a missing cmocka file.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,harness_cmocka,failure_expected
        :avocado: tags=HarnessBasicTest,test_no_cmocka_xml
        """
        self.log.info("=" * 80)
        self.log.info("Running the 'hostname' command via CmockaUtils")
        self.log.info("  This should generate a cmocka xml file with a 'Missing file' error")
        name = "no_cmocka_xml_file_test"
        cmocka_utils = CmockaUtils(None, name, self.outputdir, self.test_dir, self.log)
        command = cmocka_utils.get_cmocka_command("hostname")
        cmocka_utils.run_cmocka_test(self, command)

        # Verify a generated cmocka xml file exists
        expected = os.path.join(self.outputdir, "{}_cmocka_results.xml".format(name))
        self.log.info("Verifying the existence of the generated cmocka file: %s", expected)
        if not os.path.isfile(expected):
            self.fail("No {} file found".format(expected))

        # Verify the generated cmocka xml file contains the expected error
        self.log.info("Verifying contents of the generated cmocka file: %s", expected)
        with open(expected, "r", encoding="utf-8") as file_handle:
            actual_contents = file_handle.readlines()
        error_message = "Missing cmocka results for hostname in {}".format(self.outputdir)
        expected_lines = [
            "<testsuite errors=\"1\" failures=\"0\" name=\"{}\" skipped=\"0\" tests=\"1\"".format(
                name),
            "<testcase classname=\"{}\" name=\"{}\"".format(name, self.name),
            "<error message=\"{}\" type=\"Missing file\">".format(error_message)
        ]
        for index, actual_line in enumerate(actual_contents[1:4]):
            self.log.debug("  expecting: %s", expected_lines[index])
            self.log.debug("  in actual: %s", actual_line[:-1].strip())
            if expected_lines[index] not in actual_line:
                self.fail("Badly formed {} file".format(expected))

        self.log.info("Test passed")
