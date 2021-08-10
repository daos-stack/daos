#!/usr/bin/python3
"""
    (C) Copyright 2020-2021 Intel Corporation.

    SPDX-License-Identifier: BSD-2-Clause-Patent

"""
import re
import os

from apricot import TestWithServers
from mpio_utils import MpioUtils, MpioFailed
from daos_utils import DaosCommand
from env_modules import load_mpi


class MpiioTests(TestWithServers):
    """Run ROMIO, LLNL, MPI4PY and HDF5 test suites.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TestWithServers object."""
        super().__init__(*args, **kwargs)
        self.hostfile_clients_slots = None
        self.mpio = None
        self.daos_cmd = None
        self.cont_uuid = None

    def setUp(self):
        """Initialization function for MpiioTests."""
        super().setUp()

        # initialize daos_cmd
        self.daos_cmd = DaosCommand(self.bin)

        self.add_pool(connect=False)

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
            r"created\s+container\s+([0-9a-f-]+)", result.stdout_text)
        if not cont_uuid:
            self.fail(
                "Error obtaining the container uuid from: {}".format(
                    result.stdout_text))
        self.cont_uuid = cont_uuid[0]

    def run_test(self, test_repo, test_name):
        """Execute function to be used by test functions below.

        test_repo       --absolute or relative (to self.mpichinstall) location
                          of test repository
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

        # fix up a relative test_repo specification
        if test_repo[0] != '/':
            test_repo = os.path.join(self.mpio.mpichinstall, test_repo)

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
        for output in (result.stdout_text, result.stderr_text):
            match = re.findall(
                r"(non-zero exit code|MPI_Abort|MPI_ABORT|ERROR)", output)
            if match:
                self.log.info(
                    "The following error messages have been detected in the %s "
                    "output:", test_name)
                for item in match:
                    self.log.info("  %s", item)
                self.fail(
                    "Error messages detected in {} output".format(test_name))
