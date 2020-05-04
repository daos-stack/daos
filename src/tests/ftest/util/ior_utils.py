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
from enum import IntEnum

from command_utils import FormattedParameter, ExecutableCommand
from command_utils import EnvironmentVariables, CommandFailure


class IorCommand(ExecutableCommand):
    """Defines a object for executing an IOR command.

    Example:
        >>> # Typical use inside of a DAOS avocado test method.
        >>> ior_cmd = IorCommand()
        >>> ior_cmd.get_params(self)
        >>> ior_cmd.set_daos_params(self.server_group, self.pool)
        >>> mpirun = Mpirun()
        >>> log = get_log_file(self.client_log)
        >>> env = self.ior_cmd.get_default_env(self.tmp, log)
        >>> processes = len(self.hostlist_clients)
        >>> mpirun.setup_command(env, self.hostfile_clients, processes)
        >>> mpirun.run()
    """

    def __init__(self):
        """Create an IorCommand object."""
        super(IorCommand, self).__init__("/run/ior/*", "ior")

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
        if self.api.value not in ["MPIIO", "POSIX"]:
            param_names.extend(
                [name for name in all_param_names if "daos" in name])

        return param_names

    def set_daos_params(self, group, pool, cont_uuid=None, display=True):
        """Set the IOR parameters for the DAOS group, pool, and container uuid.

        Args:
            group (str): DAOS server group name
            pool (TestPool): DAOS test pool object
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
            pool (TestPool): DAOS test pool object
            display (bool, optional): print updated params. Defaults to True.
        """
        self.daos_pool.update(
            pool.pool.get_uuid_str(), "daos_pool" if display else None)
        self.set_daos_svcl_param(pool, display)

    def set_daos_svcl_param(self, pool, display=True):
        """Set the IOR daos_svcl param from the ranks of a DAOS pool object.

        Args:
            pool (TestPool): DAOS test pool object
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

        Raises:
            CommandFailure: if there is an error obtaining the aggregate total

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
                            raise CommandFailure(
                                "Error obtaining the IOR aggregate total from "
                                "the {} - bad key: value: {}, split: {}, "
                                "key: {}".format(name, item, sub_item, key))
                else:
                    raise CommandFailure(
                        "Error obtaining the IOR aggregate total from the {}: "
                        "value: {}, split: {}".format(name, item, sub_item))

        # Account for any replicas, except for the ones with no replication
        # i.e all object classes starting with "S". Eg: S1,S2,...,SX.
        if not self.daos_oclass.value.startswith("S"):
            try:
                # Extract the replica quantity from the object class string
                replica_qty = int(re.findall(r"\d+", self.daos_oclass.value)[0])
            except (TypeError, IndexError):
                # If the daos object class is undefined (TypeError) or it does
                # not contain any numbers (IndexError) then there is only one
                # replica.
                replica_qty = 1
            finally:
                total *= replica_qty

        return total

    def get_default_env(self, manager_cmd, attach_info, log_file=None):
        """Get the default enviroment settings for running IOR.

        Args:
            manager_cmd (str): job manager command
            attach_info (str): CART attach info path
            log_file (str, optional): log file. Defaults to None.

        Returns:
            EnvironmentVariables: a dictionary of environment names and values

        """
        env = EnvironmentVariables()
        env["CRT_ATTACH_INFO_PATH"] = attach_info
        env["MPI_LIB"] = "\"\""
        env["DAOS_SINGLETON_CLI"] = 1
        env["FI_PSM2_DISCONNECT"] = 1
        if log_file:
            env["D_LOG_FILE"] = log_file

        if "mpirun" in manager_cmd or "srun" in manager_cmd:
            env["DAOS_POOL"] = self.daos_pool.value
            env["DAOS_SVCL"] = self.daos_svcl.value
            env["DAOS_CONT"] = self.daos_cont.value
            env["FI_PSM2_DISCONNECT"] = 1
            env["IOR_HINT__MPI__romio_daos_obj_class"] = self.daos_oclass.value

        return env

    @staticmethod
    def get_ior_metrics(cmdresult):
        """Parse the CmdResult (output of the test) and look for
           the ior stdout and get the read and write metrics.

        Args:
            cmdresult (CmdResult): output of job manager

       Returns:
            metrics (tuple) : list of write and read metrics from ior run

        """
        ior_metric_summary = "Summary of all tests:"
        messages = cmdresult.stdout.splitlines()
        # Get the index whre the summary starts and add one to
        # get to the header.
        idx = messages.index(ior_metric_summary)
        # idx + 1 is header.
        # idx +2 and idx + 3 will give the write and read metrics.
        write_metrics = (" ".join(messages[idx+2].split())).split()
        read_metrics = (" ".join(messages[idx+3].split())).split()

        return (write_metrics, read_metrics)

    @staticmethod
    def log_metrics(logger, message, metrics):
        """Log the ior metrics

           Args:
               logger (log): logger object handle
               message (str) : Message to print before logging metrics
               metric (lst) : IOR write and read metrics
        """
        logger.info("\n")
        logger.info(message)
        for m in metrics:
            logger.info(m)
        logger.info("\n")


class IorMetrics(IntEnum):
    """Index Name and Number of each column in IOR result summary.
    """

    # Operation   Max(MiB)   Min(MiB)  Mean(MiB)     StdDev   Max(OPs)
    # Min(OPs)  Mean(OPs) StdDev    Mean(s) Stonewall(s) Stonewall(MiB)
    # Test# #Tasks tPN reps fPP reord reordoff reordrand seed segcnt
    # blksiz    xsize aggs(MiB)   API RefNum
    Operation = 0
    Max_MiB = 1
    Min_MiB = 2
    Mean_MiB = 3
    StdDev = 4
    Max_OPs = 5
    Min_OPs = 6
    Mean_OPs = 7
    StdDev = 8
    Mean_seconds = 9
    Stonewall_seconds = 10
    Stonewall_MiB = 11
    Test_No = 12
    Num_Tasks = 13
    tPN = 14
    reps = 15
    fPP = 16
    reord = 17
    reordoff = 18
    reordrand = 19
    seed = 20
    segcnt = 21
    blksiz = 22
    xsize = 23
    aggs_MiB = 24
    API = 25
    RefNum = 26
