#!/usr/bin/python3
"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithoutServers
from command_utils import SubProcessCommand
from job_manager_utils import Orterun, Mpirun
from exception_utils import CommandFailure


class HarnessBasicTest(TestWithoutServers):
    """Very basic harness test cases.

    :avocado: recursive
    """

    def test_always_passes(self):
        """Simple test of apricot test code.

        :avocado: tags=all
        :avocado: tags=harness,harness_basic_test,test_always_passes
        :avocado: tags=always_passes
        """
        self.log.info("NOOP test to do nothing but run successfully")

    def test_always_passes_hw(self):
        """Simple test of apricot test code.

        :avocado: tags=all
        :avocado: tags=hw,large,medium,ib2,small
        :avocado: tags=harness,harness_basic_test,test_always_passes_hw
        :avocado: tags=always_passes
        """
        self.test_always_passes()

    def test_load_mpi(self):
        """Simple test of apricot test code to load the openmpi module.

        :avocado: tags=all
        :avocado: tags=harness,harness_basic_test,test_load_mpi
        :avocado: tags=load_mpi
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
        :avocado: tags=hw,large,medium,ib2,small
        :avocado: tags=harness,harness_basic_test,test_load_mpi
        :avocado: tags=load_mpi
        """
        self.test_load_mpi()

    def test_sub_process_command(self):
        """Simple test of the SubProcessCommand object.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,harness_basic_test,sub_process_command
        :avocado: tags=test_sub_process_command
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
