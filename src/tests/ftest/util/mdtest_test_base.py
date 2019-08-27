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
import subprocess

from apricot import TestWithServers
from test_utils import TestPool
from mpio_utils import MpioUtils
from mdtest_utils import MdtestCommand, MdtestFailed

SERVER_LOG = "/tmp/server.log"
CLIENT_LOG = "client_daos.log"

class MdtestBase(TestWithServers):
    """
    Base mdtest class
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a MdtestBase object."""
        super(MdtestBase, self).__init__(*args, **kwargs)
        self.mdtest_cmd = None
        self.processes = None
        self.hostfile_clients_slots = None

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(MdtestBase, self).setUp()

        # Get the parameters for Mdtest
        self.mdtest_cmd = MdtestCommand()
        self.mdtest_cmd.get_params(self)
        self.processes = self.params.get("np", '/run/mdtest/client_processes/*')
        self.manager = self.params.get("manager", '/run/mdtest/*', "MPICH")
        self.test_id = str(self.name).split("-")[0]
        print("self.test_id:{}".format(self.test_id))

        # Determine the path and name of the daos server log using the
        # D_LOG_FILE env or, if not set, the value used in the doas server yaml
        self.log_dir, self.server_log = os.path.split(
            os.getenv("D_LOG_FILE", SERVER_LOG))
        self.client_log = os.path.join(self.log_dir,
                                       self.test_id + "_" + CLIENT_LOG)
        # To generate the seperate client log file
        self.orterun_env = '-x D_LOG_FILE={}'.format(self.client_log)

    def tearDown(self):
        """Tear down each test case."""
        try:
            if self.pool is not None:
                self.pool.destroy(1)
        finally:
            # Stop the servers and agents
            super(MdtestBase, self).tearDown()

        # collect up a debug log so that we have a separate one for each
        # subtest
        if self.test_id:
            try:
                new_logfile = os.path.join(
                    self.log_dir, self.test_id + "_" + self.server_log)
                # rename on each of the servers
                for host in self.hostlist_servers:
                    subprocess.check_call(
                        ['ssh', host,
                         '[ -f \"{0}\" ] && mv \"{0}\" \"{1}\"'.format(
                             SERVER_LOG, new_logfile)])
            except KeyError:
                pass

    def execute_mdtest(self):
        """
        Execute mdtest with optional overrides for mdtest flags
        and object_class.
        If specified the mdtest flags and mdtest daos object class parameters
        will override the values read from the yaml file.
        Args:
            mdtest_flags (str, optional): mdtest flags. Defaults to None.
            object_class (str, optional): daos object class. Defaults to None.
        """
        # Get the pool params
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)

        # Create a pool
        self.pool.create()

        # Run Mdtest
        self.mdtest_cmd.set_daos_params(self.server_group, self.pool)
        self.run_mdtest(self.get_job_manager_command(self.manager),
                        self.processes)

    def get_job_manager_command(self, manager):
        """Get the MPI job manager command for Mdtest.
        Returns:
            str: the path for the mpi job manager command
        """
        # Initialize MpioUtils if mdtest needs to be run using mpich
        if manager == "MPICH":
            mpio_util = MpioUtils()
            if mpio_util.mpich_installed(self.hostlist_clients) is False:
                self.fail("Exiting Test: Mpich not installed")
            return os.path.join(mpio_util.mpichinstall, "bin", "mpirun")
        return self.orterun

    def run_mdtest(self, manager, processes):
        """Run the Mdtest command.
        Args:
            manager (str): mpi job manager command
            processes (int): number of host processes
        """
        try:
            self.mdtest_cmd.run(
                manager, self.tmp, processes, self.hostfile_clients)
        except MdtestFailed as error:
            self.log.error("Mdtest Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")
