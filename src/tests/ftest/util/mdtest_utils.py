#!/usr/bin/python
'''
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
'''

from __future__ import print_function
import os
import subprocess
import json

class MdtestFailed(Exception):
    """Raise if mdtest failed"""

#pylint: disable=R0903
class MdtestParam(object):
    """
    Defines a object representing a single mdtest command line parameter.
    """

    def __init__(self, str_format, default=None):
        """Create a MdtestParam object.
        Args:
            str_format (str): format string used to convert the value into an
                mdtest command line argument string
            default (object): default value for the param
        """
        self.str_format = str_format
        self.default = default
        self.value = default

    def __str__(self):
        """Return a MdtestParam object as a string.
        Returns:
            str: if defined, the Mdtest parameter, otherwise an empty string
        """
        if isinstance(self.default, bool) and self.value:
            return self.str_format
        elif not isinstance(self.default, bool) and self.value:
            return self.str_format.format(self.value)
        else:
            return ""

    def set_yaml_value(self, name, test, path="/run/mdtest/*"):
        """Set the value of the parameter using the test's yaml file value.
        Args:
            name (str): name associated with the value in the yaml file
            test (Test): avocado Test object
            path (str, optional): yaml namespace. Defaults to "/run/mdtest/*"
        """
        self.value = test.params.get(name, path, self.default)


class MdtestCommand(object):
    """Defines a object representing a mdtest command."""

    def __init__(self):
        """Create an MdtestCommand object."""
        self.flags = MdtestParam("{}")                 # mdtest flags
        self.api = MdtestParam("-a {}")                # API for I/O
                                                       # [POSIX|DAOS|DFS]
        self.branching_factor = MdtestParam("-b {}")   # branching factor
        self.test_dir = MdtestParam("-d {}")           # test directory in
                                                       # which tests should run
        self.barriers = MdtestParam("-B {}")           # no barriers between
                                                       # phases
        self.read_bytes = MdtestParam("-e {}")         # bytes to read from
                                                       # each file
        self.first_num_tasks = MdtestParam("-f {}")    # first number of tasks
                                                       # on which the test will
                                                       # run
        self.iteration = MdtestParam("-i {}")          # number of iterations
        self.items = MdtestParam("-I {}")              # number of items per
                                                       # directory in tree
        self.last_num_tasks = MdtestParam("-l {}")     # last number of tasks
        self.num_of_tasks = MdtestParam("-n {}")       # every process will
                                                       # creat/stat/read/remove
                                                       # num of  directories and
                                                       # files
        self.pre_iter = MdtestParam("-p {}")           # pre-iteration delay
                                                       # (in seconds)
        self.random_seed = MdtestParam("--random-seed {}") # random seed for -R
                                                           # flag
        self.stride = MdtestParam("-s {}")             # stride between the
                                                       # number of tasks
        self.verbosity_value = MdtestParam("-V {}")    # verbosity value
        self.write_bytes = MdtestParam("-w {}")        # bytes to write to each
                                                       # file
        self.stonewall_timer = MdtestParam("-W {}")    # stonewall timer
                                                       # (in secs)
        self.stonewall_statusfile = MdtestParam("-x {}") # num of iter of the
                                                       # creation phase,
                                                       # StoneWallingStatusFile
        self.depth = MdtestParam("-z {}")              # depth of hierarchical
                                                       # directory structure

        self.daos_pool_uuid = MdtestParam("--daos.pool {}") # pool uuid
        self.daos_svcl = MdtestParam("--daos.svcl {}")      # pool svcl
        self.daos_cont = MdtestParam("--daos.cont {}")      # cont uuid
        self.daos_group = MdtestParam("--daos.group {}")    # server group
        self.daos_chunk_size = MdtestParam(" --daos.chunk_size {}") # chunk
                                                                    # size
        self.daos_oclass = MdtestParam("--daos.oclass {}")  # object class

        self.dfs_pool_uuid = MdtestParam("--dfs.pool {}") # pool uuid
        self.dfs_svcl = MdtestParam("--dfs.svcl {}")      # pool svcl
        self.dfs_cont = MdtestParam("--dfs.cont {}")      # cont uuid
        self.dfs_group = MdtestParam("--dfs.group {}")    # server group

    def __str__(self):
        """Return a MdtestCommand object as a string.
        Returns:
            str: the mdtest command with all the defined parameters
        """
        params = []
        for value in self.__dict__.values():
            value_str = str(value)
            if value_str != "":
                params.append(value_str)
        return " ".join(["mdtest"] + sorted(params))

    def set_params(self, test, path="/run/mdtest/*"):
        """Set values for all of the mdtest command params using a yaml file.
        Args:
            test (Test): avocado Test object
            path (str, optional): yaml namespace. Defaults to "/run/mdtest/*"
        """
        for name, mdtest_param in self.__dict__.items():
            mdtest_param.set_yaml_value(name, test, path)

    def get_launch_command(self, basepath, processes, hostfile, runpath=None):
        """Get the process launch command used to run mdtest
        Args:
            basepath (str): DAOS base path
            processes (int): number of host processes
            hostfile (str): file defining host names and slots
        Returns:
            str: returns mdtest command
        """
        with open(os.path.join(basepath, ".build_vars.json")) as afile:
            build_paths = json.load(afile)
        attach_info_path = os.path.join(basepath, "install/tmp")

        env = {
            "CRT_ATTACH_INFO_PATH": attach_info_path,
            "MPI_LIB": "",
            "DAOS_SINGLETON_CLI": 1,
#            "DAOS_IO_MODE": 1,
        }
        export_cmd = [
            "export {}={}".format(key, val) for key, val in env.items()]
        mpirun_cmd = [
            "".join([runpath if runpath else build_paths["OMPI_PREFIX"],
                     "/bin/mpirun"]),
            "-np {}".format(processes),
            "--hostfile {}".format(hostfile),
        ]
        command = " ".join(mpirun_cmd + [self.__str__()])
        command = "; ".join(export_cmd + [command])

        return command

    def run(self, basepath, processes, hostfile, display=True, path=None):
        """Run the mdtest command.
        Args:
            basepath (str): DAOS base path
            processes (int): number of host processes
            hostfile (str): file defining host names and slots
            display (bool, optional): print mdtest output to the console.
                Defaults to True.
        Raises:
            MdtestFailed: if an error occured runnig the mdtest command
        """
        command = self.get_launch_command(basepath, processes, hostfile, path)
        if display:
            print("<mdtest CMD>: {}".format(command))

        # Run mdtest
        try:
            process = subprocess.Popen(
                command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                shell=True)
            while True:
                output = process.stdout.readline()
                if output == "" and process.poll() is not None:
                    break
                if output and display:
                    print(output.strip())
            if process.poll() != 0:
                print("process.poll: {}".format(process.poll()))
                raise MdtestFailed(
                    "Mdtest run process failed: {}".format(
                        process.poll()))

        except (OSError, ValueError) as error:
            print("<MdtestRunFailed> Exception occurred: {0}".
                  format(str(error)))
            raise MdtestFailed("Mdtest Run process Failed: {}".
                               format(error))
