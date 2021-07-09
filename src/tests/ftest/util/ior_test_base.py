#!/usr/bin/python
"""
(C) Copyright 2018-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import threading

from ClusterShell.NodeSet import NodeSet

from dfuse_test_base import DfuseTestBase
from ior_utils import IorCommand
from command_utils_base import CommandFailure
from job_manager_utils import Mpirun
from general_utils import pcmd
from daos_utils import DaosCommand
from mpio_utils import MpioUtils
from test_utils_container import TestContainer


class IorTestBase(DfuseTestBase):
    # pylint: disable=too-many-ancestors
    """Base IOR test class.

    :avocado: recursive
    """

    IOR_WRITE_PATTERN = "Commencing write performance test"
    IOR_READ_PATTERN = "Commencing read performance test"

    def __init__(self, *args, **kwargs):
        """Initialize a IorTestBase object."""
        super().__init__(*args, **kwargs)
        self.ior_cmd = None
        self.processes = None
        self.hostfile_clients_slots = None
        self.container = None

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()
        # Start the servers and agents
        super().setUp()

        # Get the parameters for IOR
        self.ior_cmd = IorCommand()
        self.ior_cmd.get_params(self)
        self.processes = self.params.get("np", '/run/ior/client_processes/*')
        self.subprocess = self.params.get("subprocess", '/run/ior/*', False)

    def create_pool(self):
        """Create a TestPool object to use with ior."""
        # Get the pool params and create a pool
        self.add_pool(connect=False)

    def create_cont(self):
        """Create a TestContainer object to be used to create container.

        """
        # Get container params
        self.container = TestContainer(
            self.pool, daos_command=DaosCommand(self.bin))
        self.container.get_params(self)

        # create container
        self.container.create()

    def display_pool_space(self, pool=None):
        """Display the current pool space.

        If the TestPool object has a DmgCommand object assigned, also display
        the free pool space per target.

        Args:
            pool (TestPool, optional): The pool for which to display space.
                    Default is self.pool.
        """
        if not pool:
            pool = self.pool

        pool.display_pool_daos_space()
        if pool.dmg:
            pool.set_query_data()

    def run_ior_with_pool(self, intercept=None, test_file_suffix="",
                          test_file="daos:testFile", create_pool=True,
                          create_cont=True, stop_dfuse=True, plugin_path=None,
                          timeout=None, fail_on_warning=False,
                          mount_dir=None, out_queue=None):
        # pylint: disable=too-many-arguments
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
            plugin_path (str, optional): HDF5 vol connector library path.
                This will enable dfuse (xattr) working directory which is
                needed to run vol connector for DAOS. Default is None.
            timeout (int, optional): command timeout. Defaults to None.
            fail_on_warning (bool, optional): Controls whether the test
                should fail if a 'WARNING' is found. Default is False.
            mount_dir (str, optional): Create specific mount point
            out_queue (queue, optional): Pass the exception to the queue.
                Defaults to None

        Returns:
            CmdResult: result of the ior command execution

        """
        if create_pool:
            self.update_ior_cmd_with_pool(create_cont)

        # start dfuse if api is POSIX or HDF5 with vol connector
        if self.ior_cmd.api.value == "POSIX" or plugin_path:
            # Connect to the pool, create container and then start dfuse
            if not self.dfuse:
                self.start_dfuse(
                    self.hostlist_clients, self.pool, self.container, mount_dir)

        # setup test file for POSIX or HDF5 with vol connector
        if self.ior_cmd.api.value == "POSIX" or plugin_path:
            test_file = os.path.join(self.dfuse.mount_dir.value, "testfile")
        elif self.ior_cmd.api.value == "DFS":
            test_file = os.path.join("/", "testfile")

        self.ior_cmd.test_file.update("".join([test_file, test_file_suffix]))
        job_manager = self.get_ior_job_manager_command()
        job_manager.timeout = timeout
        try:
            out = self.run_ior(job_manager, self.processes,
                               intercept, plugin_path=plugin_path,
                               fail_on_warning=fail_on_warning,
                               out_queue=out_queue)
        finally:
            if stop_dfuse:
                self.stop_dfuse()

        return out

    def update_ior_cmd_with_pool(self, create_cont=True):
        """Update ior_cmd with pool.

        Args:
          create_cont (bool, optional): create a container. Defaults to True.
        """
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

    def get_ior_job_manager_command(self, custom_ior_cmd=None):
        """Get the MPI job manager command for IOR.

        Args:
            custom_ior_cmd (IorCommand): Custom IorCommand instance to create
            job_manager with.

        Returns:
            str: the path for the mpi job manager command

        """
        # Initialize MpioUtils if IOR is running in MPIIO or DFS mode
        if self.ior_cmd.api.value in ["MPIIO", "POSIX", "DFS", "HDF5"]:
            mpio_util = MpioUtils()
            if mpio_util.mpich_installed(self.hostlist_clients) is False:
                self.fail("Exiting Test: Mpich not installed")
        else:
            self.fail("Unsupported IOR API")

        if custom_ior_cmd:
            self.job_manager = Mpirun(custom_ior_cmd, self.subprocess, "mpich")
        else:
            self.job_manager = Mpirun(self.ior_cmd, self.subprocess, "mpich")

        return self.job_manager

    def check_subprocess_status(self, operation="write"):
        """Check subprocess status."""
        if operation == "write":
            self.ior_cmd.pattern = self.IOR_WRITE_PATTERN
        elif operation == "read":
            self.ior_cmd.pattern = self.IOR_READ_PATTERN
        else:
            self.fail("Exiting Test: Inappropriate operation type \
                      for subprocess status check")

        if not self.ior_cmd.check_ior_subprocess_status(
                self.job_manager.process, self.ior_cmd):
            self.fail("Exiting Test: Subprocess not running")

    def run_ior(self, manager, processes, intercept=None, display_space=True,
                plugin_path=None, fail_on_warning=False, pool=None,
                out_queue=None):
        """Run the IOR command.

        Args:
            manager (str): mpi job manager command
            processes (int): number of host processes
            intercept (str, optional): path to interception library.
            display_space (bool, optional): Whether to display the pool
                space. Defaults to True.
            plugin_path (str, optional): HDF5 vol connector library path.
                This will enable dfuse (xattr) working directory which is
                needed to run vol connector for DAOS. Default is None.
            fail_on_warning (bool, optional): Controls whether the test
                should fail if a 'WARNING' is found. Default is False.
            pool (TestPool, optional): The pool for which to display space.
                Default is self.pool.
            out_queue (queue, optional): Pass the exception to the queue.
                Defaults to None.
        """
        env = self.ior_cmd.get_default_env(str(manager), self.client_log)
        if intercept:
            env['LD_PRELOAD'] = intercept
            env['D_LOG_MASK'] = 'INFO'
            env['D_IL_REPORT'] = '1'
            #env['D_LOG_MASK'] = 'INFO,IL=DEBUG'
            #env['DD_MASK'] = 'all'
            #env['DD_SUBSYS'] = 'all'
        if plugin_path:
            env["HDF5_VOL_CONNECTOR"] = "daos"
            env["HDF5_PLUGIN_PATH"] = str(plugin_path)
            manager.working_dir.value = self.dfuse.mount_dir.value
        manager.assign_hosts(
            self.hostlist_clients, self.workdir, self.hostfile_clients_slots)
        manager.assign_processes(processes)
        manager.assign_environment(env)

        if not pool:
            pool = self.pool

        try:
            if display_space:
                self.display_pool_space(pool)
            out = manager.run()

            if self.subprocess:
                return out

            if fail_on_warning:
                report_warning = self.fail
            else:
                report_warning = self.log.warning

            for line in out.stdout_text.splitlines():
                if 'WARNING' in line:
                    report_warning("IOR command issued warnings.\n")
            return out
        except CommandFailure as error:
            self.log.error("IOR Failed: %s", str(error))
            # Queue is used when we use a thread to call
            # ior thread (eg: thread1 --> thread2 --> ior)
            if out_queue is not None:
                out_queue.put("IOR Failed")
            self.fail("Test was expected to pass but it failed.\n")
        finally:
            if not self.subprocess and display_space:
                self.display_pool_space(pool)

    def stop_ior(self):
        """Stop IOR process.

        Args:
            manager (str): mpi job manager command
        """
        self.log.info("<IOR> Stopping in-progress IOR command: %s",
                      str(self.job_manager))

        try:
            out = self.job_manager.stop()
            return out
        except CommandFailure as error:
            self.log.error("IOR stop Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")
        finally:
            self.display_pool_space()

    def run_ior_threads_il(self, results, intercept, with_clients,
                           without_clients):
        """Execute 2 IOR threads in parallel. One thread with interception
        library (IL) and one without.

        Args:
            results (dict): Dictionary to store the IOR results that gets
                printed in the IOR output.
            intercept (str): Path to the interception library. Shall be used
                only for POSIX through DFUSE.
            with_clients (list): List of clients that use IL.
            without_clients (list): List of clients that doesn't use IL.
        """
        # We can't use the shared self.ior_cmd, so we need to create the
        # IorCommand object for each thread.
        ior_cmd1 = IorCommand()
        ior_cmd1.get_params(self)
        # Update IOR params with the pool and container params
        ior_cmd1.set_daos_params(
            self.server_group, self.pool, self.container.uuid)

        ior_cmd2 = IorCommand()
        ior_cmd2.get_params(self)
        ior_cmd2.set_daos_params(
            self.server_group, self.pool, self.container.uuid)

        # start dfuse for POSIX api. This is specific to interception library
        # test requirements.
        self.start_dfuse(self.hostlist_clients, self.pool, self.container)

        # Create two threads and run in parallel.
        thread1 = self.create_ior_thread(
            ior_cmd1, with_clients, 1, results, intercept)
        thread2 = self.create_ior_thread(
            ior_cmd2, without_clients, 2, results, None)

        thread1.start()
        thread2.start()
        thread1.join()
        thread2.join()

        self.stop_dfuse()

    def create_ior_thread(self, ior_command, clients, job_num, results,
                          intercept=None):
        """Create a new thread for ior run.

        Args:
            ior_command (IorCommand): IOR command instance.
            clients (list): hosts on which to run ior
            job_num (int): Assigned job number
            results (dict): A dictionary object to store the ior metrics
            intercept (path): Path to interception library
        """
        job = threading.Thread(
            target=self.run_custom_ior_cmd,
            args=[ior_command, clients, results, job_num, intercept])
        return job

    def run_custom_ior_cmd(self, ior_command, clients, results, job_num,
                           intercept=None):
        """Run customized IOR command, not self.ior_cmd.

        Expected to be used with a threaded code where multiple IOR commands are
        executed in parallel.

        Display pool space before running it for a reference.

        Args:
            ior_command (IorCommand): Custom IOR command instance.
            clients (list): hosts on which to run ior
            results (dict): A dictionary object to store the ior metrics
            job_num (int): Assigned job number
            intercept (str, optional): path to interception library. Defaults to
                None.
        """
        self.log.info("--- IOR Thread %d: Start ---", job_num)
        tsize = ior_command.transfer_size.value
        testfile = os.path.join(
            self.dfuse.mount_dir.value, "testfile{}{}".format(tsize, job_num))
        if intercept:
            testfile += "intercept"
        ior_command.test_file.update(testfile)

        # Get the custom job manager that's associated with this thread.
        manager = self.get_ior_job_manager_command(custom_ior_cmd=ior_command)

        procs = (self.processes // len(self.hostlist_clients)) * len(clients)
        env = ior_command.get_default_env(str(manager), self.client_log)
        if intercept:
            env["LD_PRELOAD"] = intercept
        manager.assign_hosts(
            clients, self.workdir, self.hostfile_clients_slots)
        manager.assign_processes(procs)
        manager.assign_environment(env)
        self.display_pool_space()

        self.log.info("--- IOR Thread %d: Starting IOR ---", job_num)
        try:
            ior_output = manager.run()
            results[job_num] = IorCommand.get_ior_metrics(ior_output)
        except CommandFailure as error:
            self.log.error("IOR Failed: %s", str(error))
            self.fail("IOR thread failed!")
        finally:
            self.display_pool_space()

        self.log.info("--- IOR Thread %d: End ---", job_num)

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

    def execute_cmd(self, command, fail_on_err=True, display_output=True):
        """Execute cmd using general_utils.pcmd.

        Args:
            command (str): the command to execute on the client hosts
            fail_on_err (bool, optional): whether or not to fail the test if
                command returns a non zero return code. Defaults to True.
            display_output (bool, optional): whether or not to display output.
                Defaults to True.

        Returns:
            dict: a dictionary of return codes keys and accompanying NodeSet
                values indicating which hosts yielded the return code.

        """
        try:
            # Execute the bash command on each client host
            result = self._execute_command(command, fail_on_err, display_output)

        except CommandFailure as error:
            # Report an error if any command fails
            self.log.error("DfuseSparseFile Test Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")

        return result

    def _execute_command(self, command, fail_on_err=True, display_output=True):
        """Execute the command on all client hosts.

        Optionally verify if the command returns a non zero return code.

        Args:
            command (str): the command to execute on the client hosts
            fail_on_err (bool, optional): whether or not to fail the test if
                command returns a non zero return code. Defaults to True.
            display_output (bool, optional): whether or not to display output.
                Defaults to True.

        Raises:
            CommandFailure: if 'fail_on_err' is set and the command fails on at
                least one of the client hosts

        Returns:
            dict: a dictionary of return codes keys and accompanying NodeSet
                values indicating which hosts yielded the return code.

        """
        result = pcmd(
            self.hostlist_clients, command, verbose=display_output, timeout=300)
        if 0 not in result and fail_on_err:
            hosts = [str(
                nodes) for code, nodes in list(
                    result.items()) if code != 0]
            raise CommandFailure(
                "Error running '{}' on the following hosts: {}".format(
                    command, NodeSet(",".join(hosts))))
        return result
