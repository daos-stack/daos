#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

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

import re
import uuid
import daos_api

from avocado.utils.process import run, CmdError
from command_utils import FormattedParameter, CommandWithParameters


class IorFailed(Exception):
    """Raise if Ior failed."""

class IorCommand(CommandWithParameters):
    """Defines a object for executing an IOR command.

    Example:
        >>> # Typical use inside of a DAOS avocado test method.
        >>> ior_cmd = IorCommand()
        >>> ior_cmd.get_params(self)
        >>> ior_cmd.set_daos_params(self.server_group, self.pool)
        >>> ior_cmd.run(
                self.basepath, len(self.hostlist_clients),
                self.hostfile_clients)
    """

    def __init__(self):
        """Create an IorCommand object."""
        super(IorCommand, self).__init__("ior")

        # Flags
        self.flags = FormattedParameter("{}")

        # Optional arguments
        #   -a=POSIX        API for I/O [POSIX|DUMMY|MPIIO|MMAP|DAOS|DFS]
        #   -b=1048576      blockSize -- contiguous bytes to write per task
        #   -d=0            interTestDelay -- delay between reps in seconds
        #   -f=STRING       scriptFile -- test script name
        #   -G=0            setTimeStampSignature -- time stamp signature
        #   -i=1            repetitions -- number of repetitions of test
        #   -j=0            outlierThreshold -- warn on outlier N sec from mean
        #   -J=1            setAlignment -- HDF5 alignment in bytes
        #   -l=STRING       datapacket type-- type of packet created
        #   -M=STRING       memoryPerNode -- hog memory on the node
        #   -N=0            numTasks -- num of participating tasks in the test
        #   -o=testFile     testFile -- full name for test
        #   -O=STRING       string of IOR directives
        #   -Q=1            taskPerNodeOffset for read tests
        #   -s=1            segmentCount -- number of segments
        #   -t=262144       transferSize -- size of transfer in bytes
        #   -T=0            maxTimeDuration -- max time in minutes executing
        #                      repeated test; it aborts only between iterations
        #                      and not within a test!
        self.api = FormattedParameter("-a {}", "DAOS")
        self.block_size = FormattedParameter("-b {}")
        self.test_delay = FormattedParameter("-d {}")
        self.script = FormattedParameter("-f {}")
        self.signatute = FormattedParameter("-G {}")
        self.repetitions = FormattedParameter("-i {}")
        self.outlier_threshold = FormattedParameter("-j {}")
        self.alignment = FormattedParameter("-J {}")
        self.data_packet_type = FormattedParameter("-l {}")
        self.memory_per_node = FormattedParameter("-M {}")
        self.num_tasks = FormattedParameter("-N {}")
        self.test_file = FormattedParameter("-o {}")
        self.directives = FormattedParameter("-O {}")
        self.task_offset = FormattedParameter("-Q {}")
        self.segment_count = FormattedParameter("-s {}")
        self.transfer_size = FormattedParameter("-t {}")
        self.max_duration = FormattedParameter("-T {}")

        # Module DAOS
        #   Required arguments
        #       --daos.pool=STRING            pool uuid
        #       --daos.svcl=STRING            pool SVCL
        #       --daos.cont=STRING            container uuid
        #   Flags
        #       --daos.destroy                Destroy Container
        #   Optional arguments
        #       --daos.group=STRING           server group
        #       --daos.chunk_size=1048576     chunk size
        #       --daos.oclass=STRING          object class
        self.daos_pool = FormattedParameter("--daos.pool {}")
        self.daos_svcl = FormattedParameter("--daos.svcl {}")
        self.daos_cont = FormattedParameter("--daos.cont {}")
        self.daos_destroy = FormattedParameter("--daos.destroy", True)
        self.daos_group = FormattedParameter("--daos.group {}")
        self.daos_chunk = FormattedParameter("--daos.chunk_size {}", 1048576)
        self.daos_oclass = FormattedParameter("--daos.oclass {}", "SX")

    def get_param_names(self):
        """Get a sorted list of the defined IorCommand parameters."""
        # Sort the IOR parameter names to generate consistent ior commands
        all_param_names = super(IorCommand, self).get_param_names()

        # List all of the common ior params first followed by any daos-specific
        # params (except when using MPIIO).
        param_names = [name for name in all_param_names if "daos" not in name]
        if self.api.value != "MPIIO":
            param_names.extend(
                [name for name in all_param_names if "daos" in name])

        return param_names

    def get_params(self, test, path="/run/ior/*"):
        """Get values for all of the ior command params using a yaml file.

        Sets each BasicParameter object's value to the yaml key that matches
        the assigned name of the BasicParameter object in this class. For
        example, the self.block_size.value will be set to the value in the yaml
        file with the key 'block_size'.

        Args:
            test (Test): avocado Test object
            path (str, optional): yaml namespace. Defaults to "/run/ior/*".

        """
        super(IorCommand, self).get_params(test, path)

    def set_daos_params(self, group, pool, cont_uuid=None, display=True):
        """Set the IOR parameters for the DAOS group, pool, and container uuid.

        Args:
            group (str): DAOS server group name
            pool (DaosPool): DAOS pool API object
            cont_uuid (str, optional): the container uuid. If not specified one
                is generated. Defaults to None.
            display (bool, optional): print updated params. Defaults to True.
        """
        self.set_daos_pool_params(pool, display)
        self.daos_group.update(group, "daos_group" if display else None)
        self.daos_cont.update(
            cont_uuid if cont_uuid else uuid.uuid4(),
            "daos_cont" if display else None)

    def set_daos_pool_params(self, pool, display=True):
        """Set the IOR parameters that are based on a DAOS pool.

        Args:
            pool (DaosPool): DAOS pool API object
            display (bool, optional): print updated params. Defaults to True.
        """
        #self.daos_pool.value = pool.uuid
        self.daos_pool.update(
            pool.pool.get_uuid_str(), "daos_pool" if display else None)
        self.set_daos_svcl_param(pool, display)

    def set_daos_svcl_param(self, pool, display=True):
        """Set the IOR daos_svcl param from the ranks of a DAOS pool object.

        Args:
            pool (DaosPool): DAOS pool API object
            display (bool, optional): print updated params. Defaults to True.
        """
        svcl = ":".join(
            [str(item) for item in [
                int(pool.pool.svc.rl_ranks[index])
                for index in range(pool.pool.svc.rl_nr)]])
        self.daos_svcl.update(svcl, "daos_svcl" if display else None)

    def get_aggregate_total(self, processes):
        """Get the total bytes expected to be written by ior.

        Args:
            processes (int): number of processes running the ior command

        Returns:
            int: total number of bytes written

        """
        power = {"k": 1, "m": 2, "g": 3, "t": 4}
        total = processes
        for name in ("block_size", "segment_count"):
            item = getattr(self, name).value
            if item:
                sub_item = re.split(r"([^\d])", str(item))
                if sub_item > 0:
                    total *= int(sub_item[0])
                    if len(sub_item) > 1:
                        key = sub_item[1].lower()
                        if key in power:
                            total *= 1024**power[key]
                        else:
                            raise IorFailed(
                                "Error obtaining the IOR aggregate total from "
                                "the {} - bad key: value: {}, split: {}, "
                                "key: {}".format(name, item, sub_item, key))
                else:
                    raise IorFailed(
                        "Error obtaining the IOR aggregate total from the {}: "
                        "value: {}, split: {}".format(name, item, sub_item))

        # Account for any replicas
        try:
            # Extract the replica quantity from the object class string
            replica_qty = int(re.findall(r"\d+", self.daos_oclass.value)[0])
        except (TypeError, IndexError):
            # If the daos object class is undefined (TypeError) or it does not
            # contain any numbers (IndexError) then there is only one replica
            replica_qty = 1
        finally:
            total *= replica_qty

        return total

    def get_launch_command(self, manager, attach_info, processes, hostfile):
        """Get the process launch command used to run IOR.

        Args:
            manager (str): mpi job manager command
            attach_info (str): CART attach info path
            mpi_prefix (str): path for the mpi launch command
            processes (int): number of host processes
            hostfile (str): file defining host names and slots

        Raises:
            IorFailed: if an error occured building the IOR command

        Returns:
            str: ior launch command

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
                "IOR_HINT__MPI__romio_daos_obj_class":
                daos_api.get_object_class("OC_{}".\
                format(self.daos_oclass.value)).value,
            })
            assign_env = ["{}={}".format(key, val) for key, val in env.items()]
            exports = "export {}; ".format("; export ".join(assign_env))
            args = [
                "-np {}".format(processes),
                "-hostfile {}".format(hostfile),
                # "-map-by node",
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
                args.append("--distribution=cyclic")            # --map-by node
            if hostfile is not None:
                args.append("--nodefile={}".format(hostfile))

        else:
            raise IorFailed("Unsupported job manager: {}".format(manager))

        return "{}{} {} {}".format(
            exports, manager, " ".join(args), self.__str__())

    def run(self, manager, attach_info, processes, hostfile, display=True):
        """Run the IOR command.

        Args:
            manager (str): mpi job manager command
            attach_info (str): CART attach info path
            processes (int): number of host processes
            hostfile (str): file defining host names and slots
            display (bool, optional): print IOR output to the console.
                Defaults to True.

        Raises:
            IorFailed: if an error occured runnig the IOR command

        """
        command = self.get_launch_command(
            manager, attach_info, processes, hostfile)
        if display:
            print("<IOR CMD>: {}".format(command))

        # Run IOR
        try:
            run(command, allow_output_check="combined", shell=True)

        except CmdError as error:
            print("<IorRunFailed> Exception occurred: {}".format(error))
            raise IorFailed("IOR Run process Failed: {}".format(error))
