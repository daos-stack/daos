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

from ClusterShell.NodeSet import NodeSet
from apricot import TestWithServers
from test_utils import TestPool
from mpio_utils import MpioUtils
from mdtest_utils import MdtestCommand
from command_utils import CommandFailure
from job_manager_utils import OpenMPI, Mpich
from dfuse_utils import Dfuse
from daos_utils import create_container


class MdtestBase(TestWithServers):
    """Base mdtest class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a MdtestBase object."""
        super(MdtestBase, self).__init__(*args, **kwargs)
        self.mdtest_cmd = None
        self.processes = None
        self.dfuse = None

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

        # Until DAOS-3320 is resolved run IOR for POSIX
        # with single client node
        if self.mdtest_cmd.api.value == "POSIX":
            self.hostlist_clients = [self.hostlist_clients[0]]

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
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)

        # Create a pool
        self.pool.create()

    def _create_cont(self):
        """Create a TestContainer object to be used to create container."""
        # TO-DO: Enable container using TestContainer object,
        # once DAOS-3355 is resolved.
        # Get Container params
        # self.container = TestContainer(self.pool)
        # self.container.get_params(self)

        # create container
        # self.container.create()

        # Create a POSIX-type container
        result = create_container(
            self.bin,
            self.mdtest_cmd.dfs_pool_uuid.value,
            self.mdtest_cmd.dfs_svcl.value,
            "POSIX",
            self.server_managers[0].get_interface_envs())

        if result is None:
            self.fail("Container create failed")

        cont_uuid = result.stdout.split()[3]
        self.log.debug("daos cont create stdout:\n  %s", result.stdout)
        self.log.debug("daos cont create stdot_text:\n  %s", result.stdout_text)
        self.log.info("Container created with UUID %s", cont_uuid)

        return cont_uuid

    def _start_dfuse(self):
        """Create a DfuseCommand object to start dfuse."""
        # Get Dfuse params
        self.dfuse = Dfuse(
            self.hostlist_clients, self.tmp,
            self.server_managers[0].get_interface_envs())
        self.dfuse.get_params(self)

        # update dfuse params
        self.dfuse.set_dfuse_params(self.pool)
        self.dfuse.set_dfuse_cont_param(self._create_cont())

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
            # Uncomment below two lines once DAOS-3355 is resolved
            # self.pool.connect()
            # self.create_cont()
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
        if manager == "MPICH":
            # Initialize MpioUtils if mdtest needs to be run using mpich
            mpio_util = MpioUtils()
            if mpio_util.mpich_installed(self.hostlist_clients) is False:
                self.fail("Exiting Test: Mpich not installed")
            manager_class = Mpich(self.mdtest_cmd)
        else:
            manager_class = OpenMPI(self.mdtest_cmd)

        return manager_class

    def run_mdtest(self, manager, processes, slots=None):
        """Run the Mdtest command.

        Args:
            manager (str): mpi job manager command
            processes (int): number of host processes
            slots (int, optional): slots per host to specify in the hostfile.
                Defaults to None
        """
        manager.assign_hosts(self.hostlist_clients, self.workdir, slots)
        manager.assign_processes(processes)
        manager.assign_environment(
            self.mdtest_cmd.get_default_env(
                manager.command, self.tmp, self.client_log))

        try:
            manager.run()
        except CommandFailure as error:
            self.log.error("Mdtest Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")
