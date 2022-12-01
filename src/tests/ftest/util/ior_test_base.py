"""
(C) Copyright 2018-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from ClusterShell.NodeSet import NodeSet

from dfuse_test_base import DfuseTestBase
from ior_utils import IorCommand
from exception_utils import CommandFailure
from job_manager_utils import get_job_manager
from general_utils import pcmd, get_random_string


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
        self.ior_timeout = None
        self.ppn = None

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
        self.ppn = self.params.get("ppn", '/run/ior/client_processes/*')
        self.subprocess = self.params.get("subprocess", '/run/ior/*', False)
        self.ior_timeout = self.params.get("ior_timeout", '/run/ior/*', None)

    def create_pool(self):
        """Create a TestPool object to use with ior."""
        # Get the pool params and create a pool
        self.add_pool(connect=False)

    def create_cont(self):
        """Create a TestContainer object to be used to create container.

        Returns:
            TestContainer: the created container.

        """
        params = {}

        # Set container oclass to match ior oclass
        if self.ior_cmd.dfs_oclass:
            params["oclass"] = self.ior_cmd.dfs_oclass.value

        # create container from params
        self.container = self.get_container(self.pool, **params)

        return self.container

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

    def run_ior_with_pool(self, intercept=None, display_space=True, test_file_suffix="",
                          test_file="daos:/testFile", create_pool=True,
                          create_cont=True, stop_dfuse=True, plugin_path=None,
                          timeout=None, fail_on_warning=False,
                          mount_dir=None, out_queue=None, env=None):
        # pylint: disable=too-many-arguments
        """Execute ior with optional overrides for ior flags and object_class.

        If specified the ior flags and ior daos object class parameters will
        override the values read from the yaml file.

        Args:
            intercept (str, optional): path to the interception library. Shall
                be used only for POSIX through DFUSE. Defaults to None.
            display_space (bool, optional): Whether to display the pool
                space. Defaults to True.
            test_file_suffix (str, optional): suffix to add to the end of the
                test file name. Defaults to "".
            test_file (str, optional): ior test file name. Defaults to
                "daos:/testFile". Is ignored when using POSIX through DFUSE.
            create_pool (bool, optional): If it is true, create pool and
                container else just run the ior. Defaults to True.
            create_cont (bool, optional): Create new container. Default is True
            stop_dfuse (bool, optional): Stop dfuse after ior command is
                finished. Default is True.
            plugin_path (str, optional): HDF5 vol connector library path.
                This will enable dfuse (xattr) working directory which is
                needed to run vol connector for DAOS. Default is None.
            timeout (int, optional): command timeout. Defaults to None.
            fail_on_warning (bool/callable, optional): Controls test behavior when a 'WARNING' is
                found. If True, call self.fail. If False, call self.log.warn. If callable, call it.
                Default is False.
            mount_dir (str, optional): Create specific mount point
            out_queue (queue, optional): Pass the exception to the queue.
                Defaults to None
            env (EnvironmentVariables, optional): Pass the environment to be
                used when calling run_ior. Defaults to None

        Returns:
            CmdResult: result of the ior command execution

        """
        if create_pool:
            self.update_ior_cmd_with_pool(create_cont)

        # start dfuse if api is POSIX or HDF5 with vol connector
        if self.ior_cmd.api.value == "POSIX" or plugin_path:
            # add a substring in case of HDF5-VOL
            if plugin_path:
                sub_dir = get_random_string(5)
                mount_dir = os.path.join(mount_dir, sub_dir)
            # Connect to the pool, create container and then start dfuse
            if not self.dfuse:
                params = {'mount_dir': mount_dir} if mount_dir else {}
                self.start_dfuse(self.hostlist_clients, self.pool, self.container, **params)

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
                               intercept=intercept,
                               display_space=display_space, plugin_path=plugin_path,
                               fail_on_warning=fail_on_warning,
                               out_queue=out_queue, env=env)
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

    def get_ior_job_manager_command(self):
        """Get the MPI job manager command for IOR.

        Returns:
            JobManager: the mpi job manager object

        """
        return get_job_manager(self, job=self.ior_cmd, subprocess=self.subprocess)

    def check_subprocess_status(self, operation="write"):
        """Check subprocess status."""
        if operation == "write":
            self.ior_cmd.pattern = self.IOR_WRITE_PATTERN
        elif operation == "read":
            self.ior_cmd.pattern = self.IOR_READ_PATTERN
        else:
            self.fail("Exiting Test: Inappropriate operation type for subprocess status check")

        if not self.ior_cmd.check_subprocess_status(self.job_manager.process):
            self.fail("IOR subprocess not running")

    def run_ior(self, manager, processes, intercept=None, display_space=True,
                plugin_path=None, fail_on_warning=False, pool=None,
                out_queue=None, env=None):
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
            fail_on_warning (bool/callable, optional): Controls test behavior when a 'WARNING' is
                found. If True, call self.fail. If False, call self.log.warn. If callable, call it.
                Defaults is False.
            pool (TestPool, optional): The pool for which to display space.
                Default is self.pool.
            out_queue (queue, optional): Pass the exception to the queue.
                Defaults to None.
            env (EnvironmentVariables, optional): Environment to be used
             when running ior. Defaults to None
        """
        if not env:
            env = self.ior_cmd.get_default_env(str(manager), self.client_log)
        if intercept:
            env['LD_PRELOAD'] = intercept
            if 'D_IL_REPORT' not in env:
                env['D_IL_REPORT'] = '1'
        if plugin_path:
            env["HDF5_VOL_CONNECTOR"] = "daos"
            env["HDF5_PLUGIN_PATH"] = str(plugin_path)
            manager.working_dir.value = self.dfuse.mount_dir.value
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
                self.display_pool_space(pool)
            out = manager.run()

            if self.subprocess:
                return out

            if callable(fail_on_warning):
                report_warning = fail_on_warning
            elif fail_on_warning:
                report_warning = self.fail
            else:
                report_warning = self.log.warning

            for line in out.stdout_text.splitlines():
                if 'WARNING' in line:
                    report_warning("IOR command issued warnings.")
            return out
        except CommandFailure as error:
            self.log.error("IOR Failed: %s", str(error))
            # Queue is used when we use a thread to call
            # ior thread (eg: thread1 --> thread2 --> ior)
            if out_queue is not None:
                out_queue.put("IOR Failed")
            self.fail("IOR Failed")
        finally:
            if not self.subprocess and display_space:
                self.display_pool_space(pool)

        return None

    def stop_ior(self):
        """Stop IOR process.

        Args:
            manager (str): mpi job manager command

        Returns:
            Object: result of job manager stop
        """
        self.log.info("<IOR> Stopping in-progress IOR command: %s", str(self.job_manager))

        try:
            return self.job_manager.stop()
        except CommandFailure as error:
            self.log.error("IOR stop Failed: %s", str(error))
            self.fail("Failed to stop in-progress IOR command")
        finally:
            self.display_pool_space()

        return None

    def run_ior_multiple_variants(self, obj_class, apis, transfer_block_size,
                                  flags, mount_dir):
        """Run multiple ior commands with various different combination
           of ior input params.

        Args:
            obj_class(list): List of different object classes
            apis(list): list of different apis
            transfer_block_size(list): list of different transfer sizes
                                       and block sizes. eg: [1M, 32M]
                                       1M is transfer size and 32M is
                                       block size in the above example.
            flags(list): list of ior flags. Only the first index is used
            mount_dir(str): dfuse mount directory
        """
        results = []

        for oclass in obj_class:
            self.ior_cmd.dfs_oclass.update(oclass)
            for api in apis:
                hdf5_plugin_path = None
                intercept = None
                flags_w = flags[0]
                if api == "HDF5-VOL":
                    api = "HDF5"
                    hdf5_plugin_path = self.params.get("plugin_path", '/run/hdf5_vol/*')
                    flags_w += " -k"
                elif api == "POSIX+IL":
                    api = "POSIX"
                    intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
                self.ior_cmd.flags.update(flags_w, "ior.flags")
                self.ior_cmd.api.update(api)
                for test in transfer_block_size:
                    # update transfer and block size
                    self.ior_cmd.transfer_size.update(test[0])
                    self.ior_cmd.block_size.update(test[1])
                    # run ior
                    try:
                        self.run_ior_with_pool(
                            intercept=intercept, plugin_path=hdf5_plugin_path,
                            timeout=self.ior_timeout, mount_dir=mount_dir)
                        results.append(["PASS", str(self.ior_cmd)])
                    except CommandFailure:
                        results.append(["FAIL", str(self.ior_cmd)])
        return results

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
            self.log.error("Failed to execute command: %s", str(error))
            self.fail("Failed to execute command")

        return result

    def _execute_command(self, command, fail_on_err=True, display_output=True, hosts=None):
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
        if hosts is None:
            hosts = self.hostlist_clients
        result = pcmd(hosts, command, verbose=display_output, timeout=300)
        if 0 not in result and fail_on_err:
            hosts = [str(
                nodes) for code, nodes in list(
                    result.items()) if code != 0]
            raise CommandFailure(
                "Error running '{}' on the following hosts: {}".format(
                    command, NodeSet(",".join(hosts))))
        return result
