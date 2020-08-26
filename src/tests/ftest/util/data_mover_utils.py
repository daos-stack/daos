#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from __future__ import print_function
import time

from command_utils_base import CommandFailure, FormattedParameter
from command_utils_base import BasicParameter
from command_utils import ExecutableCommand
from job_manager_utils import Mpirun
from ClusterShell.NodeSet import NodeSet
from general_utils import check_file_exists, pcmd


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
        # DAOS service level
        self.daos_svcl = FormattedParameter("--daos-svcl {}", 0)
        # DAOS prefix for unified namespace path
        self.daos_prefix = FormattedParameter("--daos-prefix {}")
        # read source list from file
        self.input_file = FormattedParameter("--input {}")
        # work size per task in bytes (default 1MB)
        self.chunksize = FormattedParameter("--chunksize {}")
        # preserve permissions, ownership, timestamps, extended attributes
        self.preserve = FormattedParameter("--preserve", False)
        # use synchronous read/write calls (O_DIRECT)
        self.synchronous = FormattedParameter("--synchronous", False)
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


#        self.puuid = FormattedParameter("--pool {}")
#        self.cuuid = FormattedParameter("--container {}")
#        self.mount_dir = FormattedParameter("--mountpoint {}")
#        self.svcl = FormattedParameter("--svc {}", 0)
#        self.sys_name = FormattedParameter("--sys-name {}")
#        self.singlethreaded = FormattedParameter("--singlethreaded", False)
#        self.foreground = FormattedParameter("--foreground", False)
#        self.disable_direct_io = FormattedParameter("--disable-direct-io",
#                                                    False)

        # Environment variable names to export when running datamover
        self._env_names = ["D_LOG_FILE"]

    def get_param_names(self):
        """Overriding the original get_param_names"""

        param_names = super(DataMoverCommand, self).get_param_names()
        
        # move key=dest_path to the end
        param_names.sort(key = 'dest_path'.__eq__) 

        return param_names

    def set_datamover_params(self, display=True):
        """Set the datamover params for the DAOS group, pool, and container uuid.

        Args:
            pool (TestPool): DAOS test pool object
            display (bool, optional): print updated params. Defaults to True.
        """
        
#        self.get_params(self)

        if self.src_pool:
            self.daos_src_pool.update(self.src_pool.uuid,
                                      "daos_src_pool" if display else None)
        if self.dst_pool:
            self.daos_dst_pool.update(self.dst_pool.uuid,
                                      "daos_dst_pool" if display else None)
        if self.src_cont:
            self.daos_src_cont.update(self.src_cont.uuid,
                                      "daos_src_cont" if display else None)
        if self.dst_cont:
            self.daos_dst_cont.update(self.dst_cont.uuid,
                                      "daos_dst_cont" if display else None)

#        svcl = ":".join(
#            [str(item) for item in [
#                int(pool.pool.svc.rl_ranks[index])
#                for index in range(pool.pool.svc.rl_nr)]])
#        self.daos_svcl.update(svcl, "svcl" if display else None)

#        self.set_datamover_pool_params(pool, display)

    def set_datamover_pool_params(self, pool, display=True):
        """Set DataMover params based on Daos Pool.

        Args:
            pool (TestPool): DAOS test pool object
            display (bool, optional): print updated params. Defaults to True.
        """
        self.puuid.update(pool.uuid, "puuid" if display else None)
        self.set_datamover_svcl_param(pool, display)

    def set_datamover_svcl_param(self, pool, display=True):
        """Set the datamover svcl param from the ranks of a DAOS pool object.

        Args:
            pool (TestPool): DAOS test pool object
            display (bool, optional): print updated params. Defaults to True.
        """
        svcl = ":".join(
            [str(item) for item in [
                int(pool.pool.svc.rl_ranks[index])
                for index in range(pool.pool.svc.rl_nr)]])
        self.svcl.update(svcl, "svcl" if display else None)

    def set_datamover_cont_param(self, cont, display=True):
        """Set datamover cont param from Container object.

        Args:
            cont (TestContainer): Daos test container object
            display (bool, optional): print updated params. Defaults to True.
        """
        self.cuuid.update(cont, "cuuid" if display else None)

    def set_datamover_exports(self, manager, log_file):
        """Set exports to issue before the datamover command.

        Args:
            manager (DaosServerManager): server manager object to use to
                obtain the ofi and cart environmental variable settings from the
                server yaml file
            log_file (str): name of the log file to combine with the
                DAOS_TEST_LOG_DIR path with which to assign D_LOG_FILE
        """
        env = self.get_environment(manager, log_file)
        self.set_environment(env)


class DataMover(DataMoverCommand):
    """Class defining an object of type DataMoverCommand."""

    def __init__(self, hosts, src_pool=None, dst_pool=None, src_cont=None, dst_cont=None):
        """Create a datamover object."""
        super(DataMover, self).__init__("/run/datamover/*", "/home/standan/mpiio/install/mpifileutils/bin/dcp")

        # set params
        self.src_pool = src_pool
        self.dst_pool = dst_pool
        self.src_cont = src_cont
        self.dst_cont = dst_cont

        self.timeout = 30
        self.hosts = hosts
        
#        self.tmp = tmp
#        self.running_hosts = NodeSet()

#    def __del__(self):
#        """Destruct the object."""
#        if len(self.running_hosts):
#            self.log.error('DataMover object deleted without shutting down')

    def run(self, tmp, processes):
        """Run the datamover command.

        Args:
            check (bool): Check if datamover mounted properly after
                mount is executed.
        Raises:
            CommandFailure: In case datamover run command fails

        """
        self.log.info('Starting datamover')

        # A log file must be defined to ensure logs are captured
#        if "D_LOG_FILE" not in self.env:
#            raise CommandFailure(
#                "DataMover missing environment variables for D_LOG_FILE")

        # create datamover dir if does not exist
#        self.create_mount_point()

        # run datamover command
#        cmd = "".join([self.env.get_export_str(), self.__str__()])

        # Get job manager cmd
        self.mpirun = Mpirun(self, mpitype="mpich")
        self.mpirun.assign_hosts(self.hosts, tmp)
        self.mpirun.assign_processes(processes)

        cmd = self.__str__()

        out = self.mpirun.run()
        
        return out


#        ret = pcmd(self.hosts, cmd, timeout=self.timeout)

#        if 0 not in ret:
#            error_hosts = NodeSet(
#                ",".join(
#                    [str(node_set) for code, node_set in
#                     ret.items() if code != 0]))
#            raise CommandFailure(
#                "<DataMover Command Failed>Error running '{}' on the following "
#                "hosts: {}".format(cmd, error_hosts))
