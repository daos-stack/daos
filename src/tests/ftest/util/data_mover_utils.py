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


class DcpCommand(ExecutableCommand):
    """Defines an object representing a dcp command."""

    def __init__(self, namespace, command):
        """Create a dcp Command object."""
        super(DcpCommand, self).__init__(namespace, command)

        # dcp options

        # IO buffer size in bytes (default 64MB)
        self.blocksize = FormattedParameter("--blocksize {}")
        # work size per task in bytes (default 64MB)
        self.chunksize = FormattedParameter("--chunksize {}")
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
        # copy original files instead of links
        self.dereference = FormattedParameter("--dereference", False)
        # don't follow links in source
        self.no_dereference = FormattedParameter("--no-dereference", False)
        # preserve permissions, ownership, timestamps, extended attributes
        self.preserve = FormattedParameter("--preserve", False)
        # open files with O_DIRECT
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
        self.dst_path = BasicParameter(None)

    def get_param_names(self):
        """Overriding the original get_param_names."""

        param_names = super(DcpCommand, self).get_param_names()

        # move key=dst_path to the end
        param_names.sort(key='dst_path'.__eq__)

        return param_names

    def set_dcp_params(self,
                       src_pool=None, src_cont=None, src_path=None,
                       dst_pool=None, dst_cont=None, dst_path=None,
                       prefix=None, display=True):
        """Set common dcp params.

        Args:
            src_pool (str, optional): source pool uuid
            src_cont (str, optional): source container uuid
            src_path (str, optional): source path
            dst_pool (str, optional): destination pool uuid
            dst_cont (str, optional): destination container uuid
            dst_path (str, optional): destination path
            prefix (str, optional): prefix for uns path
            display (bool, optional): print updated params. Defaults to True.

        """
        if src_pool:
            self.daos_src_pool.update(src_pool,
                                      "daos_src_pool" if display else None)

        if src_cont:
            self.daos_src_cont.update(src_cont,
                                      "daos_src_cont" if display else None)
        if src_path:
            self.src_path.update(src_path,
                                 "src_path" if display else None)
        if dst_pool:
            self.daos_dst_pool.update(dst_pool,
                                      "daos_dst_pool" if display else None)
        if dst_cont:
            self.daos_dst_cont.update(dst_cont,
                                      "daos_dst_cont" if display else None)
        if dst_path:
            self.dst_path.update(dst_path,
                                 "dst_path" if display else None)
        if prefix:
            self.daos_prefix.update(prefix,
                                    "daos_prefix" if display else None)

class Dcp(DcpCommand):
    """Class defining an object of type DcpCommand."""

    def __init__(self, hosts, tmp, timeout=30):
        """Create a dcp object."""
        super(Dcp, self).__init__(
            "/run/dcp/*", "dcp")

        # set params
        self.timeout = timeout
        self.hosts = hosts
        self.tmp = tmp

        # Compatibility option
        self.has_src_pool = False

    def set_compatibility(self, has_src_pool):
        """Set compatibility options.

        Args:
            has_src_pool (bool): Whether dcp has the --daos-src-pool option

        """
        self.has_src_pool = has_src_pool
        self.log.info("set_compatibility: has_src_pool=%s\n",
                      str(self.has_src_pool))

    def query_compatibility(self):
        """Query for compatibility options and set class variables."""
        self.print_usage.update(True)
        self.exit_status_exception = False
        result = self.run(self.tmp, 1)
        self.exit_status_exception = True
        self.has_src_pool = ("--daos-src-pool" in result.stdout)
        self.log.info("query_compatibility: has_src_pool=%s\n",
                      str(self.has_src_pool))

    def run(self, tmp, processes):
        # pylint: disable=arguments-differ
        """Run the dcp command.

        Args:
            tmp (str): path for hostfiles
            processes: Number of processes for dcp command

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: In case dcp run command fails

        """
        self.log.info('Starting dcp')

        # Handle compatibility
        if not self.has_src_pool:
            src_pool = self.daos_src_pool.value
            src_cont = self.daos_src_cont.value
            src_path = self.src_path.value
            dst_pool = self.daos_dst_pool.value
            dst_cont = self.daos_dst_cont.value
            dst_path = self.dst_path.value
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
                self.dst_path.update(dst_path)
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
