#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from __future__ import print_function

from command_utils_base import FormattedParameter
from command_utils_base import BasicParameter
from command_utils import ExecutableCommand
from job_manager_utils import Mpirun


class DataMoverCommand(ExecutableCommand):
    """Defines a object representing a datamover command."""

    def __init__(self, namespace, command):
        """Create a datamover Command object."""
        super(DataMoverCommand, self).__init__(namespace, command)

        # datamover options

        # IO buffer size in bytes (default 1MB)
        self.blocksize = FormattedParameter("--blocksize {}")
        # DAOS source pool
        self.daos_src_pool = FormattedParameter("--daos-src-pool {}")
        # DAOS destination pool
        self.daos_dst_pool = FormattedParameter("--daos-dst-pool {}")
        # DAOS source container
        self.daos_src_cont = FormattedParameter("--daos-src-cont {}")
        # DAOS destination container
        self.daos_dst_cont = FormattedParameter("--daos-dst-cont {}")
        # DAOS prefix for unified namespace path
        self.daos_prefix = FormattedParameter("--daos-prefix {}")
        # read source list from file
        self.input_file = FormattedParameter("--input {}")
        # work size per task in bytes (default 1MB)
        self.chunksize = FormattedParameter("--chunksize {}")
        # preserve permissions, ownership, timestamps, extended attributes
        self.preserve = FormattedParameter("--preserve", False)
        # use synchronous read/write calls (O_DIRECT)
        self.direct = FormattedParameter("--direct", False)
        # create sparse files when possible
        self.sparse = FormattedParameter("--sparse", False)
        # print progress every N seconds
        self.progress = FormattedParameter("--progress {}")
        # verbose output
        self.verbose = FormattedParameter("--verbose", False)
        # quiet output
        self.quiet = FormattedParameter("--quiet", False)
        # print help/usage
        self.print_usage = FormattedParameter("--help", False)
        # source path
        self.src_path = BasicParameter(None)
        # destination path
        self.dest_path = BasicParameter(None)

    def get_param_names(self):
        """Overriding the original get_param_names"""

        param_names = super(DataMoverCommand, self).get_param_names()

        # move key=dest_path to the end
        param_names.sort(key='dest_path'.__eq__)

        return param_names

    def set_datamover_params(self, src_pool=None, dst_pool=None, src_cont=None,
                             dst_cont=None, display=True):
        """Set the datamover params for the DAOS group, pool, and cont uuid.

        Args:
          src_pool(TestPool): source pool object
          dst_pool(TestPool): destination pool object
          src_cont(TestContainer): source container object
          dst_cont(TestContainer): destination container object
          display (bool, optional): print updated params. Defaults to True.
        """

        # set the obtained values
        if src_pool:
            self.daos_src_pool.update(src_pool.uuid,
                                      "daos_src_pool" if display else None)

        if dst_pool:
            self.daos_dst_pool.update(dst_pool.uuid,
                                      "daos_dst_pool" if display else None)
        if src_cont:
            self.daos_src_cont.update(src_cont.uuid,
                                      "daos_src_cont" if display else None)
        if dst_cont:
            self.daos_dst_cont.update(dst_cont.uuid,
                                      "daos_dst_cont" if display else None)


class DataMover(DataMoverCommand):
    """Class defining an object of type DataMoverCommand."""

    def __init__(self, hosts, tmp, timeout=30):
        """Create a datamover object."""
        super(DataMover, self).__init__(
            "/run/datamover/*", "dcp")

        # set params
        self.timeout = timeout
        self.hosts = hosts
        self.tmp = tmp

        # Compatibility option
        self.has_src_pool = False

        self.exit_status_exception = False
        self.get_version()
        self.exit_status_exception = True

    def get_version(self):
        """Checks which version of dcp is available."""
        self.print_usage.update(True)
        result = self.run(self.tmp, 1)
        if "--daos-src-pool" in result.stdout:
            self.has_src_pool = True

    def run(self, tmp, processes):
        # pylint: disable=arguments-differ
        """Run the datamover command.

        Args:
            tmp (str): path for hostfiles
            processes: Number of processes for dcp command
        Raises:
            CommandFailure: In case datamover run command fails

        """
        self.log.info('Starting datamover')

        # Handle compatibility
        if not self.has_src_pool:
            src_pool = self.daos_src_pool.value
            src_cont = self.daos_src_cont.value
            src_path = self.src_path.value
            dst_pool = self.daos_dst_pool.value
            dst_cont = self.daos_dst_cont.value
            dst_path = self.dest_path.value
            if src_pool or src_cont:
                self.log.info(
                    "Converting --daos-src-pool to daos://pool/cont/path")
                src_path = "daos://{}/{}/{}".format(
                    src_pool, src_cont, src_path)
                self.src_path.update(src_path)
                self.daos_src_pool.update(None)
                self.daos_src_cont.update(None)
            if dst_pool or dst_cont:
                self.log.info(
                    "Converting --daos-dst-pool to daos://pool/cont/path")
                dst_path = "daos://{}/{}/{}".format(
                    dst_pool, dst_cont, dst_path)
                self.dest_path.update(dst_path)
                self.daos_dst_pool.update(None)
                self.daos_dst_cont.update(None)

        # Get job manager cmd
        mpirun = Mpirun(self, mpitype="mpich")
        mpirun.assign_hosts(self.hosts, tmp)
        mpirun.assign_processes(processes)
        mpirun.exit_status_exception = self.exit_status_exception

        # run dcp
        out = mpirun.run()

        return out
