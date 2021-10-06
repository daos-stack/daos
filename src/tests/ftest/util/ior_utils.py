#!/usr/bin/python
"""
(C) Copyright 2018-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import re
import uuid
import time
from enum import IntEnum

from command_utils_base import CommandFailure, FormattedParameter, BasicParameter
from command_utils import ExecutableCommand
from dfuse_utils import Dfuse
from general_utils import get_subprocess_stdout, get_random_string


class IorCommand(ExecutableCommand):
    # pylint: disable=too-many-instance-attributes
    """Defines a object for executing an IOR command.

    Example:
        >>> # Typical use inside of a DAOS avocado test method.
        >>> ior_cmd = IorCommand()
        >>> ior_cmd.get_params(self)
        >>> ior_cmd.set_daos_params(self.server_group, self.pool)
        >>> mpirun = Mpirun()
        >>> server_manager = self.server_manager[0]
        >>> env = self.ior_cmd.get_environment(server_manager, self.client_log)
        >>> mpirun.assign_hosts(self.hostlist_clients, self.workdir, None)
        >>> mpirun.assign_processes(len(self.hostlist_clients))
        >>> mpirun.assign_environment(env)
        >>> mpirun.run()
    """

    def __init__(self, namespace=None):
        """Create an IorCommand object.

        Args:
            namespace (str, optional): yaml namespace (path to parameters). Defaults to None.
        """
        if namespace is None:
            namespace = "/run/ior/*"
        super().__init__(namespace, "ior")

        # Flags
        self.flags = FormattedParameter("{}")

        # Optional arguments
        #   -a=POSIX        API for I/O [POSIX|DUMMY|MPIIO|MMAP|DFS|HDF5]
        #   -b=1048576      blockSize -- contiguous bytes to write per task
        #   -d=0            interTestDelay -- delay between reps in seconds
        #   -f=STRING       scriptFile -- test script name
        #   -G=0            setTimeStampSignature -- time stamp signature
        #   -i=1            repetitions -- number of repetitions of test
        #   -j=0            outlierThreshold -- warn on outlier N sec from mean
        #   -J=1            setAlignment -- HDF5 alignment in bytes
        #   -l=STRING       datapacket type-- type of packet created
        #   -M=STRING       memoryPerNode -- hog memory on the node
        #   -N=0            numTasks -- num of participating tasks in the test
        #   -o=testFile     testFile -- full name for test
        #   -O=STRING       string of IOR directives
        #   -O=1            stoneWallingWearOut -- all process finish to access
        #                       the amount of data after stonewalling timeout
        #   -O=0            stoneWallingWearOutIterations -- stop after
        #                       processing this number of iterations
        #   -O=STRING       stoneWallingStatusFile -- file to keep number of
        #                      iterations from stonewalling during write
        #   -Q=1            taskPerNodeOffset for read tests
        #   -s=1            segmentCount -- number of segments
        #   -t=262144       transferSize -- size of transfer in bytes
        #   -T=0            maxTimeDuration -- max time in minutes executing
        #                      repeated test; it aborts only between iterations
        #                      and not within a test!
        self.api = FormattedParameter("-a {}", "DFS")
        self.block_size = FormattedParameter("-b {}")
        self.test_delay = FormattedParameter("-d {}")
        self.script = FormattedParameter("-f {}")
        self.signature = FormattedParameter("-G {}")
        self.repetitions = FormattedParameter("-i {}")
        self.outlier_threshold = FormattedParameter("-j {}")
        self.alignment = FormattedParameter("-J {}")
        self.data_packet_type = FormattedParameter("-l {}")
        self.memory_per_node = FormattedParameter("-M {}")
        self.num_tasks = FormattedParameter("-N {}")
        self.test_file = FormattedParameter("-o {}")
        self.directives = FormattedParameter("-O {}")
        self.sw_wearout = FormattedParameter(
            "-O stoneWallingWearOut={}")
        self.sw_wearout_iteration = FormattedParameter(
            "-O stoneWallingWearOutIterations={}")
        self.sw_status_file = FormattedParameter(
            "-O stoneWallingStatusFile={}")
        self.task_offset = FormattedParameter("-Q {}")
        self.segment_count = FormattedParameter("-s {}")
        self.transfer_size = FormattedParameter("-t {}")
        self.max_duration = FormattedParameter("-T {}")

        # Module DFS
        #   Required arguments
        #       --dfs.pool=STRING            pool uuid
        #       --dfs.cont=STRING            container uuid
        #   Flags
        #       --dfs.destroy               Destroy Container
        #   Optional arguments
        #       --dfs.group=STRING           server group
        #       --dfs.chunk_size=1048576     chunk size
        #       --dfs.oclass=STRING          object class
        #       --dfs.prefix=STRING          mount prefix
        self.dfs_pool = FormattedParameter("--dfs.pool {}")
        self.dfs_cont = FormattedParameter("--dfs.cont {}")
        self.dfs_destroy = FormattedParameter("--dfs.destroy", False)
        self.dfs_group = FormattedParameter("--dfs.group {}")
        self.dfs_chunk = FormattedParameter("--dfs.chunk_size {}", 1048576)
        self.dfs_oclass = FormattedParameter("--dfs.oclass {}", "SX")
        self.dfs_dir_oclass = FormattedParameter("--dfs.dir_oclass {}", "SX")
        self.dfs_prefix = FormattedParameter("--dfs.prefix {}")

        # A list of environment variable names to set and export with ior
        self._env_names = ["D_LOG_FILE"]

        # Attributes used to determine command success when run as a subprocess
        # See self.check_ior_subprocess_status() for details.
        self.pattern = None
        self.pattern_count = 1

    def get_param_names(self):
        """Get a sorted list of the defined IorCommand parameters."""
        # Sort the IOR parameter names to generate consistent ior commands
        all_param_names = super().get_param_names()

        # List all of the common ior params first followed by any daos-specific
        # and dfs-specific params (except when using MPIIO).
        param_names = [name for name in all_param_names if ("daos" not in name)
                       and ("dfs" not in name)]

        if self.api.value == "DFS":
            param_names.extend(
                [name for name in all_param_names if "dfs" in name])

        return param_names

    def set_daos_params(self, group, pool, cont_uuid=None, display=True):
        """Set the IOR parameters for the DAOS group, pool, and container uuid.

        Args:
            group (str): DAOS server group name
            pool (TestPool): DAOS test pool object
            cont_uuid (str, optional): the container uuid. If not specified one
                is generated. Defaults to None.
            display (bool, optional): print updated params. Defaults to True.
        """
        self.set_daos_pool_params(pool, display)
        if self.api.value in ["DFS", "MPIIO", "POSIX", "HDF5"]:
            self.dfs_group.update(group, "dfs_group" if display else None)
            self.dfs_cont.update(
                cont_uuid if cont_uuid else str(uuid.uuid4()),
                "dfs_cont" if display else None)

    def set_daos_pool_params(self, pool, display=True):
        """Set the IOR parameters that are based on a DAOS pool.

        Args:
            pool (TestPool): DAOS test pool object
            display (bool, optional): print updated params. Defaults to True.
        """
        if self.api.value in ["DFS", "MPIIO", "POSIX", "HDF5"]:
            self.dfs_pool.update(
                pool.pool.get_uuid_str(), "dfs_pool" if display else None)

    def get_aggregate_total(self, processes):
        """Get the total bytes expected to be written by ior.

        Args:
            processes (int): number of processes running the ior command

        Returns:
            int: total number of bytes written

        Raises:
            CommandFailure: if there is an error obtaining the aggregate total

        """
        power = {"k": 1, "m": 2, "g": 3, "t": 4}
        total = processes
        for name in ("block_size", "segment_count"):
            item = getattr(self, name).value
            if item:
                sub_item = re.split(r"([^\d])", str(item))
                if int(sub_item[0]) > 0:
                    total *= int(sub_item[0])
                    if len(sub_item) > 1:
                        key = sub_item[1].lower()
                        if key in power:
                            total *= 1024**power[key]
                        else:
                            raise CommandFailure(
                                "Error obtaining the IOR aggregate total from "
                                "the {} - bad key: value: {}, split: {}, "
                                "key: {}".format(name, item, sub_item, key))
                else:
                    raise CommandFailure(
                        "Error obtaining the IOR aggregate total from the {}: "
                        "value: {}, split: {}".format(name, item, sub_item))

        # Account for any replicas, except for the ones with no replication
        # i.e all object classes starting with "S". Eg: S1,S2,...,SX.
        if not self.dfs_oclass.value.startswith("S"):
            try:
                # Extract the replica quantity from the object class string
                replica_qty = int(re.findall(r"\d+", self.dfs_oclass.value)[0])
            except (TypeError, IndexError):
                # If the daos object class is undefined (TypeError) or it does
                # not contain any numbers (IndexError) then there is only one
                # replica.
                replica_qty = 1
            finally:
                total *= replica_qty

        return total

    def get_default_env(self, manager_cmd, log_file=None):
        """Get the default environment settings for running IOR.

        Args:
            manager_cmd (str): job manager command
            log_file (str, optional): log file. Defaults to None.

        Returns:
            EnvironmentVariables: a dictionary of environment names and values

        """
        env = self.get_environment(None, log_file)
        env["MPI_LIB"] = "\"\""
        env["FI_PSM2_DISCONNECT"] = "1"

        # ior POSIX api does not require the below options.
        if "POSIX" in manager_cmd:
            return env

        if "mpirun" in manager_cmd or "srun" in manager_cmd:
            if self.dfs_pool.value is not None:
                env["DAOS_POOL"] = self.dfs_pool.value
                env["DAOS_CONT"] = self.dfs_cont.value
                env["DAOS_BYPASS_DUNS"] = "1"
                if self.dfs_oclass.value is not None:
                    env["IOR_HINT__MPI__romio_daos_obj_class"] = \
                        self.dfs_oclass.value
        return env

    @staticmethod
    def get_ior_metrics(cmdresult):
        """Get the ior command read and write metrics.

        Parse the CmdResult (output of the test) and look for the ior stdout
        and get the read and write metrics.

        Args:
            cmdresult (CmdResult): output of job manager

        Returns:
            metrics (tuple) : list of write and read metrics from ior run

        """
        ior_metric_summary = "Summary of all tests:"
        messages = cmdresult.stdout_text.splitlines()
        # Get the index where the summary starts and add one to
        # get to the header.
        idx = messages.index(ior_metric_summary)
        # idx + 1 is header.
        # idx +2 and idx + 3 will give the write and read metrics.
        write_metrics = (" ".join(messages[idx+2].split())).split()
        read_metrics = (" ".join(messages[idx+3].split())).split()

        return (write_metrics, read_metrics)

    @staticmethod
    def log_metrics(logger, message, metrics):
        """Log the ior metrics.

        Args:
            logger (log): logger object handle
            message (str) : Message to print before logging metrics
            metric (lst) : IOR write and read metrics
        """
        logger.info("\n")
        logger.info(message)
        for metric in metrics:
            logger.info(metric)
        logger.info("\n")

    def check_ior_subprocess_status(self, sub_process, command,
                                    pattern_timeout=10):
        """Verify the status of the command started as a subprocess.

        Continually search the subprocess output for a pattern (self.pattern)
        until the expected number of patterns (self.pattern_count) have been
        found (typically one per host) or the timeout (pattern_timeout)
        is reached or the process has stopped.

        Args:
            sub_process (process.SubProcess): subprocess used to run the command
            command (str): ior command being looked for
            pattern_timeout: (int): check pattern until this timeout limit is
                                    reached.
        Returns:
            bool: whether or not the command progress has been detected

        """
        complete = True
        self.log.info(
            "Checking status of the %s command in %s with a %s second timeout",
            command, sub_process, pattern_timeout)

        if self.pattern is not None:
            detected = 0
            complete = False
            timed_out = False
            start = time.time()

            # Search for patterns in the subprocess output until:
            #   - the expected number of pattern matches are detected (success)
            #   - the time out is reached (failure)
            #   - the subprocess is no longer running (failure)
            while not complete and not timed_out and sub_process.poll() is None:
                output = get_subprocess_stdout(sub_process)
                detected = len(re.findall(self.pattern, output))
                complete = detected == self.pattern_count
                timed_out = time.time() - start > pattern_timeout

            # Summarize results
            msg = "{}/{} '{}' messages detected in {}/{} seconds".format(
                detected, self.pattern_count, self.pattern,
                time.time() - start, pattern_timeout)

            if not complete:
                # Report the error / timeout
                self.log.info(
                    "%s detected - %s:\n%s",
                    "Time out" if timed_out else "Error",
                    msg,
                    get_subprocess_stdout(sub_process))

                # Stop the timed out process
                if timed_out:
                    self.stop()
            else:
                # Report the successful start
                self.log.info(
                    "%s subprocess startup detected - %s", command, msg)

        return complete


class IorMetrics(IntEnum):
    """Index Name and Number of each column in IOR result summary."""

    # Operation   Max(MiB)   Min(MiB)  Mean(MiB)     StdDev   Max(OPs)
    # Min(OPs)  Mean(OPs) StdDev    Mean(s) Stonewall(s) Stonewall(MiB)
    # Test# #Tasks tPN reps fPP reord reordoff reordrand seed segcnt
    # blksiz    xsize aggs(MiB)   API RefNum
    Operation = 0
    Max_MiB = 1
    Min_MiB = 2
    Mean_MiB = 3
    StdDev_MiB = 4
    Max_OPs = 5
    Min_OPs = 6
    Mean_OPs = 7
    StdDev_OPs = 8
    Mean_seconds = 9
    Stonewall_seconds = 10
    Stonewall_MiB = 11
    Test_No = 12
    Num_Tasks = 13
    tPN = 14
    reps = 15
    fPP = 16
    reord = 17
    reordoff = 18
    reordrand = 19
    seed = 20
    segcnt = 21
    blksiz = 22
    xsize = 23
    aggs_MiB = 24
    API = 25
    RefNum = 26


class Ior(IorCommand):
    """Run ior in parallel on multiple hosts using a JobManager class.

    Steps:
        1) Define a TestPool object and optionally a TestContainer object
        2) Define a JobManager object, e.g. Mpirun or Orterun, and call its assign_hosts() method to
           assign which hosts will run ior (also creates the hostfile)
        3) Initialize an Ior object with the JobManager, TestPool, and TestContainer objects
        4) Call Ior.get_params(Test)
        5) Call Ior.run()

    For running ior on multiple pools, multiple Ior objects can be created.

    For running ior threads the Ior.run() method can be used with the ThreadManager class.
    """

    def __init__(self, job_manager, group, pool, container=None, namespace=None):
        """Create an Ior object.

        Args:
            job_manager (JobManager): object to manage running the ior job
            group (str): DAOS server group name
            pool (TestPool): DAOS pool to test.
            container (TestContainer, optional): DAOS container to test. Defaults to None.
            namespace (str, optional): yaml namespace (path to parameters). Defaults to None.
        """
        super().__init__(namespace)
        self.job_manager = job_manager
        self.dfuse = Dfuse(self.job_manager.hosts)
        self.group = group
        self.pool = pool
        self.container = container

        self._display = BasicParameter(None, True, yaml_key="display")
        self._display_space = BasicParameter(None, True, yaml_key="display_space")
        self._plugin_path = BasicParameter(None, yaml_key="plugin_path")
        self._intercept = BasicParameter(None, yaml_key="intercept")
        self._fail_on_warning = BasicParameter(None, False, yaml_key="fail_on_warning")

    @property
    def display(self):
        """Get the option to display updated paramters."""
        return self._display.value

    @display.setter
    def display(self, value):
        """Set the option to display updated paramters."""
        if isinstance(value, bool):
            self.update_value(self._display, value, "display")

    @property
    def display_space(self):
        """Get the option to display pool space before and after running ior."""
        return self._display_space.value

    @display_space.setter
    def display_space(self, value):
        """Set the option to display pool space before and after running ior."""
        if isinstance(value, bool):
            self.update_value(self._display_space, value, "display_space")

    @property
    def plugin_path(self):
        """Get the plugin path option."""
        return self._plugin_path.value

    @plugin_path.setter
    def plugin_path(self, value):
        """Set the plugin path option."""
        self.update_value(self._plugin_path, value, "plugin_path")

    @property
    def intercept(self):
        """Get the intercept library option."""
        return self._intercept.value

    @intercept.setter
    def intercept(self, value):
        """Set the intercept library option."""
        self.update_value(self._intercept, value, "intercept")

    @property
    def fail_on_warning(self):
        """Get the option to fail the ior command if warnings are found in the output."""
        return self._fail_on_warning.value

    @fail_on_warning.setter
    def fail_on_warning(self, value):
        """Set the option to fail the ior command if warnings are found in the output."""
        if isinstance(value, bool):
            self.update_value(self._fail_on_warning, value, "fail_on_warning")

    def update_value(self, parameter, value, name):
        """Update a BasicParameter object's value and optionally log the update.

        Args:
            attribute (BasicParameter): object whose value is being updated
            value (object): the updated value to set
            name (str): the name of the object being updated
        """
        parameter.update(value, name if self.display.value else None)

    def get_str_param_names(self):
        """Get a sorted list of the names of the command attributes.

        Returns:
            list: a list of class attribute names used to define parameters for the command.

        """
        # Get only the parameter names that apply to the actual ior command string
        return self.get_attribute_names(FormattedParameter)

    def get_params(self, test):
        """Get values for the IOR command parameters from the test yaml file.

        Args:
            test (Test): avocado Test object
        """
        super().get_params(test)
        self.dfuse.get_params(test)
        self.set_daos_params(self.group, self.pool, self._get_container_uuid(), self.display)

        # Ensure a test file is set
        if self.test_file.value is None:
            self.update_value(self.test_file, "daos:testFile", "test_file")

    def display_pool_space(self):
        """Display the space information for the pool.

        If the TestPool object has a DmgCommand object assigned, also display
        the free pool space per target.
        """
        if self.pool and self.display_space:
            self.pool.display_pool_daos_space()
            if self.pool.dmg:
                self.pool.set_query_data()

    def _get_container_uuid(self):
        """Get the container UUID.

        Returns:
            str: the container UUID if defined; otherwise None

        """
        uuid = None
        if self.container:
            uuid = self.container.uuid
        return uuid

    def _setup_test_file(self, server_manager, log_file, test_file_suffix=None):
        """Configure the ior testfile argument.

        This method will also start dfuse if required and it is not already started.

        Args:
            server_manager (DaosServerManager): server manager for the running servers
            log_file (str): log file to use when assigning D_LOG_FILE
            test_file_suffix (str, optional): suffix to add to the end of the test file name.
                Defaults to None.
        """
        if self.api.value == "POSIX" or self.plugin_path:
            if self.plugin_path:
                # Add a substring in case of HDF5-VOL
                new_mount_dir = os.path.join(self.dfuse.mount_dir.value, get_random_string(5))
                self.update_value(self.dfuse.mount_dir, new_mount_dir, "dfuse.mount_dir")
            if not self.dfuse.started:
                # Start dfuse
                self.dfuse.start(server_manager, log_file, self.pool, self.container)
            self.update_value(
                self.test_file, os.path.join(self.dfuse.mount_dir.value, "testfile"), "test_file")
        elif self.api.value == "DFS":
            self.update_value(self.test_file, os.path.join("/", "testfile"), "test_file")

        # Add the suffix if specified
        if test_file_suffix is not None:
            self.update_value(
                self.test_file, "".join([self.test_file.value, test_file_suffix]), "test_file")

    def _setup_environment(self, env, log_file):
        """Set up the environment variables for the ior command.

        Args:
            env (EnvironmentVariables): existing environment variable definitions to extend; can be
                None.
            log_file (str): value to use with D_LOG_FILE if env is None; required if env is None.
        """
        if not env:
            env = self.get_default_env(str(self.job_manager), log_file)
        if self.intercept is not None:
            env["LD_PRELOAD"] = self.intercept
            env["D_LOG_MASK"] = "INFO"
            if env.get("D_IL_REPORT", None) is None:
                env["D_IL_REPORT"] = "1"
        if self.plugin_path is not None:
            env["HDF5_VOL_CONNECTOR"] = "daos"
            env["HDF5_PLUGIN_PATH"] = str(self.plugin_path)
            self.job_manager.working_dir.value = self.dfuse.mount_dir.value
        self.job_manager.assign_environment(env)

    def run(self, server_manager, log_file, processes=None, test_file_suffix=None, env=None):
        """Run ior via the job manager.

        Note: to use a timeout set self.job_manager.timeout prior to calling this method.

        Args:
            server_manager (DaosServerManager): server manager for the running servers
            log_file (str): log file to use when assigning D_LOG_FILE
            processes (int, optional): number of host processes. Defaults to None which will use the
                length of the JobManager.hosts list.
            test_file_suffix (str, optional): suffix to add to the end of the test file name.
                Defaults to None.
            env (EnvironmentVariables, optional): predefined environment variables to be used when
                running ior. Defaults to None.

        Returns:
            CmdResult: an avocado.utils.process CmdResult object containing the
                result of the command execution.  A CmdResult object has the
                following properties:
                    command         - command string
                    exit_status     - exit_status of the command
                    stdout          - the stdout
                    stdout_text     - decoded stdout
                    stderr          - the stderr
                    stderr_text     - decoded stderr
                    duration        - command execution time
                    interrupted     - whether the command completed within timeout
                    pid             - command's pid

        """
        if processes is None:
            processes = len(self.job_manager.hosts)
        self.job_manager.assign_processes(processes)
        self._setup_test_file(server_manager, log_file, test_file_suffix)
        self._setup_environment(env, log_file)

        if self.fail_on_warning:
            self.check_results_list.append("WARNING")
        self.job_manager.job = self

        try:
            self.display_pool_space()
            result = self.job_manager.run()

        finally:
            self.display_pool_space()
            if self.stop_dfuse:
                self.dfuse.stop()

        return result
