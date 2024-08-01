"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from exception_utils import CommandFailure
from job_manager_utils import get_job_manager
from mdtest_utils import MdtestCommand


class MdtestBase(TestWithServers):
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

        # We should not be using these as class level variables, but are needed until the
        # execute_mdtest() method can be redesigned to pass in these arguments instead of
        # optionally defining them
        self.pool = None
        self.container = None
        self.dfuse = None

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()
        # Start the servers and agents
        super().setUp()

        # Get the parameters for Mdtest
        # Makito added argument.
        self.mdtest_cmd = MdtestCommand(self.test_env.log_dir)
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

    def execute_mdtest(self, out_queue=None, display_space=True, job_manager=None,
                       intercept=None):
        """Runner method for Mdtest.

        Args:
            out_queue (queue, optional): Pass any exceptions in a queue. Defaults to None.
            display_space (bool, optional): Whether to display the pool space. Defaults to True.
            job_manager (JobManager, optional): job manager used to run mdtest. Defaults to None.
            intercept (default None): path to intercept library
 
        Returns:
            object: result of job manager run
        """
        # Create a pool if one does not already exist
        if self.pool is None:
            self.add_pool(connect=False)
        # create container
        if self.container is None:
            self.container = self.get_mdtest_container(self.pool)
        # set Mdtest params
        self.mdtest_cmd.update_params(
            dfs_pool=self.pool.identifier, dfs_cont=self.container.identifier)

        # start dfuse if api is POSIX
        if self.mdtest_cmd.api.value == "POSIX":
            self.dfuse = get_dfuse(self, self.hostlist_clients)
            start_dfuse(self, self.dfuse, self.pool, self.container)
            self.mdtest_cmd.test_dir.update(self.dfuse.mount_dir.value)

        # Create a job manager if one is not provided
        if job_manager is None:
            job_manager = self.get_mdtest_job_manager_command(self.manager)

        # Run Mdtest
        out = self.run_mdtest(
            job_manager, self.processes, display_space=display_space, out_queue=out_queue,
            intercept=intercept)

        if self.subprocess:
            return out

        # reset self.container if dfs_destroy is True or None.
        if self.mdtest_cmd.dfs_destroy.value is True:
            self.container.skip_cleanup()
            # Need to set self.container to None to force a creation of a new container
            self.container = None

        if self.dfuse is not None:
            self.dfuse.stop()
            self.dfuse = None

        return out

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

    def run_mdtest(self, manager, processes, display_space=True, pool=None, out_queue=None,
                   intercept=None):
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
        # Pass only processes or ppn to be compatible with previous behavior
        if intercept:
            env['LD_PRELOAD'] = intercept
            if 'D_IL_REPORT' not in env:
                env['D_IL_REPORT'] = '1'
        if self.ppn is not None:
            manager.assign_processes(ppn=self.ppn)
        else:
            manager.assign_processes(processes=processes)

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
