#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from command_utils_base import FormattedParameter
from command_utils_base import BasicParameter
from command_utils import ExecutableCommand
from job_manager_utils import Mpirun

def uuid_from_obj(obj):
    """Try to get uuid from an object.

    Args:
        obj (Object): The object possibly containing uuid.

    Returns:
        Object: obj.uuid if it exists; otherwise, obj

    """
    if hasattr(obj, "uuid"):
        return obj.uuid
    return obj

def format_daos_path(pool=None, cont=None, path=None):
    """Format a daos path as daos://<pool>/<cont>/<path>.

    Args:
        pool (TestPool, optional): the source pool or uuid.
        cont (TestContainer, optional): the source cont or uuid.
        path (str, optional): cont path relative to the root.

    Returns:
        str: the formatted path.

    """
    daos_path = "daos://"
    if pool:
        pool_uuid = uuid_from_obj(pool)
        daos_path += str(pool_uuid) + "/"
    if cont:
        cont_uuid = uuid_from_obj(cont)
        daos_path += str(cont_uuid) + "/"
    if path:
        daos_path += str(path).lstrip("/")
    return daos_path

class MfuCommandBase(ExecutableCommand):
    """Base MpiFileUtils command."""

    def __init__(self, namespace, command, hosts, tmp):
        """Initialize the command object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            command: command (str): string of the command to be executed.
            hosts (list): list of hosts to specify in the hostfile.
            tmp (str): path for hostfiles.

        """
        super().__init__(namespace, command)

        self.hosts = hosts
        self.tmp = tmp

    def set_params(self, **kwargs):
        """Set any Parameters for the class.

        Args:
            kwargs: name, value pairs of class Parameters

        """
        for k, v in kwargs.items():
            attr = getattr(self, k)
            attr.update(v, k)

    @staticmethod
    def __param_sort(k):
        """Key sort for get_param_names. Moves src_path and dst_path
           to the end of the list.

        Args:
            k (str): the key

        Returns:
            int: the sort priority

        """
        if k in ("dst_path", "dst"):
            return 3
        if k in ("src_path", "src"):
            return 2
        return 1

    def get_param_names(self):
        """Override the original get_param_names to sort
           the src and dst paths.

        Returns:
            list: the sorted param names.

        """
        param_names = super().get_param_names()
        param_names.sort(key=self.__param_sort)
        return param_names

    def run(self, processes, job_manager):
        # pylint: disable=arguments-differ
        """Run the MpiFileUtils command.

        Args:
            processes: Number of processes for the command.
            job_manager: Job manager variable to set/assign

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: In case run command fails.

        """
        self.log.info('Starting %s', str(self.command).lower())

        # Get job manager cmd
        job_manager = Mpirun(self, mpi_type="mpich")
        job_manager.assign_hosts(self.hosts, self.tmp)
        job_manager.assign_processes(processes)
        job_manager.exit_status_exception = self.exit_status_exception

        # Run the command
        out = job_manager.run()

        return out

class DcpCommand(MfuCommandBase):
    """Defines an object representing a dcp command."""

    def __init__(self, hosts, tmp):
        """Create a dcp Command object."""
        super().__init__("/run/dcp/*", "dcp", hosts, tmp)

        # dcp options

        # IO buffer size in bytes (default 64MB)
        self.blocksize = FormattedParameter("--blocksize {}")
        # New versions use bufsize instead of blocksize
        self.bufsize = FormattedParameter("--bufsize {}")
        # work size per task in bytes (default 64MB)
        self.chunksize = FormattedParameter("--chunksize {}")
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


class DsyncCommand(MfuCommandBase):
    """Defines an object representing a dsync command."""

    def __init__(self, hosts, tmp):
        """Create a dsync Command object."""
        super().__init__("/run/dsync/*", "dsync", hosts, tmp)

        # dsync options

        # show differences, but do not synchronize files
        self.dryrun = FormattedParameter("--dryrun", False)
        # batch files into groups of N during copy
        self.batch_files = FormattedParameter("--batch-files {}")
        # IO buffer size in bytes (default 4MB)
        self.bufsize = FormattedParameter("--blocksize {}")
        # work size per task in bytes (default 4MB)
        self.chunksize = FormattedParameter("--chunksize {}")
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

class DserializeCommand(MfuCommandBase):
    """Defines an object representing a daos-serialize command."""

    def __init__(self, hosts, tmp):
        """Create a daos-serialize Command object."""
        super().__init__("/run/dserialize/*", "daos-serialize", hosts, tmp)

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


class DdeserializeCommand(MfuCommandBase):
    """Defines an object representing a daos-deserialize command."""

    def __init__(self, hosts, tmp):
        """Create a daos-deserialize Command object."""
        super().__init__("/run/ddeserialize/*", "daos-deserialize", hosts, tmp)

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
        self.preserve_props = None
        self.daos_cmd = daos_cmd
        self.log = log

    def set_fs_copy_params(self, src=None, dst=None, preserve_props=None):
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
        if preserve_props:
            self.preserve_props = preserve_props

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

        return self.daos_cmd.filesystem_copy(src=self.src, dst=self.dst,
                                             preserve_props=self.preserve_props)

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
