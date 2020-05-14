#!/usr/bin/python
"""
  (C) Copyright 2019-2020 Intel Corporation.

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

import os

from distutils.spawn import find_executable
from env_modules import load_mpi
from general_utils import DaosTestError, run_command


class DaosPerfFailed(Exception):
    """Raise if daos_perf failed."""


# pylint: disable=R0903
class DaosPerfParam(object):
    """Defines a single daos_perf command line parameter."""

    def __init__(self, str_format, default=None):
        """Create a DaosPerfParam object.

        Args:
            str_format (str): format string used to convert the value into an
                daos_perf command line argument string
            default (object): default value for the param
        """
        self.str_format = str_format
        self.default = default
        self.value = default

    def __str__(self):
        """Return a DaosPerfParam object as a string.

        Returns:
            str: if defined, the DaosPerf parameter, otherwise an empty string

        """
        if isinstance(self.default, bool) and self.value:
            return self.str_format
        elif not isinstance(self.default, bool) and self.value:
            return self.str_format.format(self.value)
        else:
            return ""

    def set_yaml_value(self, name, test, path="/run/daos_perf/*"):
        """Set the value of the parameter using the test's yaml file value.

        Args:
            name (str): name associated with the value in the yaml file
            test (Test): avocado Test object
            path (str, optional): yaml namespace. Defaults to "/run/daos_perf/*"
        """
        self.value = test.params.get(name, path, self.default)


class DaosPerfCommand(object):
    """Defines a object representing a daos_perf command."""

    def __init__(self):
        """Create an DaosPerfCommand object."""
        self.flags = DaosPerfParam("{}")                 # daos_perf flags
        self.pool_size_scm = DaosPerfParam("-P {}")      # Pool SCM partition
                                                         # size
        self.pool_size_nvme = DaosPerfParam("-N {}")     # Pool NVMe partition
                                                         # size
        self.test_mode = DaosPerfParam("-T {}")          # Type of test, it can
                                                         # be 'vos', 'echo' and
                                                         # 'daos'
        self.credits = DaosPerfParam("-C {}")            # Credits for
                                                         # concurrently
                                                         # asynchronous I/O.
                                                         # It can be value
                                                         # between 1 and 64.
        self.oclass = DaosPerfParam("-c {}")             # Object class for
                                                         # DAOS full stack test
        self.num_of_objects = DaosPerfParam("-o {}")     # Number of objects
                                                         # are used by the
                                                         # utility
        self.dkeys = DaosPerfParam("-d {}")              # Number of dkeys per
                                                         # object
        self.akeys = DaosPerfParam("-a {}")              # Number of akeys per
                                                         # dkey
        self.records = DaosPerfParam("-r {}")            # Number of records per
                                                         # akey
        self.single_value_size = DaosPerfParam("-s {}")  # Size of single value
        self.specify_seed = DaosPerfParam("-G {}")       # -G x to specify a
                                                         # seed vs using a
                                                         # random one if not
                                                         # specified
        self.pathname = DaosPerfParam("-f {}")           # Full path name of the
                                                         # VOS file

    def __str__(self):
        """Return a DaosPerfCommand object as a string.

        Returns:
            str: the daos_perf command with all the defined parameters

        """
        params = []
        for value in self.__dict__.values():
            value_str = str(value)
            if value_str != "":
                params.append(value_str)
        return " ".join(["daos_perf"] + sorted(params))

    def set_params(self, test, path="/run/daos_perf/*"):
        """Set values for all of the daos_perf command params using a yaml file.

        Args:
            test (Test): avocado Test object
            path (str, optional): yaml namespace. Defaults to "/run/daos_perf/*"
        """
        for name, daos_perf_param in self.__dict__.items():
            daos_perf_param.set_yaml_value(name, test, path)

    def get_launch_command(self, basepath, processes, hostfile, runpath=None):
        """Get the process launch command used to run daos_perf.

        Args:
            basepath (str): DAOS base path
            processes (int): number of host processes
            hostfile (str): file defining host names and slots

        Returns:
            str: returns daos_perf command

        """
        attach_info_path = os.path.join(basepath, "install/tmp")

        load_mpi('openmpi')
        orterun_bin = find_executable('orterun')
        if orterun_bin is None:
            raise DaosPerfFailed("orterun not found")

        orterun_cmd = [
            orterun_bin,
            "-np {}".format(processes),
            "--hostfile {}".format(hostfile),
            "--map-by node",
            "-x DAOS_SINGLETON_CLI=1",
            "-x CRT_ATTACH_INFO_PATH={}".format(attach_info_path),
        ]
        command = " ".join(orterun_cmd + [self.__str__()])

        return command

    def run(self, basepath, processes, hostfile, display=True, path=None):
        """Run the daos_perf command.

        Args:
            basepath (str): DAOS base path
            processes (int): number of host processes
            hostfile (str): file defining host names and slots
            display (bool, optional): print daos_perf output to the console.
                Defaults to True.

        Raises:
            DaosPerfFailed: if an error occured runnig the daos_perf command

        """
        command = self.get_launch_command(basepath, processes, hostfile, path)
        if display:
            print("<daos_perf CMD>: {}".format(command))

        # Run daos_perf
        try:
            result = run_command(command, verbose=display)
        except DaosTestError as error:
            print(
                "<DaosPerfRunFailed> Exception occurred: {0}".format(
                    str(error)))
            raise DaosPerfFailed(
                "Daos_Perf Run process Failed: {}".format(error))

        if result.exit_status != 0:
            raise DaosPerfFailed(
                "Daos_Perf run process failed: {}".format(result.exit_status))
