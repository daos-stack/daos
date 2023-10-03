"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from dfuse_test_base import DfuseTestBase
from mdtest_utils import MdtestCommand
from exception_utils import CommandFailure
from job_manager_utils import get_job_manager


class MdtestBase(DfuseTestBase):
    """Base mdtest class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a MdtestBase object."""
        super().__init__(*args, **kwargs)
        self.mdtest_cmd = None
        self.processes = None
        self.ppn = None
        self.hostfile_clients_slots = None
        self.subprocess = False

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()
        # Start the servers and agents
        super().setUp()

        # Get the parameters for Mdtest
        self.mdtest_cmd = MdtestCommand()
        self.mdtest_cmd.get_params(self)
        self.ppn = self.params.get("ppn", '/run/mdtest/client_processes/*')
        self.processes = self.params.get("np", '/run/mdtest/client_processes/*')
        self.manager = self.params.get("manager", self.mdtest_cmd.namespace, "MPICH")
        self.subprocess = self.params.get("subprocess", self.mdtest_cmd.namespace, False)

        self.log.info('Clients %s', self.hostlist_clients)
        self.log.info('Servers %s', self.hostlist_servers)

    def get_mdtest_container(self, pool):
        """Create a container to use with mdtest.

        Args:
            pool (TestPool): pool to create container in

        Returns:
            TestContainer: the new container
        """
        params = {}
        if self.mdtest_cmd.dfs_oclass.value:
            params['oclass'] = self.mdtest_cmd.dfs_oclass.value
        if self.mdtest_cmd.dfs_dir_oclass.value:
            params['dir_oclass'] = self.mdtest_cmd.dfs_dir_oclass.value
        return self.get_container(pool, **params)

    def execute_mdtest(self, out_queue=None, display_space=True):
        """Runner method for Mdtest.

        Args:
            out_queue (queue, optional): Pass any exceptions in a queue. Defaults to None.
            display_space (bool, optional): Whether to display the pool space. Defaults to True.
        """
        # Create a pool if one does not already exist
        if self.pool is None:
            self.add_pool(connect=False)
        # create container
        if self.container is None:
            self.container = self.get_mdtest_container(self.pool)
        # set Mdtest params
        self.mdtest_cmd.set_daos_params(self.server_group, self.pool, self.container.identifier)

        # start dfuse if api is POSIX
        if self.mdtest_cmd.api.value == "POSIX":
            self.start_dfuse(self.hostlist_clients, self.pool, self.container)
            self.mdtest_cmd.test_dir.update(self.dfuse.mount_dir.value)

        # Run Mdtest
        self.run_mdtest(self.get_mdtest_job_manager_command(self.manager),
                        self.processes, display_space=display_space, out_queue=out_queue)

        if self.subprocess:
            return

        # reset self.container if dfs_destroy is True or None.
        if self.mdtest_cmd.dfs_destroy is not False:
            self.container = None
        self.stop_dfuse()

    def get_mdtest_job_manager_command(self, mpi_type):
        """Get the MPI job manager command for Mdtest.

        Returns:
            JobManager: the object for the mpi job manager command

        """
        # Initialize MpioUtils if mdtest needs to be run using mpich
        if mpi_type == "MPICH":
            manager = get_job_manager(
                self, "Mpirun", self.mdtest_cmd, self.subprocess)
        else:
            manager = get_job_manager(self, "Orterun", self.mdtest_cmd, self.subprocess)
        return manager

    def run_mdtest(self, manager, processes, display_space=True, pool=None, out_queue=None):
        """Run the Mdtest command.

        Args:
            manager (str): mpi job manager command
            processes (int): number of host processes
            display_space (bool, optional): Whether to display the pool space. Defaults to True.
            pool (TestPool, optional): The pool for which to display space. Defaults to self.pool.
            out_queue (queue, optional): Pass any exceptions in a queue. Defaults to None.

        Returns:
            object: result of job manager run
        """
        env = self.mdtest_cmd.get_default_env(str(manager), self.client_log)
        manager.assign_hosts(self.hostlist_clients, self.workdir, self.hostfile_clients_slots)
        if self.ppn is None:
            manager.assign_processes(processes)
        else:
            manager.ppn.update(self.ppn, 'mpirun.ppn')
            manager.processes.update(None, 'mpirun.np')

        manager.assign_environment(env)

        if not pool:
            pool = self.pool

        try:
            if display_space:
                pool.display_pool_daos_space()
            out = manager.run()

            return out
        except CommandFailure as error:
            self.log.error("Mdtest Failed: %s", str(error))
            # Queue is used when we use a thread to call
            # mdtest thread (eg: thread1 --> thread2 --> mdtest)
            if out_queue is not None:
                out_queue.put("Mdtest Failed")
            self.fail("Test was expected to pass but it failed.\n")
        finally:
            if not self.subprocess and display_space:
                pool.display_pool_daos_space()

        return None

    def run_mdtest_multiple_variants(self, mdtest_params):
        """Running mdtest different variants of mdtest with
           different values.
        Args:
            mdtest_params(list): List comprising of different set of mdtest parameters.
        """

        # Running mdtest for different variants
        for params in mdtest_params:
            # update mdtest params
            self.mdtest_cmd.api.update(params[0])
            self.mdtest_cmd.write_bytes.update(params[1])
            self.mdtest_cmd.read_bytes.update(params[2])
            self.mdtest_cmd.branching_factor.update(params[3])
            # if branching factor is 1 use num_of_files_dirs
            # else use items option of mdtest
            if params[3] == 1:
                self.mdtest_cmd.num_of_files_dirs.update(params[4])
            else:
                self.mdtest_cmd.items.update(params[4])
            self.mdtest_cmd.depth.update(params[5])
            self.mdtest_cmd.flags.update(params[6])
            if len(params) == 8 and params[7]:
                self.mdtest_cmd.env['LD_PRELOAD'] = os.path.join(
                    self.prefix, 'lib64', 'libpil4dfs.so')
            # run mdtest
            self.execute_mdtest()
            if len(params) == 8 and params[7]:
                del self.mdtest_cmd.env['LD_PRELOAD']
            # re-set mdtest params before next iteration
            self.mdtest_cmd.get_params(self)
