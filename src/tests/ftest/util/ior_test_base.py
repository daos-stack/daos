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

from ClusterShell.NodeSet import NodeSet
from apricot import TestWithServers
from ior_utils import IorCommand
from command_utils import Mpirun, CommandFailure
from mpio_utils import MpioUtils
from test_utils import TestPool
from dfuse_utils import Dfuse
from daos_utils import create_container
from write_host_file import write_host_file


class IorTestBase(TestWithServers):
    """Base IOR test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a IorTestBase object."""
        super(IorTestBase, self).__init__(*args, **kwargs)
        self.ior_cmd = None
        self.processes = None
        self.dfuse = None

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()
        # Start the servers and agents
        super(IorTestBase, self).setUp()

        # Get the parameters for IOR
        self.ior_cmd = IorCommand()
        self.ior_cmd.get_params(self)
        self.processes = self.params.get("np", '/run/ior/client_processes/*')
        # Until DAOS-3320 is resolved run IOR for POSIX
        # with single client node
        if self.ior_cmd.api.value == "POSIX":
            self.hostlist_clients = [self.hostlist_clients[0]]

    def tearDown(self):
        """Tear down each test case."""
        try:
            self.dfuse = None
        finally:
            # Stop the servers and agents
            super(IorTestBase, self).tearDown()

    def create_pool(self):
        """Create a TestPool object to use with ior."""
        # Get the pool params
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)

        # Create a pool
        self.pool.create()

    def create_cont(self):
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
            self.ior_cmd.daos_pool.value,
            self.ior_cmd.daos_svcl.value,
            "POSIX",
            self.server_managers[0].get_interface_envs())
        if result is not None:
            cont_uuid = result.stdout.split()[3]
            self.log.debug("daos cont create stdout:\n  %s", result.stdout)
            self.log.debug(
                "daos cont create stdot_text:\n  %s", result.stdout_text)
            self.log.info("Container created with UUID %s", cont_uuid)
            return cont_uuid
        else:
            self.fail("Container create failed")

    def start_dfuse(self):
        """Create a DfuseCommand object to start dfuse."""
        # Get Dfuse params
        self.dfuse = Dfuse(
            self.hostlist_clients, self.tmp,
            self.server_managers[0].get_interface_envs())
        self.dfuse.get_params(self)

        # update dfuse params
        self.dfuse.set_dfuse_params(self.pool)
        self.dfuse.set_dfuse_cont_param(self.create_cont())

        try:
            # start dfuse
            self.dfuse.run()
        except CommandFailure as error:
            self.log.error("Dfuse command %s failed on hosts %s",
                           str(self.dfuse),
                           str(NodeSet.fromlist(self.dfuse.hosts)),
                           exc_info=error)
            self.fail("Test was expected to pass but it failed.\n")

    def run_ior_with_pool(self, intercept=None, test_file_suffix="",
                          hostfile=None):
        """Execute ior with optional overrides for ior flags and object_class.

        If specified the ior flags and ior daos object class parameters will
        override the values read from the yaml file.

        Args:
            intercept (str): path to the interception library. Shall be used
                             only for POSIX through DFUSE.
            test_file_suffix (str, optional): Defaults to "".
            hostfile (str, optional): path to the hostfile. Defaults to None
                which will create a hostfile using all of the hostfile_client
                hosts w/o any slots specified.
        """
        # Create a pool if one does not already exist
        if self.pool is None:
            self.create_pool()
        # Update IOR params with the pool
        self.ior_cmd.set_daos_params(self.server_group, self.pool)

        # start dfuse if api is POSIX
        if self.ior_cmd.api.value == "POSIX":
            # Connect to the pool, create container and then start dfuse
            # Uncomment below two lines once DAOS-3355 is resolved
            # self.pool.connect()
            # self.create_cont()
            if self.ior_cmd.transfer_size.value == "256B":
                return "Skipping the case for transfer_size=256B"
            self.start_dfuse()
            testfile = os.path.join(self.dfuse.mount_dir.value,
                                    "testfile{}".format(test_file_suffix))

            self.ior_cmd.test_file.update(testfile)

        out = self.run_ior(
            self.get_job_manager_command(), self.processes, intercept, hostfile)

        return out

    def get_job_manager_command(self):
        """Get the MPI job manager command for IOR.

        Returns:
            str: the path for the mpi job manager command

        """
        # Initialize MpioUtils if IOR is running in MPIIO or DAOS mode
        if self.ior_cmd.api.value in ["MPIIO", "DAOS", "POSIX"]:
            mpio_util = MpioUtils()
            if mpio_util.mpich_installed(self.hostlist_clients) is False:
                self.fail("Exiting Test: Mpich not installed")
        else:
            self.fail("Unsupported IOR API")

        mpirun_path = os.path.join(mpio_util.mpichinstall, "bin")
        return Mpirun(self.ior_cmd, mpirun_path, mpitype="mpich")

    def run_ior(self, manager, processes, intercept=None, hostfile=None):
        """Run the IOR command.

        Args:
            manager (str): mpi job manager command
            processes (int): number of host processes
            intercept (str): path to interception library.
            hostfile (str, optional): path to the hostfile. Defaults to None
                which will create a hostfile using all of the hostfile_client
                hosts w/o any slots specified.
        """
        env = self.ior_cmd.get_default_env(
            str(manager), self.tmp, self.client_log)
        if intercept:
            env["LD_PRELOAD"] = intercept
        if hostfile is None:
            hostfile = write_host_file(
                self.hostlist_clients, self.workdir, None)
        manager.setup_command(env, hostfile, processes)
        try:
            out = manager.run()
            return out
        except CommandFailure as error:
            self.log.error("IOR Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")

    def verify_pool_size(self, original_pool_info, processes):
        """Validate the pool size.

        Args:
            original_pool_info (PoolInfo): Pool info prior to IOR
            processes (int): number of processes
        """
        # Get the current pool size for comparison
        current_pool_info = self.pool.pool.pool_query()

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
