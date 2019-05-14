#!/usr/bin/python
'''
    (C) Copyright 2019 Intel Corporation.

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
import sys
import json
from apricot import TestWithServers

import write_host_file
from mpio_utils import MpioUtils, MpioFailed
from daos_api import DaosPool, DaosApiError

class LlnlMpi4pyHdf5(TestWithServers):
    """
    Runs LLNL, MPI4PY and HDF5 test suites.
    :avocado: recursive
    """

    def setUp(self):
        super(LlnlMpi4pyHdf5, self).setUp()
        # initialising variables
        self.mpio = None
        self.hostlist_clients = None
        self.hostfile_clients = None

        # setting client variables
        self.hostlist_clients = self.params.get("test_clients", '/run/hosts/')
        self.hostfile_clients = write_host_file.write_host_file(
            self.hostlist_clients, self.workdir, None)
        try:
            # parameters used in pool create
            createmode = self.params.get("mode", '/run/pool/createmode/*/')
            createuid = os.geteuid()
            creategid = os.getegid()
            createsetid = self.params.get("setname", '/run/pool/createset/')
            createsize = self.params.get("size", '/run/pool/createsize/')
            self.createsvc = self.params.get("svcn", '/run/pool/createsvc/')

            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None, None,
                             self.createsvc)
        except (DaosApiError) as excep:
            self.fail("<Test Failed at pool create> \n{1}".format(excep))

    def run_test(self, test_repo, test_name):
        """
        Executable function to be used by test functions below
        test_repo       --location of test repository
        test_name       --name of the test to be run
        """
        # initialize MpioUtils
        self.mpio = MpioUtils()
        if not self.mpio.mpich_installed(self.hostlist_clients):
            self.fail("Exiting Test: Mpich not installed")

        try:
            # initialise test specific variables
            client_processes = self.params.get("np", '/run/client_processes/')

            # obtaining pool uuid and svc list
            pool_uuid = self.pool.get_uuid_str()
            svc_list = ""
            for i in range(self.createsvc):
                svc_list += str(int(self.pool.svc.rl_ranks[i])) + ":"
            svc_list = svc_list[:-1]

            # running tests
            self.mpio.run_llnl_mpi4py_hdf5(self.basepath, self.hostfile_clients,
                                           pool_uuid, test_repo, test_name,
                                           client_processes)

            # Parsing output to look for failures
            # stderr directed to stdout
            stdout = self.logdir + "/stdout"
            searchfile = open(stdout, "r")
            error_message = ["non-zero exit code", "MPI_Abort", "MPI_ABORT",
                             "ERROR"]

            for line in searchfile:
                for i in range(len(error_message)):
                    if error_message[i] in line:
                        self.fail("Test Failed with error_message: {}"
                                  .format(error_message[i]))

        except (MpioFailed, DaosApiError) as excep:
            self.fail("<{0} Test Failed> \n{1}".format(test_name, excep))

    def test_llnl(self):
        """
        Test ID: DAOS-2231
        Run LLNL test provided in mpich package
        Testing various I/O functions provided in llnl test suite
        :avocado: tags=mpio,llnlmpi4py,llnl
        """
        test_repo = self.params.get("llnl", '/run/test_repo/')
        self.run_test(test_repo, "llnl")

    def test_mpi4py(self):
        """
        Test ID: DAOS-2231
        Run mpi4py io test provided in mpi4py package
        Testing various I/O functions provided in mpi4py test suite
        :avocado: tags=mpio,llnlmpi4py,mpi4py
        """
        test_repo = self.params.get("mpi4py", '/run/test_repo/')
        self.run_test(test_repo, "mpi4py")

    def test_hdf5(self):
        """
        Test ID: DAOS-2252
        Run HDF5 testphdf5 and t_shapesame provided in HDF5 package
        Testing various I/O functions provided in HDF5 test suite
        :avocado: tags=mpio,llnlmpi4py,hdf5
        """
        test_repo = self.params.get("hdf5", '/run/test_repo/')
        self.run_test(test_repo, "hdf5")
