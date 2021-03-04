#!/usr/bin/python
'''
    (C) Copyright 2020-2021 Intel Corporation.

    SPDX-License-Identifier: BSD-2-Clause-Patent

'''
from __future__    import print_function

import os
import re

from apricot import TestWithServers
from mpio_utils import MpioUtils, MpioFailed
from test_utils_pool import TestPool
from daos_utils import DaosCommand
from env_modules import load_mpi


class MpiioTests(TestWithServers):
    """
    Runs ROMIO, LLNL, MPI4PY and HDF5 test suites.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestWithServers object."""
        super(MpiioTests, self).__init__(*args, **kwargs)
        self.hostfile_clients_slots = None
        self.mpio = None
        self.daos_cmd = None
        self.cont_uuid = None

    def setUp(self):
        super(MpiioTests, self).setUp()

        # initialize daos_cmd
        self.daos_cmd = DaosCommand(self.bin)

        # initialize a python pool object then create the underlying
        self.pool = TestPool(self.context, self.get_dmg_command())
        self.pool.get_params(self)
        self.pool.create()

    def _create_cont(self):
        """Create a container.

        Args:
            daos_cmd (DaosCommand): daos command to issue the container
                create

        Returns:
            str: UUID of the created container

        """
        cont_type = self.params.get("type", "/run/container/*")
        result = self.daos_cmd.container_create(
            pool=self.pool.uuid, cont_type=cont_type)

        # Extract the container UUID from the daos container create output
        cont_uuid = re.findall(
            r"created\s+container\s+([0-9a-f-]+)", result.stdout)
        if not cont_uuid:
            self.fail(
                "Error obtaining the container uuid from: {}".format(
                    result.stdout))
        self.cont_uuid = cont_uuid[0]

    def run_test(self, test_repo, test_name):
        """
        Executable function to be used by test functions below
        test_repo       --location of test repository
        test_name       --name of the test to be run
        """
        # Required to run daos command
        load_mpi("openmpi")

        # create container
        self._create_cont()

        # initialize MpioUtils
        self.mpio = MpioUtils()
        if not self.mpio.mpich_installed(self.hostlist_clients):
            self.fail("Exiting Test: Mpich not installed")

        # initialize test specific variables
        client_processes = self.params.get("np", '/run/client_processes/')

        try:
            # running tests
            result = self.mpio.run_mpiio_tests(
                self.hostfile_clients, self.pool.uuid, test_repo, test_name,
                client_processes, self.cont_uuid)
        except MpioFailed as excep:
            self.fail("<{0} Test Failed> \n{1}".format(test_name, excep))

        # Check output for errors
        error_message = [
            "non-zero exit code", "MPI_Abort", "MPI_ABORT", "ERROR"]
        for output in (result.stdout, result.stderr):
            for line in output:
                for error in error_message:
                    if error in line:
                        self.fail(
                            "Test Failed with error_message: {}".format(error))
