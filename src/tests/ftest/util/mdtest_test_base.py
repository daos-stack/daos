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
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from __future__ import print_function

import os
import subprocess

from ClusterShell.NodeSet import NodeSet
from apricot import TestWithServers
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from mpio_utils import MpioUtils
from mdtest_utils import MdtestCommand
from command_utils import Mpirun, Orterun, CommandFailure
from dfuse_utils import Dfuse
import write_host_file


class MdtestBase(TestWithServers):
    """Base mdtest class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a MdtestBase object."""
        super(MdtestBase, self).__init__(*args, **kwargs)
        self.mdtest_cmd = None
        self.processes = None
        self.hostfile_clients_slots = None
        self.dfuse = None
        self.container = None

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()
        # Start the servers and agents
        super(MdtestBase, self).setUp()

        # Get the parameters for Mdtest
        self.mdtest_cmd = MdtestCommand()
        self.mdtest_cmd.get_params(self)
        self.processes = self.params.get("np", '/run/mdtest/client_processes/*')
        self.manager = self.params.get("manager", '/run/mdtest/*', "MPICH")
        self.co_prop = self.params.get("container_properties",
                                       "/run/container/*")

        # Until DAOS-3320 is resolved run IOR for POSIX
        # with single client node
        if self.mdtest_cmd.api.value == "POSIX":
            self.hostlist_clients = [self.hostlist_clients[0]]
            self.hostfile_clients = write_host_file.write_host_file(
                self.hostlist_clients, self.workdir,
                self.hostfile_clients_slots)

    def tearDown(self):
        """Tear down each test case."""
        try:
            self.dfuse = None
        finally:
            # Stop the servers and agents
            super(MdtestBase, self).tearDown()

    def _create_pool(self):
        """Create a pool and execute Mdtest."""
        # Get the pool params
        self.pool = TestPool(self.context, dmg_command=self.get_dmg_command())
        self.pool.get_params(self)

        # Create a pool
        self.pool.create()

    def _create_cont(self):
        """Create a TestContainer object to be used to create container."""
        # Enable container using TestContainer object,
        # Get Container params
        self.container = TestContainer(self.pool)
        self.container.get_params(self)
        # create container
        self.container.create(con_in=self.co_prop)

    def _start_dfuse(self):
        """Create a DfuseCommand object to start dfuse."""
        # Get Dfuse params
        self.dfuse = Dfuse(self.hostlist_clients, self.tmp, True)
        self.dfuse.get_params(self)

        # update dfuse params
        self.dfuse.set_dfuse_params(self.pool)
        self.dfuse.set_dfuse_cont_param(self.container)

        try:
            # start dfuse
            self.dfuse.run()
        except CommandFailure as error:
            self.log.error("Dfuse command %s failed on hosts %s",
                           str(self.dfuse), str(NodeSet(self.dfuse.hosts)),
                           exc_info=error)
            self.fail("Unable to launch Dfuse.\n")


    def execute_mdtest(self):
        """Runner method for Mdtest."""

        # Create a pool if one does not already exist
        if self.pool is None:
            self._create_pool()
        # set Mdtest params
        self.mdtest_cmd.set_daos_params(self.server_group, self.pool)

        # start dfuse if api is POSIX
        if self.mdtest_cmd.api.value == "POSIX":
            # Connect to the pool, create container and then start dfuse
            self.pool.connect()
            self._create_cont()
            self._start_dfuse()
            self.mdtest_cmd.test_dir.update(self.dfuse.mount_dir.value)

       # Run Mdtest
        self.run_mdtest(self.get_job_manager_command(self.manager),
                        self.processes)

    def get_job_manager_command(self, manager):
        """Get the MPI job manager command for Mdtest.

        Returns:
            JobManager: the object for the mpi job manager command

        """
        # Initialize MpioUtils if mdtest needs to be run using mpich
        if manager == "MPICH":
            mpio_util = MpioUtils()
            if mpio_util.mpich_installed(self.hostlist_clients) is False:
                self.fail("Exiting Test: Mpich not installed")
            path = os.path.join(mpio_util.mpichinstall, "bin")
            return Mpirun(self.mdtest_cmd, path, mpitype="mpich")

        path = os.path.join(self.ompi_prefix, "bin")
        return Orterun(self.mdtest_cmd, path)

    def run_mdtest(self, manager, processes):
        """Run the Mdtest command.

        Args:
            manager (str): mpi job manager command
            processes (int): number of host processes
        """
        env = self.mdtest_cmd.get_default_env(
            str(manager), self.tmp, self.client_log)
        manager.setup_command(env, self.hostfile_clients, processes)
        try:
            manager.run()
        except CommandFailure as error:
            self.log.error("Mdtest Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")
