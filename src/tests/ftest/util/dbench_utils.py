#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


from command_utils_base import FormattedParameter
from command_utils_base import BasicParameter
from command_utils import ExecutableCommand
from job_manager_utils import Mpirun

# pylint: disable=too-few-public-methods,too-many-instance-attributes
class DbenchCommand(ExecutableCommand):
    """Defines a object representing a dbench command."""

    def __init__(self, namespace, command):
        """Create a dbench Command object."""
        super().__init__(namespace, command)

        # dbench options
        self.timelimit = FormattedParameter("--timelimit {}")
        self.loadfile = FormattedParameter("--loadfile {}")
        self.directory = FormattedParameter("--directory {}")
        self.tcp_options = FormattedParameter("--tcp-options {}")
        self.target_rate = FormattedParameter("--target-rate {}")
        self.sync = FormattedParameter("--sync", False)
        self.fsync = FormattedParameter("--fsync", False)
        self.xattr = FormattedParameter("--xattr", False)
        self.no_resolve = FormattedParameter("--no-resolve", False)
        self.clients_per_process = FormattedParameter(
            "--clients-per-process {}")
        self.one_byte_write_fix = FormattedParameter(
            "--one-byte-write-fix", False)
        self.stat_check = FormattedParameter("--stat-check", False)
        self.fake_io = FormattedParameter("--fake-io", False)
        self.skip_cleanup = FormattedParameter("--skip-cleanup", False)
        self.per_client_results = FormattedParameter(
            "--per-client-results", False)
        self.num_of_procs = BasicParameter(None)

    def get_param_names(self):
        """Overriding the original get_param_names."""

        param_names = super().get_param_names()

        # move key=num_of_procs to the end
        param_names.sort(key='num_of_procs'.__eq__)

        return param_names


class Dbench(DbenchCommand):
    """Class defining an object of type DbenchCommand."""

    def __init__(self, hosts, tmp):
        """Create a dbench object."""
        super().__init__("/run/dbench/*", "dbench")

        # set params
        self.hosts = hosts
        self.tmp = tmp

    def run(self, processes=1):
        # pylint: disable=arguments-differ
        """Run the dbench command.

        Args:
            processes: mpi processes

        Raises:
            CommandFailure: In case dbench run command fails

        """
        self.log.info('Starting dbench')

        # Get job manager cmd
        mpirun = Mpirun(self, mpitype="mpich")
        mpirun.assign_hosts(self.hosts, self.tmp)
        mpirun.assign_processes(processes)
        mpirun.exit_status_exception = True

        # run dcp
        out = mpirun.run()

        return out
