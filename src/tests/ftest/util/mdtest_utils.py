"""
  (C) Copyright 2019-2024 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import re

from command_utils import ExecutableCommand
from command_utils_base import BasicParameter, FormattedParameter, LogParameter
from exception_utils import CommandFailure
from general_utils import get_log_file
from job_manager_utils import get_job_manager

MDTEST_NAMESPACE = "/run/mdtest/*"


def get_mdtest(test, hosts, manager=None, path=None, slots=None, namespace=MDTEST_NAMESPACE,
               mdtest_params=None):
    """Get a Mdtest object.

    Args:
        test (Test): avocado Test object
        hosts (NodeSet): hosts on which to run the mdtest command
        manager (JobManager, optional): command to manage the multi-host execution of mdtest.
            Defaults to None, which will get a default job manager.
        path (str, optional): hostfile path. Defaults to None.
        slots (int, optional): hostfile number of slots per host. Defaults to None.
        namespace (str, optional): path to yaml parameters. Defaults to MDTEST_NAMESPACE.
        mdtest_params (dict, optional): parameters to update the mdtest command. Defaults to None.

    Returns:
        Mdtest: the Mdtest object requested
    """
    mdtest = Mdtest(test, hosts, manager, path, slots, namespace)
    if mdtest_params:
        for name, value in mdtest_params.items():
            mdtest.update(name, value)
    return mdtest


def run_mdtest(test, hosts, path, slots, container, processes, ppn=None, manager=None,
               log_file=None, intercept=None, display_space=True, namespace=MDTEST_NAMESPACE,
               mdtest_params=None):
    # pylint: disable=too-many-arguments
    """Run Mdtest on multiple hosts.

    Args:
        test (Test): avocado Test object
        hosts (NodeSet): hosts on which to run the mdtest command
        path (str): hostfile path.
        slots (int): hostfile number of slots per host.
        container (TestContainer): DAOS test container object.
        processes (int): number of processes to run
        ppn (int, optional): number of processes per node to run.  If specified it will override
            the processes input. Defaults to None.
        manager (JobManager, optional): command to manage the multi-host execution of mdtest.
            Defaults to None, which will get a default job manager.
        log_file (str, optional): log file name. Defaults to None, which will result in a log file
            name containing the test, pool, and container IDs.
        intercept (str, optional): path to interception library. Defaults to None.
        display_space (bool, optional): Whether to display the pool space. Defaults to True.
        namespace (str, optional): path to yaml parameters. Defaults to MDTEST_NAMESPACE.
        mdtest_params (dict, optional): dictionary of MdtestCommand attributes to override from
            get_params(). Defaults to None.

    Raises:
        CommandFailure: if there is an error running the mdtest command

    Returns:
        CmdResult: result of the ior command

    """
    mdtest = get_mdtest(test, hosts, manager, path, slots, namespace, mdtest_params)
    if log_file is None:
        log_file = mdtest.get_unique_log(container)
    mdtest.update_log_file(log_file)
    return mdtest.run(container, processes, ppn, intercept, display_space)


def write_mdtest_data(test, container, namespace=MDTEST_NAMESPACE, **mdtest_run_params):
    """Write data to the container using mdtest.

    Simple method for test classes to use to write data with mdtest. While not required, this is
    setup by default to pull in mdtest parameters from the test yaml.

    Args:
        test (Test): avocado Test object
        container (TestContainer): the container to populate
        namespace (str, optional): path to mdtest yaml parameters. Defaults to MDTEST_NAMESPACE.
        mdtest_run_params (dict): optional params for the Mdtest.run() command.

    Returns:
        Mdtest: the Mdtest object used to populate the container
    """
    mdtest = get_mdtest(test, test.hostlist_clients, None, test.workdir, None, namespace)
    mdtest.update_log_file(mdtest.get_unique_log(container))

    if 'processes' not in mdtest_run_params:
        mdtest_run_params['processes'] = test.params.get('processes', namespace, None)
    elif 'ppn' not in mdtest_run_params:
        mdtest_run_params['ppn'] = test.params.get('ppn', namespace, None)

    mdtest.run(container, **mdtest_run_params)
    return mdtest


class MdtestCommand(ExecutableCommand):
    """Defines a object representing a mdtest command."""

    def __init__(self, log_dir, namespace="/run/mdtest/*"):
        """Create an MdtestCommand object.

        Args:
            log_dir (str): directory in which to put log files
            namespace (str, optional): path to yaml parameters. Defaults to "/run/mdtest/*".
        """
        super().__init__(namespace, "mdtest")

        self._log_dir = log_dir

        self.flags = FormattedParameter("{}")   # mdtest flags
        # Optional arguments
        #  -a=STRING             API for I/O [POSIX|DFS|DUMMY]
        #  -b=1                  branching factor of hierarchical dir structure
        #  -d=./out              the directory in which the tests will run
        #  -B=0                  no barriers between phases
        #  -e=0                  bytes to read from each file
        #  -f=1                  first number of tasks on which test will run
        #  -i=1                  number of iterations the test will run
        #  -I=0                  number of items per directory in tree
        #  -l=0                  last number of tasks on which test will run
        #  -n=0                  every process will creat/stat/read/remove num
        #                        of directories and files
        #  -N=0                  stride num between neighbor tasks for file/dir
        #                        operation (local=0)
        #  -p=0                  pre-iteration delay (in seconds)
        #  --random-seed=0       random seed for -R
        #  -s=1                  stride between number of tasks for each test
        #  -V=0                  verbosity value
        #  -w=0                  bytes to write each file after it is created
        #  -W=0                  number in seconds; stonewall timer, write as
        #                        many seconds and ensure all processes did the
        #                        same number of operations (currently only
        #                        stops during create phase)
        # -x=STRING              StoneWallingStatusFile; contains the number
        #                        of iterations of the creation phase, can be
        #                        used to split phases across runs
        # -z=0                   depth of hierarchical directory structure

        self.api = FormattedParameter("-a {}")
        self.branching_factor = FormattedParameter("-b {}")
        self.test_dir = FormattedParameter("-d {}")
        self.barriers = FormattedParameter("-B {}")
        self.read_bytes = FormattedParameter("-e {}")
        self.first_num_tasks = FormattedParameter("-f {}")
        self.iteration = FormattedParameter("-i {}")
        self.items = FormattedParameter("-I {}")
        self.last_num_tasks = FormattedParameter("-l {}")
        self.num_of_files_dirs = FormattedParameter("-n {}")
        self.pre_iter = FormattedParameter("-p {}")
        self.random_seed = FormattedParameter("--random-seed {}")
        self.stride = FormattedParameter("-s {}")
        self.verbosity_value = FormattedParameter("-V {}")
        self.write_bytes = FormattedParameter("-w {}")
        self.stonewall_timer = FormattedParameter("-W {}")
        self.stonewall_statusfile = LogParameter(self._log_dir, "-x {}", None)
        self.depth = FormattedParameter("-z {}")

        # Module DFS
        # Required arguments
        #  --dfs.pool=STRING             DAOS pool uuid
        #  --dfs.cont=STRING             DFS container uuid

        # Flags
        #  --dfs.destroy                 Destroy DFS Container

        # Optional arguments
        #  --dfs.group=STRING            DAOS server group
        #  --dfs.chunk_size=1048576      Chunk size
        #  --dfs.oclass=STRING           DAOS object class
        #  --dfs.dir_oclass=STRING       DAOS directory object class
        #  --dfs.prefix=STRING           Mount prefix

        self.dfs_pool = FormattedParameter("--dfs.pool {}")
        self.dfs_cont = FormattedParameter("--dfs.cont {}")
        self.dfs_group = FormattedParameter("--dfs.group {}")
        self.dfs_destroy = FormattedParameter("--dfs.destroy", True)
        self.dfs_chunk = FormattedParameter("--dfs.chunk_size {}", 1048576)
        self.dfs_oclass = FormattedParameter("--dfs.oclass {}", "S1")
        self.dfs_prefix = FormattedParameter("--dfs.prefix {}")
        self.dfs_dir_oclass = FormattedParameter("--dfs.dir_oclass {}", "SX")

        # Include bullseye coverage file environment
        self.env["COVFILE"] = os.path.join(os.sep, "tmp", "test.cov")

    def get_param_names(self):
        """Get a sorted list of the defined MdtestCommand parameters."""
        # Sort the Mdtest parameter names to generate consistent ior commands
        all_param_names = super().get_param_names()

        # List all of the common ior params first followed by any dfs-specific
        # params (except when using POSIX).
        param_names = [name for name in all_param_names if "dfs" not in name]
        if self.api.value != "POSIX":
            param_names.extend(
                [name for name in all_param_names if "dfs" in name])

        return param_names

    def get_default_env(self, manager_cmd, log_file=None):
        """Get the default environment settings for running mdtest.

        Args:
            manager_cmd (str): job manager command
            log_file (str, optional): log file. Defaults to None.

        Returns:
            EnvironmentVariables: a dictionary of environment names and values

        """
        env = self.env.copy()
        env["D_LOG_FILE"] = get_log_file(log_file or "{}_daos.log".format(self.command))
        env["MPI_LIB"] = '""'

        if "mpirun" in manager_cmd or "srun" in manager_cmd:
            env["DAOS_POOL"] = self.dfs_pool.value
            env["DAOS_CONT"] = self.dfs_cont.value
            env["IOR_HINT__MPI__romio_daos_obj_class"] = self.dfs_oclass.value

        return env


class Mdtest:
    """Defines a class that runs the mdtest command through a job manager, e.g. mpirun."""

    def __init__(self, test, hosts, manager=None, path=None, slots=None,
                 namespace=MDTEST_NAMESPACE):
        """Initialize an Mdtest object.

        Args:
            test (Test): avocado Test object
            hosts (NodeSet): hosts on which to run the mdtest command
            manager (JobManager, optional): command to manage the multi-host execution of mdtest.
                Defaults to None, which will get a default job manager.
            path (str, optional): hostfile path. Defaults to None.
            slots (int, optional): hostfile number of slots per host. Defaults to None.
            namespace (str, optional): path to yaml parameters. Defaults to MDTEST_NAMESPACE.
        """
        if manager is None:
            manager = get_job_manager(test, subprocess=False, timeout=60)
        self.manager = manager
        self.manager.assign_hosts(hosts, path, slots)
        self.manager.job = MdtestCommand(test.test_env.log_dir, namespace)
        self.manager.job.get_params(test)
        self.manager.output_check = "both"
        self.timeout = test.params.get("timeout", namespace, None)
        self.label_generator = test.label_generator
        self.test_id = test.test_id
        self.env = self.command.get_default_env(str(self.manager))

    @property
    def command(self):
        """Get the MdtestCommand object.

        Returns:
            MdtestCommand: the MdtestCommand object managed by the JobManager

        """
        return self.manager.job

    def update(self, name, value):
        """Update a MdtestCommand BasicParameter with a new value.

        Args:
            name (str): name of the MdtestCommand BasicParameter to update
            value (str): value to assign to the MdtestCommand BasicParameter
        """
        param = getattr(self.command, name, None)
        if param:
            if isinstance(param, BasicParameter):
                param.update(value, ".".join([self.command.command, name]))

    def update_log_file(self, log_file):
        """Update the log file for the mdtest command.

        Args:
            log_file (str): new mdtest log file
        """
        self.command.env["D_LOG_FILE"] = get_log_file(
            log_file or f"{self.command.command}_daos.log")

    def get_unique_log(self, container):
        """Get a unique mdtest log file name.

        Args:
            container (TestContainer): container involved with the command

        Returns:
            str: a log file name
        """
        label = self.label_generator.get_label("mdtest")
        parts = [self.test_id, container.pool.identifier, container.identifier, label]
        return '.'.join(['_'.join(parts), 'log'])

    def update_daos_params(self, pool, container):
        """Set the mdtest parameters for the pool and container.

        Optionally also set the DAOS pool and container environment variables for mdtest.

        Args:
            pool (TestPool): the pool to use with the mdtest command
            container (TestContainer): the container to use with the mdtest command
        """
        self.command.update_params(dfs_pool=pool.identifier, dfs_cont=container.identifier)

        if "mpirun" in str(self.manager) or "srun" in str(self.manager):
            self.env["DAOS_POOL"] = self.command.dfs_pool.value
            self.env["DAOS_CONT"] = self.command.dfs_cont.value
            self.env["IOR_HINT__MPI__romio_daos_obj_class"] = self.command.dfs_oclass.value

    def run(self, container, processes, ppn=None, intercept=None, display_space=True):
        """Run mdtest.

        Args:
            container (TestContainer): DAOS test container object.
            processes (int): number of processes to run
            ppn (int, optional): number of processes per node to run.  If specified it will override
                the processes input. Defaults to None.
            intercept (str, optional): path to interception library. Defaults to None.
            display_space (bool, optional): Whether to display the pool space. Defaults to True.

        Raises:
            CommandFailure: if there is an error running the mdtest command

        Returns:
            CmdResult: result of the mdtest command
        """
        result = None
        error_message = None

        self.update_daos_params(container.pool, container)

        if intercept:
            self.env["LD_PRELOAD"] = intercept

        # Pass only processes or ppn to be compatible with previous behavior
        if ppn is not None:
            self.manager.assign_processes(ppn=ppn)
        else:
            self.manager.assign_processes(processes=processes)

        self.manager.assign_environment(self.env)

        try:
            if display_space:
                container.pool.display_space()
            result = self.manager.run()

        except CommandFailure as error:
            error_message = "Mdtest Failed:\n  {}".format("\n  ".join(str(error).split("\n")))

        finally:
            if not self.manager.run_as_subprocess and display_space:
                container.pool.display_space()

        if error_message:
            raise CommandFailure(error_message)

        return result


class MdtestMetrics():
    # pylint: disable=too-few-public-methods
    """Represents metrics from mdtest output.

    Metrics are split into groups "rates" and "times".
    Each group contains metrics for each operation.
    Each operation contains values for max, min, mean, stddev.
    For example:
        self.rates.file_creation.max
        self.times.directory_stat.min

    """

    class MdtestMetricsOperation():
        """Metrics for an individual operation. E.g. file_creation."""
        def __init__(self):
            """Initialize operations values."""
            self.max = 0.0
            self.min = 0.0
            self.mean = 0.0
            self.stddev = 0.0

    class MdtestMetricsGroup():
        """Group of metrics. E.g. "SUMMARY rate" and "SUMMARY time"."""
        def __init__(self):
            """Initialize each operation.
            Names are aligned with output from mdtest. E.g. "File creation" -> "file_creation"
            """
            self.directory_creation = MdtestMetrics.MdtestMetricsOperation()
            self.directory_stat = MdtestMetrics.MdtestMetricsOperation()
            self.directory_rename = MdtestMetrics.MdtestMetricsOperation()
            self.directory_removal = MdtestMetrics.MdtestMetricsOperation()
            self.file_creation = MdtestMetrics.MdtestMetricsOperation()
            self.file_stat = MdtestMetrics.MdtestMetricsOperation()
            self.file_read = MdtestMetrics.MdtestMetricsOperation()
            self.file_rename = MdtestMetrics.MdtestMetricsOperation()
            self.file_removal = MdtestMetrics.MdtestMetricsOperation()
            self.tree_creation = MdtestMetrics.MdtestMetricsOperation()
            self.tree_removal = MdtestMetrics.MdtestMetricsOperation()

    def __init__(self, output=None):
        """Initialize MdtestMetrics.

        Args:
            output (str, optional): output from mdtest from which to parse metrics.
                Default initializes metrics to 0.

        """
        self.rates = MdtestMetrics.MdtestMetricsGroup()
        self.times = MdtestMetrics.MdtestMetricsGroup()
        if output is not None:
            self.parse_output(output)

    def parse_output(self, output):
        """Parse output from Mdtest into metrics.

        Args:
            output (str): output from mdtest from which to parse metrics.

        """
        self._parse_output_group(output, self.rates, "rate")
        self._parse_output_group(output, self.times, "time")

    @staticmethod
    def _parse_output_group(output, group_obj, group_suffix):
        """Parse an output group.

        Args:
            output (str): output from mdtest from which to parse metrics.
            group_obj (MdtestMetrics.MdtestMetricsGroup): the group object.
            group_suffix (str): "rate" or "time" header in the output.

        """
        # Extract just one group, in case both are in the output
        match = re.search("SUMMARY {}(((?!(SUMMARY)|-- finished).)*)".format(group_suffix),
                          output, re.MULTILINE | re.DOTALL)
        if not match:
            return

        # Split into individual metric lines, skipping over headers
        for metric_line in match.group(0).splitlines()[3:]:
            if not metric_line:
                continue
            metric_vals = metric_line.split()
            if not metric_vals:
                continue
            # Name is, for example, "File" + " " + "creation"
            # Convert to "file_creation"
            operation_name = metric_vals[0] + ' ' + metric_vals[1]
            operation_name = operation_name.lower().replace(" ", "_")
            try:
                operation = getattr(group_obj, operation_name)
                operation.max = float(metric_vals[2])
                operation.min = float(metric_vals[3])
                operation.mean = float(metric_vals[4])
                operation.stddev = float(metric_vals[5])
            except AttributeError:
                pass
