#!/usr/bin/python3
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from dfuse_test_base import DfuseTestBase
from mpio_utils import MpioUtils
from mdtest_utils import MdtestCommand
from command_utils_base import CommandFailure
from job_manager_utils import Mpirun, Orterun


class MdtestBase(DfuseTestBase):
    # pylint: disable=too-many-ancestors
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
        self.manager = self.params.get("manager", '/run/mdtest/*', "MPICH")

        self.log.info('Clients %s', self.hostlist_clients)
        self.log.info('Servers %s', self.hostlist_servers)

    def execute_mdtest(self, out_queue=None):
        """Runner method for Mdtest.
        Args:
            out_queue (queue, optional): Pass any exceptions in a queue.
                                         Defaults to None.
        """
        # Create a pool if one does not already exist
        if self.pool is None:
            self.add_pool(connect=False)
        # create container
        if self.container is None:
            self.add_container(self.pool)
        # set Mdtest params
        self.mdtest_cmd.set_daos_params(self.server_group, self.pool,
                                        self.container.uuid)

        # start dfuse if api is POSIX
        if self.mdtest_cmd.api.value == "POSIX":
            self.start_dfuse(self.hostlist_clients, self.pool, self.container)
            self.mdtest_cmd.test_dir.update(self.dfuse.mount_dir.value)

        # Run Mdtest
        self.run_mdtest(self.get_mdtest_job_manager_command(self.manager),
                        self.processes, out_queue=out_queue)

        # reset self.container if dfs_destroy is True or None.
        if self.mdtest_cmd.dfs_destroy is not False:
            self.container = None
        self.stop_dfuse()

    def get_mdtest_job_manager_command(self, manager):
        """Get the MPI job manager command for Mdtest.

        Returns:
            JobManager: the object for the mpi job manager command

        """
        # pylint: disable=redefined-variable-type
        # Initialize MpioUtils if mdtest needs to be run using mpich
        if manager == "MPICH":
            mpio_util = MpioUtils()
            if mpio_util.mpich_installed(self.hostlist_clients) is False:
                self.fail("Exiting Test: Mpich not installed")
            self.job_manager = Mpirun(self.mdtest_cmd, mpitype="mpich")
        else:
            self.job_manager = Orterun(self.mdtest_cmd)

        return self.job_manager

    def run_mdtest(self, manager, processes, display_space=True, pool=None,
                   out_queue=None):
        """Run the Mdtest command.

        Args:
            manager (str): mpi job manager command
            processes (int): number of host processes
            display_space (bool, optional): Whether to display the pool
                space. Defaults to True.
            pool (TestPool, optional): The pool for which to display space.
                Default is self.pool.
        """
        env = self.mdtest_cmd.get_default_env(str(manager), self.client_log)
        manager.assign_hosts(
            self.hostlist_clients, self.workdir, self.hostfile_clients_slots)
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
            manager.run()
        except CommandFailure as error:
            self.log.error("Mdtest Failed: %s", str(error))
            # Queue is used when we use a thread to call
            # mdtest thread (eg: thread1 --> thread2 --> mdtest)
            if out_queue is not None:
                out_queue.put("Mdtest Failed")
            self.fail("Test was expected to pass but it failed.\n")
        finally:
            if display_space:
                pool.display_pool_daos_space()

    def run_mdtest_multiple_variants(self, mdtest_params):
        """Running mdtest different variants of mdtest with
           different values.
        Args:
            mdtest_params(list): List comprising of different set of
                                 mdtest parameters.
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
            # run mdtest
            self.execute_mdtest()
            # re-set mdtest params before next iteration
            self.mdtest_cmd.get_params(self)
