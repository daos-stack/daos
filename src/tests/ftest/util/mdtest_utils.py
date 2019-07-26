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

import re
import uuid

from avocado.utils.process import run, CmdError
from command_utils import FormattedParameter, CommandWithParameters

class MdtestFailed(Exception):
    """Raise if mdtest failed"""

class MdtestCommand(CommandWithParameters):
    """Defines a object representing a mdtest command."""

    def __init__(self):
        """Create an MdtestCommand object."""
        super(MdtestCommand, self).__init__("mdtest")
        self.flags = FormattedParameter("{}")               # mdtest flags
        # Optional arguments
        #  -a=STRING             API for I/O [POSIX|DUMMY]
        #  -b=1                  branching factor of hierarchical dir structure
        #  -d=./out              the directory in which the tests will run
        #  -B=0                  no barriers between phases
        #  -e=0                  bytes to read from each file
        #  -f=1                  first number of tasks on which test will run
        #  -i=1                  number of iterations the test will run
        #  -I=0                  number of items per directory in tree
        #  -l=0                  last number of tasks on which test will run
        #  -n=0                  every process will creat/stat/read/remove num
        #                        of directories and files
        #  -N=0                  stride num between neighbor tasks for file/dir
        #                        operation (local=0)
        #  -p=0                  pre-iteration delay (in seconds)
        #  --random-seed=0       random seed for -R
        #  -s=1                  stride between number of tasks for each test
        #  -V=0                  verbosity value
        #  -w=0                  bytes to write each file after it is created
        #  -W=0                  number in seconds; stonewall timer, write as
        #                        many seconds and ensure all processes did the
        #                        same number of operations (currently only
        #                        stops during create phase)
        # -x=STRING              StoneWallingStatusFile; contains the number
        #                        of iterations of the creation phase, can be
        #                        used to split phases across runs
        # -z=0                   depth of hierarchical directory structure
        
        self.api = FormattedParameter("-a {}")
        self.branching_factor = FormattedParameter("-b {}")
        self.test_dir = FormattedParameter("-d {}")
        self.barriers = FormattedParameter("-B {}")
        self.read_bytes = FormattedParameter("-e {}")
        self.first_num_tasks = FormattedParameter("-f {}")
        self.iteration = FormattedParameter("-i {}")
        self.items = FormattedParameter("-I {}")
        self.last_num_tasks = FormattedParameter("-l {}")
        self.num_of_files_dirs = FormattedParameter("-n {}")
        self.pre_iter = FormattedParameter("-p {}")
        self.random_seed = FormattedParameter("--random-seed {}") 
        self.stride = FormattedParameter("-s {}") 
        self.verbosity_value = FormattedParameter("-V {}") 
        self.write_bytes = FormattedParameter("-w {}")
        self.stonewall_timer = FormattedParameter("-W {}") 
        self.stonewall_statusfile = FormattedParameter("-x {}")
        self.depth = FormattedParameter("-z {}")
        
        # Module DAOS
        # Required arguments
        #  --daos.pool=STRING            pool uuid
        #  --daos.svcl=STRING            pool SVCL
        #  --daos.cont=STRING            container uuid

        # Flags
        #  --daos.destroy                Destroy Container

        # Optional arguments
        #  --daos.group=STRING           server group
        #  --daos.chunk_size=1048576     chunk size
        #  --daos.oclass=STRING          object class

        self.daos_pool_uuid = FormattedParameter("--daos.pool {}")
        self.daos_svcl = FormattedParameter("--daos.svcl {}")
        self.daos_cont = FormattedParameter("--daos.cont {}")
        self.daos_group = FormattedParameter("--daos.group {}")
        self.daos_chunk_size = FormattedParameter(" --daos.chunk_size {}")
        self.daos_oclass = FormattedParameter("--daos.oclass {}") 
        
        # Module DFS
        # Required arguments
        #  --dfs.pool=STRING             DAOS pool uuid
        #  --dfs.svcl=STRING             DAOS pool SVCL
        #  --dfs.cont=STRING             DFS container uuid

        # Flags
        #  --dfs.destroy                 Destroy DFS Container

        # Optional arguments
        #  --dfs.group=STRING            DAOS server group
         
        self.dfs_pool_uuid = FormattedParameter("--dfs.pool {}")
        self.dfs_svcl = FormattedParameter("--dfs.svcl {}")
        self.dfs_cont = FormattedParameter("--dfs.cont {}")
        self.dfs_group = FormattedParameter("--dfs.group {}")

    def get_param_names(self):
        """Get a sorted list of the defined MdtestCommand parameters."""
        # Sort the Mdtest parameter names to generate consistent mdtest commands
        # all_param_names = super(MdtestCommand, self).get_param_names()
        param_names = super(MdtestCommand, self).get_param_names()

        # List all of the common ior params first followed by any daos-specific
        # params (except when using MPIIO).
        #param_names = [name for name in all_param_names if "daos" not in name]
        #if self.api.value != "MPIIO":
        #    param_names.extend(
        #        [name for name in all_param_names if "daos" in name])

        return param_names

    def get_params(self, test, path="/run/mdtest/*"):
        """Get values for all of the mdtest command params using a yaml file.
        Sets each BasicParameter object's value to the yaml key that matches
        the assigned name of the BasicParameter object in this class. For
        example, the self.depth.value will be set to the value in the yaml
        file with the key 'depth'.
        Args:
            test (Test): avocado Test object
            path (str, optional): yaml namespace. Defaults to "/run/mdtest/*".
        """
        super(MdtestCommand, self).get_params(test, path)

    def set_dfs_params(self, group, pool, cont_uuid=None, display=True):
        """Set the Mdtest parameters for the DAOS group, pool, and container uuid.
        Args:
            group (str): DAOS server group name
            pool (DaosPool): DAOS pool API object
            cont_uuid (str, optional): the container uuid. If not specified one
                is generated. Defaults to None.
            display (bool, optional): print updated params. Defaults to True.
        """
        self.set_daos_pool_params(pool, display)
        self.dfs_group.update(group, "daos_group" if display else None)
        self.dfs_cont.update(
            cont_uuid if cont_uuid else uuid.uuid4(),
            "daos_cont" if display else None)

    def set_daos_pool_params(self, pool, display=True):
        """Set the Mdtest parameters that are based on a DAOS pool.
        Args:
            pool (DaosPool): DAOS pool API object
            display (bool, optional): print updated params. Defaults to True.
        """
        self.dfs_pool.update(
            pool.get_uuid_str(), "daos_pool" if display else None)
        self.set_daos_svcl_param(pool, display)

    def set_daos_svcl_param(self, pool, display=True):
        """Set the IOR daos_svcl param from the ranks of a DAOS pool object.
        Args:
            pool (DaosPool): DAOS pool API object
            display (bool, optional): print updated params. Defaults to True.
        """
        svcl = ":".join(
            [str(item) for item in [
                int(pool.svc.rl_ranks[index])
                for index in range(pool.svc.rl_nr)]])
        self.dfs_svcl.update(svcl, "daos_svcl" if display else None)

    def get_launch_command(self, manager, attach_info, processes, hostfile):
        """Get the process launch command used to run Mdtest
        Args:
            manager (str): mpi job manager command
            attach_info (str): CART attach info path
            mpi_prefix (str): path for the mpi launch command
            processes (int): number of host processes
            hostfile (str): file defining host names and slots
        Raises:
            MdtestFailed: if an error occured building the IOR command
        Returns:
            str: mdtest launch command
        """
        print("Getting launch command for {}".format(manager))
        exports = ""
        env = {
            "CRT_ATTACH_INFO_PATH": attach_info,
            "MPI_LIB": "\"\"",
            "DAOS_SINGLETON_CLI": 1,
        }
        if manager.endswith("mpirun"):
            env.update({
                "DAOS_POOL": self.daos_pool.value,
                "DAOS_SVCL": self.daos_svcl.value,
                "FI_PSM2_DISCONNECT": 1,
            })
            assign_env = ["{}={}".format(key, val) for key, val in env.items()]
            exports = "export {}; ".format("; export ".join(assign_env))
            args = [
                "-np {}".format(processes),
                "-hostfile {}".format(hostfile),
            ]

        elif manager.endswith("orterun"):
            assign_env = ["{}={}".format(key, val) for key, val in env.items()]
            args = [
                "-np {}".format(processes),
                "-hostfile {}".format(hostfile),
                "-map-by node",
            ]
            args.extend(["-x {}".format(assign) for assign in assign_env])

        elif manager.endswith("srun"):
            env.update({
                "DAOS_POOL": self.daos_pool.value,
                "DAOS_SVCL": self.daos_svcl.value,
                "FI_PSM2_DISCONNECT": 1,
            })
            assign_env = ["{}={}".format(key, val) for key, val in env.items()]
            args = [
                "-l",
                "--mpi=pmi2",
                "--export={}".format(",".join(["ALL"] + assign_env)),
            ]
            if processes is not None:
                args.append("--ntasks={}".format(processes))
                args.append("--distribution=cyclic")
            if hostfile is not None:
                args.append("--nodefile={}".format(hostfile))

        else:
            raise MdtestFailed("Unsupported job manager: {}".format(manager))

        return "{}{} {} {}".format(
            exports, manager, " ".join(args), self.__str__())

    def run(self, manager, attach_info, processes, hostfile, display=True):
        """Run the Mdtest command.
        Args:
            manager (str): mpi job manager command
            attach_info (str): CART attach info path
            processes (int): number of host processes
            hostfile (str): file defining host names and slots
            display (bool, optional): print Mdtest output to the console.
                Defaults to True.
        Raises:
            MdtestFailed: if an error occured runnig the Mdtest command
        """
        command = self.get_launch_command(
            manager, attach_info, processes, hostfile)
        if display:
            print("<MDTEST CMD>: {}".format(command))

        # Run Mdtest
        try:
            run(command, allow_output_check="combined", shell=True)

        except CmdError as error:
            print("<MdtestRunFailed> Exception occurred: {}".format(error))
            raise IorFailed("Mdtest Run process Failed: {}".format(error))
