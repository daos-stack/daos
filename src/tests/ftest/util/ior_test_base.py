#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

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
"""
import os

from apricot import TestWithServers
from daos_api import DaosPool
from ior_utils import IorCommand, IorFailed
from mpio_utils import MpioUtils


class IorTestBase(TestWithServers):
    """Base IOR test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a IorTestBase object."""
        super(IorTestBase, self).__init__(*args, **kwargs)
        self.ior_cmd = None
        self.processes = None
        self.hostfile_clients_slots = None

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(IorTestBase, self).setUp()

        # Get the parameters for IOR
        self.ior_cmd = IorCommand()
        self.ior_cmd.get_params(self)
        self.processes = self.params.get("np", '/run/ior/client_processes/*')

    def tearDown(self):
        """Tear down each test case."""
        try:
            if self.pool is not None and self.pool.attached:
                self.pool.destroy(1)
        finally:
            # Stop the servers and agents
            super(IorTestBase, self).tearDown()

    def run_ior_with_pool(self):
        """Execute ior with optional overrides for ior flags and object_class.

        If specified the ior flags and ior daos object class parameters will
        override the values read from the yaml file.

        Args:
            ior_flags (str, optional): ior flags. Defaults to None.
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
        self.ior_cmd.set_daos_params(self.server_group, self.pool)

        # Run IOR
        self.run_ior(self.get_job_manager_command(), self.processes)

    def get_job_manager_command(self):
        """Get the MPI job manager command for IOR.

        Returns:
            str: the path for the mpi job manager command

        """
        # Initialize MpioUtils if IOR is running in MPIIO mode
        if self.ior_cmd.api.value == "MPIIO":
            mpio_util = MpioUtils()
            if mpio_util.mpich_installed(self.hostlist_clients) is False:
                self.fail("Exiting Test: Mpich not installed")
            return os.path.join(mpio_util.mpichinstall, "bin", "mpirun")
        return self.orterun

    def run_ior(self, manager, processes):
        """Run the IOR command.

        Args:
            manager (str): mpi job manager command
            processes (int): number of host processes
        """
        try:
            self.ior_cmd.run(
                manager, self.tmp, processes, self.hostfile_clients)
        except IorFailed as error:
            self.log.error("IOR Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")

    def verify_pool_size(self, original_pool_info, processes):
        """Validate the pool size.

        Args:
            original_pool_info (PoolInfo): Pool info prior to IOR
            processes (int): number of processes
        """
        # Get the current pool size for comparison
        current_pool_info = self.pool.pool_query()

        # If Transfer size is < 4K, Pool size will verified against NVMe, else
        # it will be checked against SCM
        if self.ior_cmd.transfer_size.value >= 4096:
            self.log.info(
                "Size is > 4K,Size verification will be done with NVMe size")
            storage_index = 1
        else:
            self.log.info(
                "Size is < 4K,Size verification will be done with SCM size")
            storage_index = 0

        actual_pool_size = \
            original_pool_info.pi_space.ps_space.s_free[storage_index] - \
            current_pool_info.pi_space.ps_space.s_free[storage_index]
        expected_pool_size = self.ior_cmd.get_aggregate_total(processes)

        if actual_pool_size < expected_pool_size:
            self.fail(
                "Pool Free Size did not match: actual={}, expected={}".format(
                    actual_pool_size, expected_pool_size))
