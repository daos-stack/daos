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

import os
import json
import re
import uuid

from avocado.utils.process import run, CmdError


class IorFailed(Exception):
    """Raise if Ior failed."""


class IorParam(object):
    # pylint: disable=too-few-public-methods
    """Defines a object representing a single IOR command line parameter."""

    def __init__(self, str_format, default=None):
        """Create a IorParam object.

        Args:
            str_format (str): format string used to convert the value into an
                ior command line argument string
            default (object): default value for the param
        """
        self.str_format = str_format
        self.default = default
        self.value = default

    def __str__(self):
        """Return a IorParam object as a string.

        Returns:
            str: if defined, the IOR parameter, otherwise an empty string

        """
        if isinstance(self.default, bool) and self.value:
            return self.str_format
        elif not isinstance(self.default, bool) and self.value:
            return self.str_format.format(self.value)
        else:
            return ""

    def set_yaml_value(self, name, test, path="/run/ior/*"):
        """Set the value of the parameter using the test's yaml file value.

        Args:
            name (str): name associated with the value in the yaml file
            test (Test): avocado Test object
            path (str, optional): yaml namespace. Defaults to "/run/ior/*".

        """
        self.value = test.params.get(name, path, self.default)


class IorCommand(object):
    """Defines a object for executing an IOR command.

    Example:
        >>> # Typical use inside of a DAOS avocado test method.
        >>> ior_cmd = IorCommand()
        >>> ior_cmd.set_params(self)
        >>> ior_cmd.set_daos_params(self.server_group, self.pool)
        >>> ior_cmd.run(
                self.basepath, len(self.hostlist_clients),
                self.hostfile_clients)
    """

    def __init__(self):
        """Create an IorCommand object."""
        self.flags = IorParam("{}")                 # IOR flags
        self.api = IorParam("-a {}", "DAOS")        # API for I/O
        self.block_size = IorParam("-b {}")         # bytes to write per task
        self.test_delay = IorParam("-d {}")         # sec delay between reps
        self.script = IorParam("-f {}")             # test script name
        self.signatute = IorParam("-G {}")          # set value for rand seed
        self.repetitions = IorParam("-i {}")        # number of repetitions
        self.outlier_threshold = IorParam("-j {}")  # warn if N sec from mean
        self.alignment = IorParam("-J {}")          # HDF5 alignment in bytes
        self.data_packet_type = IorParam("-l {}")   # type of packet created
        self.memory_per_node = IorParam("-M {}")    # hog memory on the node
        self.num_tasks = IorParam("-N {}")          # number of tasks
        self.test_file = IorParam("-o {}")          # full name for test
        self.directives = IorParam("-O {}")         # IOR directives
        self.task_offset = IorParam("-Q {}")        # for -C & -Z read tests
        self.segment_count = IorParam("-s {}")      # number of segments
        self.transfer_size = IorParam("-t {}")      # bytes to transfer
        self.max_duration = IorParam("-T {}")       # max mins executing test
        self.daos_pool = IorParam("--daos.pool {}")             # pool uuid
        self.daos_svcl = IorParam("--daos.svcl {}")             # pool SVCL
        self.daos_cont = IorParam("--daos.cont {}")             # cont uuid
        self.daos_destroy = IorParam("--daos.destroy", True)    # destroy cont
        self.daos_group = IorParam("--daos.group {}")           # server group
        self.daos_chunk = IorParam("--daos.chunk_size {}", 1048576)
        self.daos_oclass = IorParam("--daos.oclass {}")         # object class

    def __str__(self):
        """Return a IorCommand object as a string.

        Returns:
            str: the ior command with all the defined parameters

        """
        # Sort the IOR parameter names to generate consistent ior commands
        all_param_names = [name for name in sorted(self.__dict__.keys())]

        # List all of the common ior params first followed by any daos-specific
        # params (except when using MPIIO).
        param_names = [name for name in all_param_names if "daos" not in name]
        if self.api.value != "MPIIO":
            param_names.extend(
                [name for name in all_param_names if "daos" in name])

        # Join all the IOR parameters that have been assigned a value to create
        # the IOR command string
        params = []
        for value_str in [str(getattr(self, name)) for name in param_names]:
            if value_str != "":
                params.append(value_str)
        return " ".join(["ior"] + params)

    def set_params(self, test, path="/run/ior/*"):
        """Set values for all of the ior command params using a yaml file.

        Sets the IorParam object's value to a yaml key that matches the
        IoRCommand's attribute name.  For example, the self.block_size.value
        will be set to the value in the yaml file with the key 'block_size'.
        If no key matches are found in the yaml file the IorParam object will
        be set to its defult value.

        Args:
            test (Test): avocado Test object
            path (str, optional): yaml namespace. Defaults to "/run/ior/*".

        """
        for name, ior_param in self.__dict__.items():
            if isinstance(ior_param, IorParam):
                ior_param.set_yaml_value(name, test, path)

    def set_daos_params(self, group, pool, cont_uuid=None, display=True):
        """Set the IOR parameters for the DAOS group, pool, and container uuid.

        Args:
            group (str): DAOS server group name
            pool (DaosPool): DAOS pool API object
            cont_uuid (str, optional): the container uuid. If not specified one
                is generated. Defaults to None.
            display (bool, optional): print updated params. Defaults to True.
        """
        self.daos_group.value = group
        self.set_daos_pool_params(pool, display)
        self.daos_cont.value = cont_uuid if cont_uuid else uuid.uuid4()
        if display:
            print("Updated DOAS IOR param: {}".format(str(self.daos_cont)))
            print("Updated DOAS IOR param: {}".format(str(self.daos_group)))

    def set_daos_pool_params(self, pool, display=True):
        """Set the IOR parameters that are based on a DAOS pool.

        Args:
            pool (DaosPool): DAOS pool API object
            display (bool, optional): print updated params. Defaults to True.
        """
        self.daos_pool.value = pool.get_uuid_str()
        self.set_daos_svcl_param(pool, display)
        if display:
            print("Updated DOAS IOR param: {}".format(str(self.daos_pool)))

    def set_daos_svcl_param(self, pool, display):
        """Set the IOR daos_svcl param from the ranks of a DAOS pool object.

        Args:
            pool (DaosPool): DAOS pool API object
            display (bool, optional): print updated params. Defaults to True.
        """
        self.daos_svcl.value = ":".join(
            [str(item) for item in [
                int(pool.svc.rl_ranks[index])
                for index in range(pool.svc.rl_nr)]])
        if display:
            print("Updated DOAS IOR param: {}".format(str(self.daos_svcl)))

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
                if len(sub_item) > 0:
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

    def get_launch_command(self, basepath, processes, hostfile, runpath=None):
        """Get the process launch command used to run IOR.

        Args:
            basepath (str): DAOS base path
            processes (int): number of host processes
            hostfile (str): file defining host names and slots
            runpath (str, optional): Optional path to the mpirun/oretrun
                command. Defaults to None.

        Raises:
            IorFailed: if an error occured building the IOR command

        Returns:
            str: ior launch command

        """
        with open(os.path.join(basepath, ".build_vars.json")) as afile:
            build_paths = json.load(afile)
        attach_info_path = os.path.join(basepath, "install/tmp")

        if self.api.value == "MPIIO":
            env = {
                "CRT_ATTACH_INFO_PATH": attach_info_path,
                "DAOS_POOL": self.daos_pool.value,
                "MPI_LIB": "",
                "DAOS_SVCL": self.daos_svcl.value,
                "DAOS_SINGLETON_CLI": 1,
                "FI_PSM2_DISCONNECT": 1,
            }
            export_cmd = [
                "export {}={}".format(key, val) for key, val in env.items()]
            mpirun_cmd = [
                os.path.join(
                    runpath if runpath else build_paths["OMPI_PREFIX"],
                    "bin/mpirun"),
                "-np {}".format(processes),
                "--hostfile {}".format(hostfile),
            ]
            command = " ".join(mpirun_cmd + [self.__str__()])
            command = "; ".join(export_cmd + [command])

        elif self.api.value == "DAOS":
            orterun_cmd = [
                os.path.join(
                    runpath if runpath else build_paths["OMPI_PREFIX"],
                    "bin/orterun"),
                "-np {}".format(processes),
                "--hostfile {}".format(hostfile),
                "--map-by node",
                "-x DAOS_SINGLETON_CLI=1",
                "-x CRT_ATTACH_INFO_PATH={}".format(attach_info_path),
            ]
            command = " ".join(orterun_cmd + [self.__str__()])

        else:
            raise IorFailed("Unsupported IOR API: {}".format(self.api.value))

        return command

    def run(self, basepath, processes, hostfile, display=True, path=None):
        """Run the IOR command.

        Args:
            basepath (str): DAOS base path
            processes (int): number of host processes
            hostfile (str): file defining host names and slots
            display (bool, optional): print IOR output to the console.
                Defaults to True.
            path (str, optional): Optional path to the mpirun/oretrun command.
                Defaults to None.

        Raises:
            IorFailed: if an error occured runnig the IOR command

        """
        command = self.get_launch_command(basepath, processes, hostfile, path)
        if display:
            print("<IOR CMD>: {}".format(command))

        # Run IOR
        try:
            run(command, allow_output_check="combined", shell=True)

        except CmdError as error:
            print("<IorRunFailed> Exception occurred: {}".format(error))
            raise IorFailed("IOR Run process Failed: {}".format(error))
