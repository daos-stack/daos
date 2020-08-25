#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

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
import threading
import time

from ClusterShell.NodeSet import NodeSet
from apricot import TestWithServers
from ior_utils import IorCommand
from command_utils_base import CommandFailure
from job_manager_utils import Mpirun
from general_utils import pcmd
from daos_utils import DaosCommand
from mpio_utils import MpioUtils
from test_utils_pool import TestPool
from test_utils_container import TestContainer

from dfuse_utils import Dfuse


class IorTestBase(TestWithServers):
    """Base IOR test class.

    :avocado: recursive
    """

    IOR_WRITE_PATTERN = "Commencing write performance test"
    IOR_READ_PATTERN = "Commencing read performance test"

    def __init__(self, *args, **kwargs):
        """Initialize a IorTestBase object."""
        super(IorTestBase, self).__init__(*args, **kwargs)
        self.ior_cmd = None
        self.processes = None
        self.hostfile_clients_slots = None
        self.dfuse = None
        self.container = None
        self.lock = None
        self.mpirun = None

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
        self.subprocess = self.params.get("subprocess", '/run/ior/*', False)

        # lock is needed for run_multiple_ior method.
        self.lock = threading.Lock()

    def tearDown(self):
        """Tear down each test case."""
        try:
            if self.dfuse:
                self.dfuse.stop()
        finally:
            # Stop the servers and agents
            super(IorTestBase, self).tearDown()

    def create_pool(self):
        """Create a TestPool object to use with ior."""
        # Get the pool params
        self.pool = TestPool(
            self.context, dmg_command=self.get_dmg_command())
        self.pool.get_params(self)

        # Create a pool
        self.pool.create()

    def create_cont(self):
        """Create a TestContainer object to be used to create container."""
        # Get container params
        self.container = TestContainer(
            self.pool, daos_command=DaosCommand(self.bin))
        self.container.get_params(self)

        # create container
        self.container.create()

    def _start_dfuse(self):
        """Create a DfuseCommand object to start dfuse."""
        # Get Dfuse params
        self.dfuse = Dfuse(self.hostlist_clients, self.tmp)
        self.dfuse.get_params(self)

        # update dfuse params
        self.dfuse.set_dfuse_params(self.pool)
        self.dfuse.set_dfuse_cont_param(self.container)
        self.dfuse.set_dfuse_exports(self.server_managers[0], self.client_log)

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
                          test_file="daos:testFile", create_pool=True,
                          create_cont=True, stop_dfuse=True):
        """Execute ior with optional overrides for ior flags and object_class.

        If specified the ior flags and ior daos object class parameters will
        override the values read from the yaml file.

        Args:
            intercept (str, optional): path to the interception library. Shall
                    be used only for POSIX through DFUSE. Defaults to None.
            test_file_suffix (str, optional): suffix to add to the end of the
                test file name. Defaults to "".
            test_file (str, optional): ior test file name. Defaults to
                "daos:testFile". Is ignored when using POSIX through DFUSE.
            create_pool (bool, optional): If it is true, create pool and
                container else just run the ior. Defaults to True.
            create_cont (bool, optional): Create new container. Default is True
            stop_dfuse (bool, optional): Stop dfuse after ior command is
                finished. Default is True.

        Returns:
            CmdResult: result of the ior command execution

        """
        if create_pool:
            self.update_ior_cmd_with_pool(create_cont)

        # start dfuse if api is POSIX
        if self.ior_cmd.api.value == "POSIX":
            # Connect to the pool, create container and then start dfuse
            if not self.dfuse:
                self._start_dfuse()
            test_file = os.path.join(self.dfuse.mount_dir.value, "testfile")
        elif self.ior_cmd.api.value == "DFS":
            test_file = os.path.join("/", "testfile")

        self.ior_cmd.test_file.update("".join([test_file, test_file_suffix]))

        out = self.run_ior(self.get_ior_job_manager_command(), self.processes,
                           intercept)

        if stop_dfuse and self.dfuse:
            self.dfuse.stop()
            self.dfuse = None
        return out

    def update_ior_cmd_with_pool(self, create_cont=True):
        """Update ior_cmd with pool."""
        # Create a pool if one does not already exist
        if self.pool is None:
            self.create_pool()
        # Create a container, if needed.
        # Don't pass uuid and pool handle to IOR.
        # It will not enable checksum feature
        if create_cont:
            self.pool.connect()
            self.create_cont()
        # Update IOR params with the pool and container params
        self.ior_cmd.set_daos_params(self.server_group, self.pool,
                                     self.container.uuid)

    def get_ior_job_manager_command(self):
        """Get the MPI job manager command for IOR.

        Returns:
            str: the path for the mpi job manager command

        """
        # Initialize MpioUtils if IOR is running in MPIIO or DFS mode
        if self.ior_cmd.api.value in ["MPIIO", "POSIX", "DFS"]:
            mpio_util = MpioUtils()
            if mpio_util.mpich_installed(self.hostlist_clients) is False:
                self.fail("Exiting Test: Mpich not installed")
        else:
            self.fail("Unsupported IOR API")

        if self.subprocess:
            self.mpirun = Mpirun(self.ior_cmd, True, mpitype="mpich")
        else:
            self.mpirun = Mpirun(self.ior_cmd, mpitype="mpich")

        return self.mpirun

    def check_subprocess_status(self, operation="write"):
        """Check subprocess status """
        if operation == "write":
            self.ior_cmd.pattern = self.IOR_WRITE_PATTERN
        elif operation == "read":
            self.ior_cmd.pattern = self.IOR_READ_PATTERN
        else:
            self.fail("Exiting Test: Inappropriate operation type \
                      for subprocess status check")

        if not self.ior_cmd.check_ior_subprocess_status(
                self.mpirun.process, self.ior_cmd):
            self.fail("Exiting Test: Subprocess not running")

    def run_ior(self, manager, processes, intercept=None, display_space=True):
        """Run the IOR command.

        Args:
            manager (str): mpi job manager command
            processes (int): number of host processes
            intercept (str): path to interception library.
        """
        env = self.ior_cmd.get_default_env(str(manager), self.client_log)
        if intercept:
            env["LD_PRELOAD"] = intercept
        manager.assign_hosts(
            self.hostlist_clients, self.workdir, self.hostfile_clients_slots)
        manager.assign_processes(processes)
        manager.assign_environment(env)

        try:
            if display_space:
                self.pool.display_pool_daos_space()
            out = manager.run()

            if not self.subprocess:
                for line in out.stdout.splitlines():
                    if 'WARNING' in line:
                        self.fail("IOR command issued warnings.\n")
            return out
        except CommandFailure as error:
            self.log.error("IOR Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")
        finally:
            if not self.subprocess and display_space:
                self.pool.display_pool_daos_space()

    def stop_ior(self):
        """Stop IOR process.
        Args:
            manager (str): mpi job manager command
        """
        self.log.info(
            "<IOR> Stopping in-progress IOR command: %s", self.mpirun.__str__())

        try:
            out = self.mpirun.stop()
            return out
        except CommandFailure as error:
            self.log.error("IOR stop Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")
        finally:
            self.pool.display_pool_daos_space()


    def run_multiple_ior_with_pool(self, results, intercept=None):
        """Execute ior with optional overrides for ior flags and object_class.

        If specified the ior flags and ior daos object class parameters will
        override the values read from the yaml file.

        Args:
            intercept (str): path to the interception library. Shall be used
                             only for POSIX through DFUSE.
            ior_flags (str, optional): ior flags. Defaults to None.
            object_class (str, optional): daos object class. Defaults to None.
        """
        self.update_ior_cmd_with_pool()

        # start dfuse for POSIX api. This is specific to interception
        # library test requirements.
        self._start_dfuse()

        # Create two jobs and run in parallel.
        # Job1 will have 3 client set up to use dfuse + interception
        # library
        # Job2 will have 1 client set up to use only dfuse.
        job1 = self.get_new_job(self.hostlist_clients[:-1], 1,
                                results, intercept)
        job2 = self.get_new_job([self.hostlist_clients[-1]], 2,
                                results, None)

        job1.start()
        # Since same ior_cmd is used to trigger the MPIRUN
        # with different parameters, pausing for 2 seconds to
        # avoid data collisions.
        time.sleep(2)
        job2.start()
        job1.join()
        job2.join()
        self.dfuse.stop()
        self.dfuse = None

    def get_new_job(self, clients, job_num, results, intercept=None):
        """Create a new thread for ior run.

        Args:
            clients (list): hosts on which to run ior
            job_num (int): Assigned job number
            results (dict): A dictionary object to store the ior metrics
            intercept (path): Path to interception library
        """
        job = threading.Thread(target=self.run_multiple_ior, args=[
            clients, results, job_num, intercept])
        return job

    def run_multiple_ior(self, clients, results, job_num, intercept=None):
        """Run the IOR command.

        Args:
            clients (list): hosts on which to run ior
            results (dict): A dictionary object to store the ior metrics
            job_num (int): Assigned job number
            intercept (str, optional): path to interception library. Defaults to
                None.
        """
        self.lock.acquire(True)
        tsize = self.ior_cmd.transfer_size.value
        testfile = os.path.join(self.dfuse.mount_dir.value,
                                "testfile{}{}".format(tsize, job_num))
        if intercept:
            testfile += "intercept"
        self.ior_cmd.test_file.update(testfile)
        manager = self.get_ior_job_manager_command()
        procs = (self.processes // len(self.hostlist_clients)) * len(clients)
        env = self.ior_cmd.get_default_env(str(manager), self.client_log)
        if intercept:
            env["LD_PRELOAD"] = intercept
        manager.assign_hosts(clients, self.workdir, self.hostfile_clients_slots)
        manager.assign_processes(procs)
        manager.assign_environment(env)
        self.lock.release()
        try:
            self.pool.display_pool_daos_space()
            out = manager.run()
            self.lock.acquire(True)
            results[job_num] = IorCommand.get_ior_metrics(out)
            self.lock.release()
        except CommandFailure as error:
            self.log.error("IOR Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")
        finally:
            self.pool.display_pool_daos_space()

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

    def execute_cmd(self, cmd, fail_on_err=True, display_output=True):
        """Execute cmd using general_utils.pcmd

          Args:
            cmd (str): String command to be executed
            fail_on_err (bool): Boolean for whether to fail the test if command
                                execution returns non zero return code.
            display_output (bool): Boolean for whether to display output.

          Returns:
            dict: a dictionary of return codes keys and accompanying NodeSet
                  values indicating which hosts yielded the return code.
        """
        try:
            # execute bash cmds
            ret = pcmd(
                self.hostlist_clients, cmd, verbose=display_output, timeout=300)
            if 0 not in ret:
                error_hosts = NodeSet(
                    ",".join(
                        [str(node_set) for code, node_set in
                         ret.items() if code != 0]))
                if fail_on_err:
                    raise CommandFailure(
                        "Error running '{}' on the following "
                        "hosts: {}".format(cmd, error_hosts))

         # report error if any command fails
        except CommandFailure as error:
            self.log.error("DfuseSparseFile Test Failed: %s",
                           str(error))
            self.fail("Test was expected to pass but "
                      "it failed.\n")
        return ret
