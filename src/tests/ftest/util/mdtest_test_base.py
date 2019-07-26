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

from apricot import TestWithServers
from daos_api import DaosPool
from mpio_utils import MpioUtils
from mdtest_utils import MdtestCommand, MdtestFailed

import os
import write_host_file
# import mdtest_utils

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

        # Recreate the client hostfile without slots defined
#        self.hostfile_clients = write_host_file.write_host_file(
#            self.hostlist_clients, self.workdir, None)

        # Get the parameters for Mdtest
        self.mdtest_cmd = MdtestCommand()
        self.mdtest_cmd.get_params(self)
        self.processes = self.params.get("np", '/run/mdtest/client_processes/*')
        self.manager = self.params.get("manager", '/run/mdtest/*', "MPICH")

    def tearDown(self):
        """Tear down each test case."""
        try:
            if self.pool is not None:
                self.pool.destroy(1)
        finally:
            # Stop the servers and agents
            super(MdtestBase, self).tearDown()

    def execute_mdtest(self, mdtest_flags=None, object_class=None):
        """
        Execute mdtest with optional overrides for mdtest flags
        and object_class.
        If specified the mdtest flags and mdtest daos object class parameters
        will override the values read from the yaml file.
        Args:
            mdtest_flags (str, optional): mdtest flags. Defaults to None.
            object_class (str, optional): daos object class. Defaults to None.
        """
        # Get the parameters used to create a pool
        mode = self.params.get("mode", "/run/pool/*")
        uid = os.geteuid()
        gid = os.getegid()
        group = self.params.get("setname", "/run/pool/*", self.server_group)
        scm_size = self.params.get("scm_size", "/run/pool/*")
        nvme_size = self.params.get("nvme_size", "/run/pool/*", 0)
        svcn = self.params.get("svcn", "/run/pool/*", 1)

        # Initialize a python pool object then create the underlying
        # daos storage
        self.pool = DaosPool(self.context)
        self.pool.create(
            mode, uid, gid, scm_size, group, None, None, svcn, nvme_size)

        # Initialize MpioUtils if Mdtest is running in MPIIO mode
#        mpio_util = MpioUtils()
#        if mpio_util.mpich_installed(self.hostlist_clients) is False:
#            self.fail("Exiting Test: Mpich not installed")
#        path = mpio_util.mpichinstall

#        svc_list = ""
#        for item in range(svcn):
#            svc_list += str(int(self.pool.svc.rl_ranks[item])) + ":"
#        svc_list = svc_list[:-1]

        # assign mdtest params
#        self.mdtest_cmd.dfs_pool_uuid.value = self.pool.get_uuid_str()
#        self.mdtest_cmd.dfs_svcl.value = svc_list

        # Override the yaml Mdtest params with provided values
#        if mdtest_flags:
#            self.mdtest_cmd.flags.value = mdtest_flags
#        if object_class:
#            self.mdtest_cmd.daos_oclass.value = object_class

        # Run Mdtest
        self.mdtest_cmd.set_daos_params(self.server_group, self.pool)
        self.run_ior(self.get_job_manager_command(self.manager), self.processes)

#        try:
#            self.mdtest_cmd.run(
#                self.basepath, self.processes, self.hostfile_clients, True,
#                path)
#        except mdtest_utils.MdtestFailed as excep:
#            print(excep)
#            self.fail("Test was expected to pass but it failed.\n")

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

    def run_ior(self, manager, processes):
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
