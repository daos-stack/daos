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


AVOCADO_FILE = "src/tests/ftest/data/daos_avocado_test.yaml"

class DfuseFailed(Exception):
    """Dfuse Failed """

class DfuseCommand(ExecutableCommand):
    """Defines a object representing a dfuse command."""

    def __init__(self):
        """Create a dfuse Command object."""
        super(DfuseCommand, self).__init__("/run/dfuse/*", "dfuse")

        # dfuse options
        self.puuid = FormattedParameter("-p {}")
        self.cuuid = FormattedParameter("-c {}")
        self.mount_dir = FormattedParameter("-m {}")
        self.svcl = FormattedParameter("-s {}", 0)
        self.foreground = FormattedParameter("-f", False)

    def get_param_names(self):
        """Get a sorted list of dfuse command parameter names."""
        names = self.get_attribute_names(FormattedParameter)
        return names

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

    def create_dfuse_dir(self, hosts):
        """Create dfuse directory

        Args:
            hosts: list of hosts where dfuse directory needs to be created

        Raises:
            CommandFailure: In case of error creating directory
        """
        for host in hosts:
            if general_utils.check_file_exists(
                    [host], self.mount_dir.value, directory=True)[0] is False:
                ret_code = general_utils.pcmd(
                    [host], "mkdir " + self.mount_dir.value, timeout=30)
                for key in ret_code:
                    if key is not 0:
                        raise CommandFailure(
                            "DfuseFailure: Error creating directory: "
                            "{}".format(self.mount_dir.value))

    def remove_dfuse_dir(self, hosts):
        """Remove dfuse directory

        Args:
            hosts: list of hosts from where dfuse directory needs to be deleted

        Raises:
            CommandFailure: In case of error deleting directory
        """
        if general_utils.check_file_exists(
                hosts, self.mount_dir.value)[0] is True:
            ret_code = general_utils.pcmd(
                hosts, "rm -rf " + self.mount_dir.value, timeout=30)
            for key in ret_code:
                if key is not 0:
                    raise CommandFailure("DfuseFailure: Error deleting: "
                                         "{}".format(self.mount_dir.value))

    def run_dfuse(self, hosts, attach_info, basepath):
        """ Run the dfuse command.

        Args:
            hosts: list of hosts where dfuse needs to be started
            attach_info (str): CART attach info path
            basepath: path for daos install dir
        Raises:
            CommandFailure: In case dfuse run command fails
        """

        # create dfuse dir if does not exist
        self.create_dfuse_dir(hosts)
        # obtain env export string
        env = self.get_default_env(attach_info, basepath)

        # run dfuse command
        ret_code = general_utils.pcmd(hosts, env + self.__str__(), timeout=30)

        # check for any failures
        for key in ret_code:
            if key is not 0:
                raise CommandFailure("DfuseFailure: Error starting dfuse. "
                                     "RC:{}".format(ret_code))

    def stop_dfuse(self, hosts):
        """Stop dfuse

        Args:
            hosts: list of hosts where dfuse needs to be stopped

        Raises:
            CommandFailure: In case dfuse stop fails
        """

        for host in hosts:
            if 0 in general_utils.pcmd([host], "which fusermount"):
                fuse_cmd = "fusermount -u {}".format(self.mount_dir.value)
            elif 0 in general_utils.pcmd([host], "which fusermount3"):
                fuse_cmd = "fusermount3 -u {}".format(self.mount_dir.value)
            else:
                raise CommandFailure("No Fuse on {}".format([host]))

            ret_code = general_utils.pcmd([host], fuse_cmd, timeout=30)

            self.remove_dfuse_dir([host])

            for key in ret_code:
                if key is not 0:
                    raise CommandFailure("DfuseFailure: Error stopping dfuse. "
                                         "RC:{}".format(ret_code))


    @classmethod
    def get_default_env(cls, attach_info, basepath=None):
        """Get the default enviroment settings for running Dfuse.

        Args:
            attach_info (str): CART attach info path
            basepath: path for daos install dir
        Returns:
            env_export: a single string of all env vars to be
                                  exported

        """

        # obtain any env variables to be exported
        env = EnvironmentVariables()
        env["CRT_ATTACH_INFO_PATH"] = attach_info
        env["DAOS_SINGLETON_CLI"] = 1

        if basepath is not None:
            try:
                with open('{}/{}'.format(basepath, AVOCADO_FILE),
                          'r') as read_file:
                    for line in read_file:
                        if ("provider" in line) or ("fabric_iface" in line):
                            items = line.split()
                            key, values = items[0][:-1], items[1]
                            env[key] = values

                env['OFI_INTERFACE'] = env.pop('fabric_iface')
                env['OFI_PORT'] = env.pop('fabric_iface_port')
                env['CRT_PHY_ADDR_STR'] = env.pop('provider')
            except Exception as err:
                raise DfuseFailed("Failed to read yaml file:{}".format(err))

        env_export = env.get_export_str()

        return env_export
