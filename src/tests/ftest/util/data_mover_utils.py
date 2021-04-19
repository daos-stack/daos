#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


from command_utils_base import FormattedParameter
from command_utils_base import BasicParameter
from command_utils import ExecutableCommand
from job_manager_utils import Mpirun


class DcpCommand(ExecutableCommand):
    """Defines an object representing a dcp command."""

    def __init__(self, namespace, command):
        """Create a dcp Command object."""
        super().__init__(namespace, command)

        # dcp options

        # IO buffer size in bytes (default 64MB)
        self.blocksize = FormattedParameter("--blocksize {}")
        # New versions use bufsize instead of blocksize
        self.bufsize = FormattedParameter("--bufsize {}")
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
        # DAOS API in {DFS, DAOS} (default uses DFS for POSIX containers)
        self.daos_api = FormattedParameter("--daos-api {}")
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

        param_names = super().get_param_names()

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
        super().__init__("/run/dcp/*", "dcp")

        # set params
        self.timeout = timeout
        self.hosts = hosts
        self.tmp = tmp

        # Compatibility option
        self.has_src_pool = False
        self.has_bufsize = True

    def set_compatibility(self, has_src_pool, has_bufsize):
        """Set compatibility options.

        Args:
            has_src_pool (bool): Whether dcp has the --daos-src-pool option
            has_bufsize (bool): Whether dcp has the --bufsize option

        """
        self.has_src_pool = has_src_pool
        self.log.info("set_compatibility: has_src_pool=%s\n",
                      str(self.has_src_pool))
        self.has_bufsize = has_bufsize
        self.log.info("set_compatibility: has_bufsize=%s\n",
                      str(self.has_bufsize))

    def query_compatibility(self):
        """Query for compatibility options and set class variables."""
        self.blocksize.update(None)
        self.bufsize.update(None)
        self.print_usage.update(True)
        self.exit_status_exception = False
        result = self.run(self.tmp, 1)
        self.exit_status_exception = True
        self.has_src_pool = ("--daos-src-pool" in result.stdout_text)
        self.log.info("query_compatibility: has_src_pool=%s\n",
                      str(self.has_src_pool))
        self.has_bufsize = ("--bufsize" in result.stdout_text)
        self.log.info("query_compatibility: has_bufsize=%s\n",
                      str(self.has_bufsize))

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
                new_path = "daos://"
                if src_pool:
                    new_path += str(src_pool) + "/"
                if src_cont:
                    new_path += str(src_cont) + "/"
                if src_path:
                    new_path += str(src_path).lstrip("/")
                self.src_path.update(new_path)
                self.daos_src_pool.update(None)
                self.daos_src_cont.update(None)
            if dst_pool or dst_cont:
                self.log.info(
                    "Converting --daos-dst-pool to daos://pool/cont/path")
                new_path = "daos://"
                if dst_pool:
                    new_path += str(dst_pool) + "/"
                if dst_cont:
                    new_path += str(dst_cont) + "/"
                if dst_path:
                    new_path += str(dst_path).lstrip("/")
                self.dst_path.update(new_path)
                self.daos_dst_pool.update(None)
                self.daos_dst_cont.update(None)
        if self.has_bufsize:
            blocksize = self.blocksize.value
            if blocksize:
                self.log.info(
                    "Converting --blocksize to --bufsize")
                self.blocksize.update(None)
                self.bufsize.update(blocksize)

        # Get job manager cmd
        mpirun = Mpirun(self, mpitype="mpich")
        mpirun.assign_hosts(self.hosts, tmp)
        mpirun.assign_processes(processes)
        mpirun.exit_status_exception = self.exit_status_exception

        # run dcp
        out = mpirun.run()

        return out


class DsyncCommand(ExecutableCommand):
    """Defines an object representing a dsync command."""

    def __init__(self, namespace, command):
        """Create a dsync Command object."""
        super().__init__(namespace, command)

        # dsync options

        # show differences, but do not synchronize files
        self.dryrun = FormattedParameter("--dryrun", False)
        # batch files into groups of N during copy
        self.batch_files = FormattedParameter("--batch-files {}")
        # IO buffer size in bytes (default 4MB)
        self.bufsize = FormattedParameter("--blocksize {}")
        # work size per task in bytes (default 4MB)
        self.chunksize = FormattedParameter("--chunksize {}")
        # DAOS prefix for unified namespace path
        self.daos_prefix = FormattedParameter("--daos-prefix {}")
        # DAOS API in {DFS, DAOS} (default uses DFS for POSIX containers)
        self.daos_api = FormattedParameter("--daos-api {}")
        # read and compare file contents rather than compare size and mtime
        self.contents = FormattedParameter("--contents", False)
        # delete extraneous files from target
        self.delete = FormattedParameter("--delete", False)
        # copy original files instead of links
        self.dereference = FormattedParameter("--dereference", False)
        # don't follow links in source
        self.no_dereference = FormattedParameter("--no-dereference", False)
        # open files with O_DIRECT
        self.direct = FormattedParameter("--direct", False)
        # hardlink to files in DIR when unchanged
        self.link_dest = FormattedParameter("--link-dest {}")
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

        param_names = super().get_param_names()

        # move key=dst_path to the end
        param_names.sort(key='dst_path'.__eq__)

        return param_names

    def set_dsync_params(self, src=None, dst=None,
                         prefix=None, display=True):
        """Set common dsync params.

        Args:
            src (str, optional): The source path formatted as
                daos://<pool>/<cont>/<path> or <path>
            dst (str, optional): The destination path formatted as
                daos://<pool>/<cont>/<path> or <path>
            prefix (str, optional): prefix for uns path
            display (bool, optional): print updated params. Defaults to True.
        """
        if src:
            self.src_path.update(src,
                                 "src_path" if display else None)
        if dst:
            self.dst_path.update(dst,
                                 "dst_path" if display else None)
        if prefix:
            self.daos_prefix.update(prefix,
                                    "daos_prefix" if display else None)


class Dsync(DsyncCommand):
    """Class defining an object of type DsyncCommand."""

    def __init__(self, hosts, timeout=30):
        """Create a dsync object."""
        super().__init__("/run/dsync/*", "dsync")

        # set params
        self.timeout = timeout
        self.hosts = hosts

    def run(self, tmp, processes):
        # pylint: disable=arguments-differ
        """Run the dsync command.

        Args:
            tmp (str): path for hostfiles
            processes: Number of processes for dsync command

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: In case dsync run command fails

        """
        self.log.info('Starting dsync')

        # Get job manager cmd
        mpirun = Mpirun(self, mpitype="mpich")
        mpirun.assign_hosts(self.hosts, tmp)
        mpirun.assign_processes(processes)
        mpirun.exit_status_exception = self.exit_status_exception

        # run dsync
        out = mpirun.run()

        return out

class DserializeCommand(ExecutableCommand):
    """Defines an object representing a daos-serialize command."""

    def __init__(self, namespace, command):
        """Create a daos-serialize Command object."""
        super().__init__(namespace, command)

        # daos-serialize options

        # path to output serialized hdf5 files
        self.output_path = FormattedParameter("--output-path {}")
        # verbose output
        self.verbose = FormattedParameter("--verbose", False)
        # quiet output
        self.quiet = FormattedParameter("--quiet", False)
        # print help/usage
        self.print_usage = FormattedParameter("--help", False)
        # source path
        self.src_path = BasicParameter(None)

    def get_param_names(self):
        """Overriding the original get_param_names."""

        param_names = super().get_param_names()

        # move key=src_path to the end
        param_names.sort(key='src_path'.__eq__)

        return param_names

    def set_dserialize_params(self, src_path=None, out_path=None,
                              display=True):
        """Set common daos-serialize params.

        Args:
            src_path (str, optional): The source path formatted as
                daos://<pool>/<cont>
            out_path (str, optional): The output POSIX path to store
                the HDF5 file(s)
            display (bool, optional): print updated params. Defaults to True.

        """
        if src_path:
            self.src_path.update(src_path,
                                 "src_path" if display else None)
        if out_path:
            self.output_path.update(out_path,
                                    "output_path" if display else None)


class Dserialize(DserializeCommand):
    """Class defining an object of type DserializeCommand."""

    def __init__(self, hosts, timeout=30):
        """Create a daos-serialize object."""
        super().__init__(
            "/run/dserialize/*", "daos-serialize")

        # set params
        self.timeout = timeout
        self.hosts = hosts

    def run(self, tmp, processes):
        # pylint: disable=arguments-differ
        """Run the command.

        Args:
            tmp (str): path for hostfiles
            processes: Number of processes for the command

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: In case run command fails

        """
        self.log.info('Starting daos-serialize')

        # Get job manager cmd
        mpirun = Mpirun(self, mpitype="mpich")
        mpirun.assign_hosts(self.hosts, tmp)
        mpirun.assign_processes(processes)
        mpirun.exit_status_exception = self.exit_status_exception

        # run the command
        out = mpirun.run()

        return out

class DdeserializeCommand(ExecutableCommand):
    """Defines an object representing a daos-deserialize command."""

    def __init__(self, namespace, command):
        """Create a daos-deserialize Command object."""
        super().__init__(namespace, command)

        # daos-deserialize options

        # pool uuid for containers
        self.pool = FormattedParameter("--pool {}")
        # verbose output
        self.verbose = FormattedParameter("--verbose", False)
        # quiet output
        self.quiet = FormattedParameter("--quiet", False)
        # print help/usage
        self.print_usage = FormattedParameter("--help", False)
        # source path
        self.src_path = BasicParameter(None)

    def get_param_names(self):
        """Overriding the original get_param_names."""

        param_names = super().get_param_names()

        # move key=src_path to the end
        param_names.sort(key='src_path'.__eq__)

        return param_names

    def set_ddeserialize_params(self, src_path=None, pool=None,
                                display=True):
        """Set common daos-deserialize params.

        Args:
            src_path (str, optional): Either a list of paths to each HDF5
                file, or the path to the directory containing the file(s).
            pool (str, optional): The pool uuid.
            display (bool, optional): print updated params. Defaults to True.

        """
        if src_path:
            self.src_path.update(src_path,
                                 "src_path" if display else None)
        if pool:
            self.pool.update(pool,
                             "pool" if display else None)

class Ddeserialize(DdeserializeCommand):
    """Class defining an object of type DdeserializeCommand."""

    def __init__(self, hosts, timeout=30):
        """Create a daos-deserialize object."""
        super().__init__(
            "/run/ddeserialize/*", "daos-deserialize")

        # set params
        self.timeout = timeout
        self.hosts = hosts

    def run(self, tmp, processes):
        # pylint: disable=arguments-differ
        """Run the command.

        Args:
            tmp (str): path for hostfiles
            processes: Number of processes for the command

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: In case run command fails

        """
        self.log.info('Starting daos-deserialize')

        # Get job manager cmd
        mpirun = Mpirun(self, mpitype="mpich")
        mpirun.assign_hosts(self.hosts, tmp)
        mpirun.assign_processes(processes)
        mpirun.exit_status_exception = self.exit_status_exception

        # run the command
        out = mpirun.run()

        return out

class FsCopy():
    """Class defining an object of type FsCopy.
       Allows interfacing with daos fs copy in a similar
       manner to DcpCommand.
    """

    def __init__(self, daos_cmd, log):
        """Create a FsCopy object.

        Args:
            daos_cmd (DaosCommand): daos command to issue the filesystem
                copy command.
            log (TestLogger): logger to log messages

        """
        self.src = None
        self.dst = None
        self.daos_cmd = daos_cmd
        self.log = log

    def set_fs_copy_params(self, src=None, dst=None):
        """Set the daos fs copy params.

        Args:
            src (str, optional): The source path formatted as
                daos://<pool>/<cont>/<path> or <path>
            dst (str, optional): The destination path formatted as
                daos://<pool>/<cont>/<path> or <path>

        """
        if src:
            self.src = src
        if dst:
            self.dst = dst

    def run(self):
        # pylint: disable=arguments-differ
        """Run the daos fs copy command.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: In case daos fs copy run command fails

        """
        self.log.info("Starting daos filesystem copy")

        return self.daos_cmd.filesystem_copy(src=self.src, dst=self.dst)

class ContClone():
    """Class defining an object of type ContClone.
       Allows interfacing with daos container copy in a similar
       manner to DcpCommand.
    """

    def __init__(self, daos_cmd, log):
        """Create a ContClone object.

        Args:
            daos_cmd (DaosCommand): daos command to issue the cont clone
                command.
            log (TestLogger): logger to log messages

        """
        self.src = None
        self.dst = None
        self.daos_cmd = daos_cmd
        self.log = log

    def set_cont_clone_params(self, src=None, dst=None):
        """Set the daos container clone params.

        Args:
            src (str, optional): the src, formatted as /<pool>/<cont>
            dst (str, optional): the dst, formatted as /<pool>/<cont>

        """
        if src:
            self.src = src
        if dst:
            self.dst = dst

    def run(self):
        # pylint: disable=arguments-differ
        """Run the daos container clone command.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: In case daos container clone run command fails.

        """
        self.log.info("Starting daos container clone")

        return self.daos_cmd.container_clone(src=self.src, dst=self.dst)
