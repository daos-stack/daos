#!/usr/bin/python3
"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithoutServers
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
            Mpirun(None, mpitype="mpich")
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
