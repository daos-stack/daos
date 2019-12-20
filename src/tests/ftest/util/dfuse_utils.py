#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.
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
import general_utils

from command_utils import ExecutableCommand, EnvironmentVariables
from command_utils import CommandFailure, FormattedParameter
from ClusterShell.NodeSet import NodeSet
# from server_utils import AVOCADO_FILE


class DfuseCommand(ExecutableCommand):
    """Defines a object representing a dfuse command."""

    def __init__(self, namespace, command):
        """Create a dfuse Command object."""
        super(DfuseCommand, self).__init__(namespace, command)

        # dfuse options
        self.puuid = FormattedParameter("--pool {}")
        self.cuuid = FormattedParameter("--container {}")
        self.mount_dir = FormattedParameter("--mountpoint {}")
        self.svcl = FormattedParameter("--svc {}", 0)
        self.sys_name = FormattedParameter("--sys-name {}")
        self.singlethreaded = FormattedParameter("--singlethreaded", False)
        self.foreground = FormattedParameter("--foreground", False)

    def set_dfuse_params(self, pool, display=True):
        """Set the dfuse parameters for the DAOS group, pool, and container uuid
        Args:
            pool (TestPool): DAOS test pool object
            display (bool, optional): print updated params. Defaults to True.
        """
        self.set_dfuse_pool_params(pool, display)

    def set_dfuse_pool_params(self, pool, display=True):
        """Set Dfuse params based on Daos Pool.
        Args:
            pool (TestPool): DAOS test pool object
            display (bool, optional): print updated params. Defaults to True.
        """
        self.puuid.update(pool.uuid, "puuid" if display else None)
        self.set_dfuse_svcl_param(pool, display)

    def set_dfuse_svcl_param(self, pool, display=True):
        """Set the dfuse svcl param from the ranks of a DAOS pool object.
        Args:
            pool (TestPool): DAOS test pool object
            display (bool, optional): print updated params. Defaults to True.
        """

        svcl = ":".join(
            [str(item) for item in [
                int(pool.pool.svc.rl_ranks[index])
                for index in range(pool.pool.svc.rl_nr)]])
        self.svcl.update(svcl, "svcl" if display else None)

    def set_dfuse_cont_param(self, cont, display=True):
        """Set dfuse cont param from Container object
        Args:
            cont (TestContainer): Daos test container object
            display (bool, optional): print updated params. Defaults to True.
        """
        self.cuuid.update(cont, "cuuid" if display else None)


class Dfuse(DfuseCommand):
    """Class defining an object of type DfuseCommand"""

    def __init__(self, hosts, tmp, dfuse_env=None):
        """Create a dfuse object.

        Args:
            hosts (list): list of hosts on which to run the dfuse command
            tmp ([type]): [description]
            dfuse_env (EnvironmentVariables, optional): dictionary of
                environmental variable names and values to export to the dfuse
                command. Defaults to None.
        """
        super(Dfuse, self).__init__("/run/dfuse/*", "dfuse")

        # set params
        self.hosts = hosts
        self.tmp = tmp
        self.dfuse_env = dfuse_env

    def __del__(self):
        """Destroy Dfuse object and stop dfuse """
        # stop dfuse
        self.stop()

    def create_mount_point(self):
        """Create dfuse directory
        Raises:
            CommandFailure: In case of error creating directory
        """
        # raise exception if mount point not specified
        if self.mount_dir.value is None:
            raise CommandFailure("Mount point not specified, "
                                 "check test yaml file")

        dir_exists, _ = general_utils.check_file_exists(
            self.hosts, self.mount_dir.value, directory=True)
        if not dir_exists:
            cmd = "mkdir -p {}".format(self.mount_dir.value)
            ret_code = general_utils.pcmd(self.hosts, cmd, timeout=30)
            if 0 not in ret_code:
                error_hosts = NodeSet(
                    ",".join(
                        [str(node_set) for code, node_set in ret_code.items()
                         if code != 0]))
                raise CommandFailure(
                    "Error creating the {} dfuse mount point on the following "
                    "hosts: {}".format(self.mount_dir.value, error_hosts))

    def remove_mount_point(self):
        """Remove dfuse directory
        Raises:
            CommandFailure: In case of error deleting directory
        """
        # raise exception if mount point not specified
        if self.mount_dir.value is None:
            raise CommandFailure("Mount point not specified, "
                                 "check test yaml file")

        dir_exists, _ = general_utils.check_file_exists(
            self.hosts, self.mount_dir.value, directory=True)
        if dir_exists:
            cmd = "rm -rf {}".format(self.mount_dir.value)
            ret_code = general_utils.pcmd(self.hosts, cmd, timeout=30)
            if 0 not in ret_code:
                error_hosts = NodeSet(
                    ",".join(
                        [str(node_set) for code, node_set in ret_code.items()
                         if code != 0]))
                raise CommandFailure(
                    "Error removing the {} dfuse mount point on the following "
                    "hosts: {}".format(self.mount_dir.value, error_hosts))

    def run(self):
        """ Run the dfuse command.
        Raises:
            CommandFailure: In case dfuse run command fails
        """

        # create dfuse dir if does not exist
        self.create_mount_point()
        # obtain env export string
        if isinstance(self.env, EnvironmentVariables):
            self._pre_command = self.env.get_export_str()
        # run dfuse command
        ret_code = general_utils.pcmd(self.hosts, self.__str__(), timeout=30)
        # check for any failures
        if 0 not in ret_code:
            error_hosts = NodeSet(
                ",".join(
                    [str(node_set) for code, node_set in ret_code.items()
                     if code != 0]))
            raise CommandFailure(
                "Error starting dfuse on the following hosts: {}".format(
                    error_hosts))

    def stop(self):
        """Stop dfuse
        Raises:
            CommandFailure: In case dfuse stop fails
        """

        cmd = "if [ -x '$(command -v fusermount)' ]; "
        cmd += "then fusermount -u {0}; else fusermount3 -u {0}; fi".\
               format(self.mount_dir.value)
        ret_code = general_utils.pcmd(self.hosts, cmd, timeout=30)
        self.remove_mount_point()
        if 0 not in ret_code:
            error_hosts = NodeSet(
                ",".join(
                    [str(node_set) for code, node_set in ret_code.items()
                     if code != 0]))
            raise CommandFailure(
                "Error stopping dfuse on the following hosts: {}".format(
                    error_hosts))
