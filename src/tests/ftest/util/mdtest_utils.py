"""
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import re

from command_utils_base import FormattedParameter
from command_utils import ExecutableCommand
from general_utils import get_log_file


class MdtestCommand(ExecutableCommand):
    """Defines a object representing a mdtest command."""

    def __init__(self):
        """Create an MdtestCommand object."""
        super().__init__("/run/mdtest/*", "mdtest")
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
        self.stonewall_statusfile = FormattedParameter("-x {}")
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

    def set_daos_params(self, group, pool, cont):
        """Set the Mdtest params for the DAOS group, pool, and container uuid.

        Args:
            group (str): DAOS server group name
            pool (TestPool): DAOS test pool object
            cont (str): the container uuid or label
        """
        self.update_params(
            dfs_group=group,
            dfs_pool=pool.identifier,
            dfs_cont=cont)

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
