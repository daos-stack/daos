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
import subprocess
import json


class IorFailed(Exception):
    """Raise if Ior failed."""


class IorParam(object):
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
    """Defines a object representing a IOR command."""

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
        params = []
        for value in self.__dict__.values():
            value_str = str(value)
            if value_str != "":
                params.append(value_str)
        return " ".join(["ior"] + sorted(params))

    def set_params(self, test, path="/run/ior/*"):
        """Set values for all of the ior command params using a yaml file.

        Args:
            test (Test): avocado Test object
            path (str, optional): yaml namespace. Defaults to "/run/ior/*".

        """
        for name, ior_param in self.__dict__.items():
            ior_param.set_yaml_value(name, test, path)

    def get_launch_command(self, basepath, processes, hostfile, runpath=None):
        """Get the process launch command used to run IOR.

        Args:
            basepath (str): DAOS base path
            processes (int): number of host processes
            hostfile (str): file defining host names and slots

        Raises:
            IorFailed: if an error occured building the IOR command

        """
        with open(os.path.join(basepath, ".build_vars.json")) as afile:
            build_paths = json.load(afile)
        attach_info_path = os.path.join(basepath, "install/tmp")

        # # Verify required params are defined
        # for name in ("daos_pool", "daos_cont", "daos_svcl"):
        #     value = getattr(self, name)
        #     if str(value) == "":
        #         raise IorFailed("Missing required {} param".format(name))

        if self.api.value == "MPIIO":
            env = {
                "CRT_ATTACH_INFO_PATH": attach_info_path,
                "DAOS_POOL": str(self.daos_pool),
                "MPI_LIB": "",
                "DAOS_SVCL": str(self.daos_svcl),
                "DAOS_SINGLETON_CLI": 1,
                "FI_PSM2_DISCONNECT": 1,
            }
            export_cmd = [
                "export {}={}".format(key, val) for key, val in env.items()]
            mpirun_cmd = [
               os.path.join(
                   runpath if runpath else build_paths[
                       "OMPI_PREFIX"], "bin/mpirun"), "-np {}".format(
                           processes), "--hostfile {}".format(hostfile),
            ]
            command = " ".join(mpirun_cmd + [self.__str__()])
            command = "; ".join(export_cmd + command)

        elif self.api.value == "DAOS":
            orterun_cmd = [
                os.path.join(runpath if runpath else build_paths[
                    "OMPI_PREFIX"], "bin/orterun"),
                "-np {}".format(processes),
                "--hostfile {}".format(hostfile),
                "--map-by node",
                "-x DAOS_SINGLETON_CLI=1",
                "-x CRT_ATTACH_INFO_PATH={}".format(attach_info_path),
            ]
            command = " ".join(orterun_cmd + [self.__str__()])

        else:
            raise IorFailed("Unsupported IOR API: {}".format(self.api))

        return command

    def run(self, basepath, processes, hostfile, display=True, path=None):
        """Run the IOR command.

        Args:
            basepath (str): DAOS base path
            processes (int): number of host processes
            hostfile (str): file defining host names and slots
            display (bool, optional): print IOR output to the console.
                Defaults to True.

        Raises:
            IorFailed: if an error occured runnig the IOR command

        """
        command = self.get_launch_command(basepath, processes, hostfile, path)
        if display:
            print("<IOR CMD>: {}".format(command))

        # Run IOR
        try:
            process = subprocess.Popen(
                command, stdout=subprocess.PIPE, shell=True)
            while True:
                output = process.stdout.readline()
                if output == "" and process.poll() is not None:
                    break
                if output and display:
                    print(output.strip())
            if process.poll() != 0:
                raise IorFailed(
                    "IOR Run process Failed with non zero exit code:{}".format(
                        process.poll()))

        except (OSError, ValueError) as error:
            print("<IorRunFailed> Exception occurred: {0}".format(str(error)))
            raise IorFailed("IOR Run process Failed: {}".format(error))


def run_ior_daos(client_file, ior_flags, iteration, block_size, transfer_size,
                 pool_uuid, svc_list, object_class, basepath, client_processes,
                 cont_uuid="`uuidgen`", seg_count=1, chunk_size=1048576,
                 display_output=True):
    """Run IOR test.

    Args:
        client_file (str): file holding client hostname and slots
        ior_flags (str): all ior specific flags
        iteration (int): number of iterations for ior run
        block_size (int): contiguous bytes to write per task
        transfer_size (int): size of transfer in bytes
        pool_uuid (str): DAOS pool uuid
        svc_list (str): DAOS pool svcl
        object_class (str): DAOS object class designation
        basepath (str): DAOS base path
        client_processes (int): number of client processes
        cont_uuid (str, optional): container uuid. Defaults to "`uuidgen`".
        seg_count (int, optional): segment count. Defaults to 1.
        chunk_size (int, optional): chunk size in bytes. Defaults to 1048576.
        display_output (bool, optional): print IOR output. Defaults to True.
    """
    ior_cmd = IorCommand()
    ior_cmd.api.value = "DAOS"
    ior_cmd.flags.value = ior_flags
    ior_cmd.repetitions.value = iteration
    ior_cmd.block_size.value = block_size
    ior_cmd.transfer_size.value = transfer_size
    ior_cmd.daos_pool.value = pool_uuid
    ior_cmd.daos_svcl.value = svc_list
    ior_cmd.daos_oclass.value = object_class
    ior_cmd.daos_cont.value = cont_uuid
    ior_cmd.segment_count.value = seg_count
    ior_cmd.daos_chunk.value = chunk_size

    ior_cmd.run(
        basepath, client_processes, client_file, display_output)


def run_ior_mpiio(basepath, mpichinstall, pool_uuid, svcl, np, hostfile,
                  ior_flags, iteration, transfer_size, block_size,
                  display_output=True):
    """Run IOR over mpich.

    Args:
        basepath (str): DAOS base path
        mpichinstall (str): location of installed mpich
        pool_uuid (str): DAOS pool uuid
        svcl (str): DAOS pool svcl
        np (int): number of client processes
        hostfile (str): file holding client hostname and slots
        ior_flags (str): all ior specific flags
        iteration (int): number of iterations for ior run
        transfer_size (int): size of transfer in bytes
        block_size (int): contiguous bytes to write per task
        display_output (bool, optional): print IOR output. Defaults to True.
    """
    ior_cmd = IorCommand()
    ior_cmd.api.value = "MPIIO"
    ior_cmd.flags.value = ior_flags
    ior_cmd.repetitions.value = iteration
    ior_cmd.block_size.value = block_size
    ior_cmd.transfer_size.value = transfer_size
    ior_cmd.daos_pool.value = pool_uuid
    ior_cmd.daos_svcl.value = svcl
    ior_cmd.test_file.value = "daos:testFile"

    ior_cmd.run(
        basepath, np, hostfile, display_output, mpichinstall)
