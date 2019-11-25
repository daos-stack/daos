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
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from __future__ import print_function

import os
import sys
import json
from avocado import Test
from apricot import skipForTicket

sys.path.append('./util')
sys.path.append('../util')

import agent_utils
import server_utils
import write_host_file
from mpio_utils import MpioUtils, MpioFailed
from pydaos.raw import DaosContext

class Romio(Test):
    """
    Runs Romio test.
    """

    def __init__(self, *args, **kwargs):

        super(Romio, self).__init__(*args, **kwargs)

        self.basepath = None
        self.server_group = None
        self.context = None
        self.hostlist_servers = None
        self.hostfile_servers = None
        self.hostlist_clients = None
        self.hostfile_clients = None

    def setUp(self):
        self.agent_sessions = None
        # get paths from the build_vars generated by build
        with open('../../.build_vars.json') as build_file:
            build_paths = json.load(build_file)
        self.basepath = os.path.normpath(build_paths['PREFIX'] + "/../")
        self.prefix = build_paths['PREFIX']
        self.tmp = os.path.join(self.prefix, 'tmp')

        self.server_group = self.params.get("name", '/server_config/',
                                            'daos_server')

        # setup the DAOS python API
        self.context = DaosContext(build_paths['PREFIX'] + '/lib64/')

        self.hostlist_servers = self.params.get("test_servers", '/run/hosts/')
        self.hostfile_servers = (
            write_host_file.write_host_file(self.hostlist_servers,
                                            self.workdir))
        print("Host file servers is: {}".format(self.hostfile_servers))

        self.hostlist_clients = self.params.get("test_clients", '/run/hosts/')
        self.hostfile_clients = (
            write_host_file.write_host_file(self.hostlist_clients,
                                            self.workdir))
        print("Host file clients is: {}".format(self.hostfile_clients))

        # start servers
        self.agent_sessions = agent_utils.run_agent(self,
                                                    self.hostlist_servers,
                                                    self.hostlist_clients)
        server_utils.run_server(self, self.hostfile_servers, self.server_group)

        self.mpio = None

    def tearDown(self):
        if self.agent_sessions:
            agent_utils.stop_agent(self.agent_sessions, self.hostlist_clients)
        server_utils.stop_server(hosts=self.hostlist_servers)

    @skipForTicket("CORCI-635")
    def test_romio(self):
        """
        Test ID: DAOS-1994
        Run Romio test provided in mpich package
        Testing various I/O functions provided in romio test suite
        :avocado: tags=all,mpiio,pr,small,romio
        """
        # setting romio parameters
        romio_test_repo = self.params.get("romio_repo", '/run/romio/')

        # initialize MpioUtils
        self.mpio = MpioUtils()
        if self.mpio.mpich_installed(self.hostlist_clients) is False:
            self.fail("Exiting Test: Mpich not installed")

        try:
            # Romio do not need slots in hostfile
            with open(self.hostfile_clients) as client_file:
                new_text = client_file.read().replace('slots=1', '')

            with open(self.hostfile_clients, "w") as client_file:
                client_file.write(new_text)

            # running romio
            self.mpio.run_romio(self.basepath, self.hostlist_clients,
                                romio_test_repo)

            # Parsing output to look for failures
            # stderr directed to stdout
            stdout = self.logdir + "/stdout"
            searchfile = open(stdout, "r")
            error_message = ["non-zero exit code", "MPI_Abort", "errors",
                             "failed to create pool",
                             "failed to parse pool UUID",
                             "failed to destroy pool"]

            for line in searchfile:
                for i in xrange(len(error_message)):
                    if error_message[i] in line:
                        self.fail("Romio Test Failed with error_message: "
                                  "{}".format(error_message[i]))

        except (MpioFailed) as excep:
            self.fail("<Romio Test Failed> \n{}".format(excep))
