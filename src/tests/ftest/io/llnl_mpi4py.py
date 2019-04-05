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
from avocado       import Test

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')
import ServerUtils
import WriteHostFile
from MpioUtils import MpioUtils, MpioFailed
from daos_api import DaosContext, DaosPool, DaosApiError

class LlnlMpi4py(Test):
    """
    Runs LLNL and MPI4PY test suites.
    """

    def __init__(self, *args, **kwargs):

        super(LlnlMpi4py, self).__init__(*args, **kwargs)

        self.basepath = None
        self.server_group = None
        self.context = None
        self.pool = None
        self.mpio = None
        self.hostlist_servers = None
        self.hostfile_servers = None
        self.hostlist_clients = None
        self.hostfile_clients = None

    def setUp(self):
        # get paths from the build_vars generated by build
        with open('../../../.build_vars.json') as var_file:
            build_paths = json.load(var_file)
        self.basepath = os.path.normpath(build_paths['PREFIX']  + "/../")

        self.server_group = self.params.get("name", '/server/', 'daos_server')

        # setup the DAOS python API
        self.context = DaosContext(build_paths['PREFIX'] + '/lib/')

        self.hostlist_servers = self.params.get("test_servers", '/run/hosts/')
        self.hostfile_servers = WriteHostFile.WriteHostFile(self.hostlist_servers,
                                                            self.workdir)
        print("Host file servers is: {}".format(self.hostfile_servers))

        self.hostlist_clients = self.params.get("test_clients", '/run/hosts/')
        self.hostfile_clients = WriteHostFile.WriteHostFile(self.hostlist_clients,
                                                            self.workdir, None)
        print("Host file clients is: {}".format(self.hostfile_clients))

        # start servers
        ServerUtils.runServer(self.hostfile_servers, self.server_group,
                              self.basepath)

    def tearDown(self):
        ServerUtils.stopServer(hosts=self.hostlist_servers)

    def executable(self, test_repo, test_name):
        """
        Executable function to be used by test functions below
        test_repo       --location of test repository
        test_name       --name of the test to be run
        """
        # initialize MpioUtils
        self.mpio = MpioUtils()
        if self.mpio.mpich_installed(self.hostlist_clients) is False:
            self.fail("Exiting Test: Mpich not installed")

        # parameters used in pool create
        createmode = self.params.get("mode", '/run/pool/createmode/*/')
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = self.params.get("setname", '/run/pool/createset/')
        createsize = self.params.get("size", '/run/pool/createsize/')
        createsvc = self.params.get("svcn", '/run/pool/createsvc/')

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None, None, createsvc)

            pool_uuid = self.pool.get_uuid_str()
            svc_list = ""
            for i in range(createsvc):
                svc_list += str(int(self.pool.svc.rl_ranks[i])) + ":"
            svc_list = svc_list[:-1]

            # running tests
            self.mpio.run_llnl_mpi4py(self.basepath, self.hostfile_clients,
                                      pool_uuid, test_repo, test_name)

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

        except (MpioFailed, DaosApiError) as error:
            self.fail("<{0} Test Failed> \n{1}".format(test_name, error))

    def test_llnl(self):
        """
        Test ID: DAOS-2231
        Run LLNL test provided in mpich package
        Testing various I/O functions provided in llnl test suite
        :avocado: tags=mpio,llnl
        """
        test_repo = self.params.get("llnl", '/run/test_repo/')
        self.executable(test_repo, "llnl")

    def test_mpi4py(self):
        """
        Test ID: DAOS-2231
        Run LLNL test provided in mpich package
        Testing various I/O functions provided in llnl test suite
        :avocado: tags=mpio,mpi4py
        """
        test_repo = self.params.get("mpi4py", '/run/test_repo/')
        self.executable(test_repo, "mpi4py")
