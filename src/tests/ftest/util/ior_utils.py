"""
(C) Copyright 2018-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re
from enum import IntEnum

from avocado.utils.process import CmdResult
from command_utils_base import FormattedParameter, BasicParameter
from exception_utils import CommandFailure
from command_utils import SubProcessCommand
from general_utils import get_log_file
from duns_utils import format_path


def get_ior(test, manager, hosts, path, slots, namespace="/run/ior/*", ior_params=None):
    """Get a Ior object.

    Args:
        test (Test): avocado Test object
        manager (JobManager): command to manage the multi-host execution of ior
        hosts (NodeSet): hosts on which to run the ior command
        path (str): hostfile path.
        slots (int): hostfile number of slots per host.
        namespace (str, optional): path to yaml parameters. Defaults to "/run/ior/*".
        ior_params (dict, optional): dictionary of IorCommand attributes to override from
            get_params(). Defaults to None.

    Returns:
        Ior: the Ior object requested
    """
    ior = Ior(test, manager, hosts, path, slots, namespace)
    if ior_params:
        for name, value in ior_params.items():
            ior.update(name, value)
    return ior


def run_ior(test, manager, log, hosts, path, slots, group, pool, container, processes, ppn=None,
            intercept=None, plugin_path=None, dfuse=None, display_space=True, fail_on_warning=False,
            namespace="/run/ior/*", ior_params=None):
    # pylint: disable=too-many-arguments
    """Run IOR on multiple hosts.

    Args:
        test (Test): avocado Test object
        manager (JobManager): command to manage the multi-host execution of ior
        log (str): log file.
        hosts (NodeSet): hosts on which to run the ior command
        path (str): hostfile path.
        slots (int): hostfile number of slots per host.
        group (str): DAOS server group name
        pool (TestPool): DAOS test pool object
        container (TestContainer): DAOS test container object.
        processes (int): number of processes to run
        ppn (int, optional): number of processes per node to run.  If specified it will override
            the processes input. Defaults to None.
        intercept (str, optional): path to interception library. Defaults to None.
        plugin_path (str, optional): HDF5 vol connector library path. This will enable dfuse
            working directory which is needed to run vol connector for DAOS. Default is None.
        dfuse (Dfuse, optional): DAOS test dfuse object required when specifying a plugin_path.
            Defaults to None.
        display_space (bool, optional): Whether to display the pool space. Defaults to True.
        fail_on_warning (bool, optional): Controls whether the test should fail if a 'WARNING'
            is found. Default is False.
        namespace (str, optional): path to yaml parameters. Defaults to "/run/ior/*".
        ior_params (dict, optional): dictionary of IorCommand attributes to override from
            get_params(). Defaults to None.

    Raises:
        CommandFailure: if there is an error running the ior command

    Returns:
        CmdResult: result of the ior command

    """
    ior = get_ior(test, manager, hosts, path, slots, namespace, ior_params)
    ior.update_log_file(log)
    return ior.run(
        group, pool, container, processes, ppn, intercept, plugin_path, dfuse, display_space,
        fail_on_warning, False)


def thread_run_ior(thread_queue, job_id, test, manager, log, hosts, path, slots, group,
                   pool, container, processes, ppn, intercept, plugin_path, dfuse,
                   display_space, fail_on_warning, namespace, ior_params):
    # pylint: disable=too-many-arguments
    """Start an IOR thread with thread queue for failure analysis.

    Args:
        thread_queue (Queue): Thread queue object.
        job_id (str): Job identifier.
        test (Test): avocado Test object
        manager (JobManager): command to manage the multi-host execution of ior
        log (str): test log.
        hosts (NodeSet): hosts on which to run the ior command
        path (str): hostfile path.
        slots (int): hostfile number of slots per host.
        group (str): DAOS server group name
        pool (TestPool): DAOS test pool object
        container (TestContainer): DAOS test container object.
        processes (int): number of processes to run
        ppn (int, optional): number of processes per node to run.  If specified it will override
            the processes input. Defaults to None.
        intercept (str, optional): path to interception library. Defaults to None.
        plugin_path (str, optional): HDF5 vol connector library path. This will enable dfuse
            working directory which is needed to run vol connector for DAOS. Default is None.
        dfuse (Dfuse, optional): DAOS test dfuse object required when specifying a plugin_path.
            Defaults to None.
        display_space (bool, optional): Whether to display the pool space. Defaults to True.
        fail_on_warning (bool, optional): Controls whether the test should fail if a 'WARNING'
            is found. Default is False.
        namespace (str, optional): path to yaml parameters. Defaults to "/run/ior/*".
        ior_params (dict, optional): dictionary of IorCommand attributes to override from
            get_params(). Defaults to None.

    Returns:
        dict: A dictionary containing job_id(str), result(CmdResult) and log(str) keys.

    """
    thread_result = {
        "job_id": job_id,
        "result": None,
        "log": log
    }
    saved_verbose = manager.verbose
    manager.verbose = False
    try:
        thread_result["result"] = run_ior(test, manager, log, hosts, path, slots, group,
                                          pool, container, processes, ppn, intercept,
                                          plugin_path, dfuse, display_space, fail_on_warning,
                                          namespace, ior_params)
    except CommandFailure as error:
        thread_result["result"] = CmdResult(command="", stdout=str(error), exit_status=1)
    finally:
        manager.verbose = saved_verbose
        thread_queue.put(thread_result)


class IorCommand(SubProcessCommand):
    # pylint: disable=too-many-instance-attributes
    # pylint: disable=wrong-spelling-in-docstring
    """Defines a object for executing an IOR command.

    Example:
        >>> # Typical use inside of a DAOS avocado test method.
        >>> ior_cmd = IorCommand()
        >>> ior_cmd.get_params(self)
        >>> ior_cmd.set_daos_params(self.server_group, self.pool)
        >>> mpirun = Mpirun()
        >>> server_manager = self.server_manager[0]
        >>> env = self.ior_cmd.get_default_env(server_manager, self.client_log)
        >>> mpirun.assign_hosts(self.hostlist_clients, self.workdir, None)
        >>> mpirun.assign_processes(len(self.hostlist_clients))
        >>> mpirun.assign_environment(env)
        >>> mpirun.run()
    """

    def __init__(self, namespace="/run/ior/*"):
        """Create an IorCommand object.

        Args:
            namespace (str, optional): path to yaml parameters. Defaults to "/run/ior/*".
        """
        super().__init__(namespace, "ior", timeout=60)

        # Flags
        self.flags = FormattedParameter("{}")

        # pylint: disable=wrong-spelling-in-comment
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
        #   -D=0            deadlineForStonewalling -- seconds before stopping write or read phase
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
        self.sw_deadline = FormattedParameter("-D {}")
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

        # Include bullseye coverage file environment
        self.env["COVFILE"] = os.path.join(os.sep, "tmp", "test.cov")

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

    def set_daos_params(self, group, pool, cont):
        """Set the IOR parameters for the DAOS group, pool, and container uuid.

        Args:
            group (str): DAOS server group name
            pool (TestPool/str): DAOS test pool object or pool uuid/label
            cont (str): the container uuid or label
        """
        if self.api.value in ["DFS", "MPIIO", "POSIX", "HDF5"]:
            try:
                dfs_pool = pool.identifier
            except AttributeError:
                dfs_pool = pool
            self.update_params(
                dfs_group=group,
                dfs_pool=dfs_pool,
                dfs_cont=cont)

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
            replica_qty = 1
            try:
                # Extract the replica quantity from the object class string
                replica_qty = int(re.findall(r"\d+", self.dfs_oclass.value)[0])
            except (TypeError, IndexError):
                # If the daos object class is undefined (TypeError) or it does
                # not contain any numbers (IndexError) then there is only one
                # replica.
                pass
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
        env = self.env.copy()
        env["D_LOG_FILE"] = get_log_file(log_file or "{}_daos.log".format(self.command))
        env["MPI_LIB"] = '""'

        # ior POSIX api does not require the below options.
        if "POSIX" in manager_cmd:
            return env

        if "mpirun" in manager_cmd or "srun" in manager_cmd:
            if self.dfs_pool.value is not None:
                env["DAOS_UNS_PREFIX"] = format_path(self.dfs_pool.value, self.dfs_cont.value)
                if self.dfs_oclass.value is not None:
                    env["IOR_HINT__MPI__romio_daos_obj_class"] = self.dfs_oclass.value
        return env

    @staticmethod
    def get_ior_metrics(cmdresult):
        """Get the ior command read and write metrics.

        Parse the CmdResult (output of the test) and look for the ior stdout
        and get the read and write metrics.

        Args:
            cmdresult (CmdResult/str): output of job manager, or str output

        Returns:
            metrics (tuple) : list of write and read metrics from ior run

        """
        ior_metric_summary = "Summary of all tests:"
        if isinstance(cmdresult, str):
            messages = cmdresult.splitlines()
        else:
            messages = cmdresult.stdout_text.splitlines()
        # Get the index where the summary starts and add one to
        # get to the header.
        idx = messages.index(ior_metric_summary)
        # idx + 1 is header.
        # idx +2 and idx + 3 will give the write and read metrics.
        write_metrics = (" ".join(messages[idx + 2].split())).split()
        read_metrics = (" ".join(messages[idx + 3].split())).split()

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


class IorMetrics(IntEnum):
    """Index Name and Number of each column in IOR result summary."""

    # pylint: disable=wrong-spelling-in-comment
    # Operation   Max(MiB)   Min(MiB)  Mean(MiB)     StdDev   Max(OPs)
    # Min(OPs)  Mean(OPs) StdDev    Mean(s) Stonewall(s) Stonewall(MiB)
    # Test# #Tasks tPN reps fPP reord reordoff reordrand seed segcnt
    # blksiz    xsize aggs(MiB)   API RefNum
    OPERATION = 0
    MAX_MIB = 1
    MIN_MIB = 2
    MEAN_MIB = 3
    STDDEV_MIB = 4
    MAX_OPS = 5
    MIN_OPS = 6
    MEAN_OPS = 7
    STDDEV_OPS = 8
    MEAN_SECONDS = 9
    STONEWALL_SECONDS = 10
    STONEWALL_MIB = 11
    TEST_NO = 12
    NUM_TASKS = 13
    TPN = 14
    REPS = 15
    FPP = 16
    REORD = 17
    REORDOFF = 18
    REORDRAND = 19
    SEED = 20
    SEGCNT = 21
    BLKSIZ = 22
    XSIZE = 23
    AGGS_MIB = 24
    API = 25
    REFNUM = 26


class Ior:
    """Defines a class that runs the ior command through a job manager, e.g. mpirun."""

    def __init__(self, test, manager, hosts, path=None, slots=None, namespace="/run/ior/*"):
        """Initialize an Ior object.

        Args:
            test (Test): avocado Test object
            manager (JobManager): command to manage the multi-host execution of ior
            hosts (NodeSet): hosts on which to run the ior command
            path (str, optional): hostfile path. Defaults to None.
            slots (int, optional): hostfile number of slots per host. Defaults to None.
            namespace (str, optional): path to yaml parameters. Defaults to "/run/ior/*".
        """
        self.manager = manager
        self.manager.assign_hosts(hosts, path, slots)
        self.manager.job = IorCommand(namespace)
        self.manager.job.get_params(test)
        self.manager.output_check = "both"
        self.timeout = test.params.get("timeout", namespace, None)
        self.label_generator = test.label_generator
        self.test_id = test.test_id
        self.env = self.command.get_default_env(str(self.manager))

    @property
    def command(self):
        """Get the IorCommand object.

        Returns:
            IorCommand: the IorCommand object managed by the JobManager

        """
        return self.manager.job

    def update(self, name, value):
        """Update a IorCommand BasicParameter with a new value.

        Args:
            name (str): name of the IorCommand BasicParameter to update
            value (str): value to assign to the IorCommand BasicParameter
        """
        param = getattr(self.command, name, None)
        if param:
            if isinstance(param, BasicParameter):
                param.update(value, ".".join([self.command.command, name]))

    def update_log_file(self, log_file):
        """Update the log file for the ior command.

        Args:
            log_file (str): new ior log file
        """
        self.command.env["D_LOG_FILE"] = get_log_file(
            log_file or "{}_daos.log".format(self.command.command))

    def get_unique_log(self, container):
        """Get a unique ior log file name.

        Args:
            container (TestContainer): container involved with the command

        Returns:
            str: a log file name
        """
        label = self.label_generator.get_label("ior")
        parts = [self.test_id, container.pool.identifier, container.identifier, label]
        flags = self.command.flags.value
        if flags and '-w' in flags.lower():
            parts.append('write')
        if flags and '-r' in flags.lower():
            parts.append('read')
        return '.'.join(['_'.join(parts), 'log'])

    def run(self, group, pool, container, processes, ppn=None, intercept=None, plugin_path=None,
            dfuse=None, display_space=True, fail_on_warning=False, unique_log=True):
        # pylint: disable=too-many-arguments
        """Run ior.

        Args:
            group (str): DAOS server group name
            pool (TestPool): DAOS test pool object
            container (TestContainer): DAOS test container object.
            processes (int): number of processes to run
            ppn (int, optional): number of processes per node to run.  If specified it will override
                the processes input. Defaults to None.
            intercept (str, optional): path to interception library. Defaults to None.
            plugin_path (str, optional): HDF5 vol connector library path. This will enable dfuse
                working directory which is needed to run vol connector for DAOS. Default is None.
            dfuse (Dfuse, optional): DAOS test dfuse object required when specifying a plugin_path.
                Defaults to None.
            display_space (bool, optional): Whether to display the pool space. Defaults to True.
            fail_on_warning (bool, optional): Controls whether the test should fail if a 'WARNING'
                is found. Default is False.
            unique_log (bool, optional): whether or not to update the log file with a new unique log
                file name. Defaults to True.

        Raises:
            CommandFailure: if there is an error running the ior command

        Returns:
            CmdResult: result of the ior command

        """
        result = None
        error_message = None

        self.command.set_daos_params(group, pool, container.identifier)

        if intercept:
            self.env["LD_PRELOAD"] = intercept
            if "D_LOG_MASK" not in self.env:
                self.env["D_LOG_MASK"] = "INFO"
            if "D_IL_REPORT" not in self.env:
                self.env["D_IL_REPORT"] = "1"

        if plugin_path:
            self.env["HDF5_VOL_CONNECTOR"] = "daos"
            self.env["HDF5_PLUGIN_PATH"] = str(plugin_path)
            if dfuse:
                self.manager.working_dir.value = dfuse.mount_dir.value
            else:
                raise CommandFailure("Undefined 'dfuse' argument; required for 'plugin_path'")

        if not self.manager.job.test_file.value:
            # Provide a default test_file if not specified
            if dfuse and (self.manager.job.api.value == "POSIX" or plugin_path):
                test_file = self.manager.job.test_file.value or 'testfile'
                self.manager.job.test_file.update(os.path.join(dfuse.mount_dir.value, test_file))
            elif self.manager.job.api.value == "DFS":
                self.manager.job.test_file.update(
                    os.path.join(os.sep, self.label_generator.get_label("testfile")))

        if ppn is None:
            self.manager.assign_processes(processes)
        else:
            self.manager.ppn.update(ppn, ".".join([self.manager.command, "ppn"]))
            self.manager.processes.update(None, ".".join([self.manager.command, "np"]))

        self.manager.assign_environment(self.env)

        if fail_on_warning and "WARNING" not in self.manager.check_results_list:
            self.manager.check_results_list.append("WARNING")

        if unique_log:
            self.update_log_file(self.get_unique_log(container))

        try:
            if display_space:
                pool.display_space()
            result = self.manager.run()

        except CommandFailure as error:
            error_message = "IOR Failed:\n  {}".format("\n  ".join(str(error).split("\n")))

        finally:
            if not self.manager.run_as_subprocess and display_space:
                pool.display_space()

        if error_message:
            raise CommandFailure(error_message)

        return result

    def stop(self, pool=None):
        """Stop the ior command when the job manager was run as a subprocess .

        Args:
            pool (TestPool, optional): if provided the pool space will be displayed after attempting
                to stop the ior command . Defaults to None.

        Raises:
            CommandFailure: if there is an error stopping the ior subprocess

        """
        if self.manager.run_as_subprocess:
            error_message = None
            try:
                self.manager.stop()
            except CommandFailure as error:
                error_message = "IOR Failed: {}".format(error)
            finally:
                if pool:
                    pool.display_space()
            if error_message:
                raise CommandFailure(error_message)
