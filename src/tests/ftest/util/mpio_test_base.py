#!/usr/bin/python
'''
    (C) Copyright 2020 Intel Corporation.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

    GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
    The Government's rights to use, modify, reproduce, release, perform,
    display, or disclose this software are subject to the terms of the Apache
    License as provided in Contract No. B609815.
    Any reproduction of computer software, computer software documentation, or
    portions thereof marked with this legend must also reproduce the markings.
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
            self.mpio.run_mpiio_tests(
                self.hostfile_clients, self.pool.uuid, self.pool.svc_ranks,
                test_repo, test_name, client_processes, self.cont_uuid)
        except MpioFailed as excep:
            self.fail("<{0} Test Failed> \n{1}".format(test_name, excep))

        # Parsing output to look for failures
        # stderr directed to stdout
        stdout = os.path.join(self.logdir, "stdout")
        searchfile = open(stdout, "r")
        error_message = ["non-zero exit code", "MPI_Abort", "MPI_ABORT",
                         "ERROR"]

        for line in searchfile:
            for error in error_message:
                if error in line:
                    self.fail(
                        "Test Failed with error_message: {}".format(error))
