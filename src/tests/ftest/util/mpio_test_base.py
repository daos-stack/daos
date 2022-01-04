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

    def run_test(self, test_repo, test_name):
        """Execute function to be used by test functions below.

        test_repo       --absolute or relative (to self.mpichinstall) location
                          of test repository
        test_name       --name of the test to be run
        """
        # Required to run daos command
        load_mpi("openmpi")

        # Create pool
        self.add_pool(connect=False)

        # create container
        self.add_container(self.pool)

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
                client_processes, self.container.uuid)
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
