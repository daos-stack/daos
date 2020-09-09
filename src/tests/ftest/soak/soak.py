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
provided in Contract No. 8F-30005.
Any reproduction of computer software, computer software documentation, or
portions thereof marked with this legend must also reproduce the markings.
"""

import os
import time
from apricot import TestWithServers
from ior_utils import IorCommand
from fio_utils import FioCommand
from dfuse_utils import Dfuse
from job_manager_utils import Srun
from general_utils import get_random_string, \
    run_command, DaosTestError, get_log_file
import slurm_utils
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from ClusterShell.NodeSet import NodeSet
from getpass import getuser
import socket
import threading
from avocado.core.exceptions import TestFail
from pydaos.raw import DaosSnapshot, DaosApiError
from agent_utils import include_local_host

H_LOCK = threading.Lock()


def DDHHMMSS_format(seconds):
    """Convert seconds into  #days:HH:MM:SS format.

    Args:
        seconds(int):  number of seconds to convert

    Returns:  str in the format of DD:HH:MM:SS

    """
    seconds = int(seconds)
    if seconds < 86400:
        return time.strftime("%H:%M:%S", time.gmtime(seconds))
    num_days = seconds/86400
    return "{} {} {}".format(
        num_days, 'Day' if num_days == 1 else 'Days', time.strftime(
            "%H:%M:%S", time.gmtime(seconds % 86400)))


class SoakTestError(Exception):
    """Soak exception class."""


class SoakTestBase(TestWithServers):
    # pylint: disable=too-many-public-methods
    """Execute DAOS Soak test cases.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a SoakBase object."""
        super(SoakTestBase, self).__init__(*args, **kwargs)
        self.failed_job_id_list = None
        self.test_log_dir = None
        self.exclude_slurm_nodes = None
        self.loop = None
        self.log_dir = None
        self.outputsoakdir = None
        self.test_name = None
        self.test_timeout = None
        self.end_time = None
        self.job_timeout = None
        self.nodesperjob = None
        self.task_list = None
        self.soak_results = None
        self.srun_params = None
        self.test_iteration = None
        self.h_list = None
        self.harasser_joblist = None
        self.harasser_results = None
        self.harasser_timeout = None
        self.all_failed_jobs = None
        self.username = None
        self.used = None
        self.dfuse = []

    def setUp(self):
        """Define test setup to be done."""
        self.log.info("<<setUp Started>> at %s", time.ctime())
        super(SoakTestBase, self).setUp()
        self.username = getuser()
        # Initialize loop param for all tests
        self.loop = 1
        self.exclude_slurm_nodes = []
        # Setup logging directories for soak logfiles
        # self.output dir is an avocado directory .../data/
        self.log_dir = get_log_file("soak")
        self.outputsoakdir = self.outputdir + "/soak"
        # Create the remote log directories on all client nodes
        self.test_log_dir = self.log_dir + "/pass" + str(self.loop)
        self.local_pass_dir = self.outputsoakdir + "/pass" + str(self.loop)
        self.sharedlog_dir = self.tmp + "/soak"
        self.sharedsoakdir = self.sharedlog_dir + "/pass" + str(self.loop)
        # Fail if slurm partition is not defined
        if not self.client_partition:
            raise SoakTestError(
                "<<FAILED: Partition is not correctly setup for daos "
                "slurm partition>>")
        # Check if the server nodes are in the client list;
        # this will happen when only one partition is specified
        for host_server in self.hostlist_servers:
            if host_server in self.hostlist_clients:
                self.hostlist_clients.remove(host_server)
                self.exclude_slurm_nodes.append(host_server)
        # Include test node for log cleanup; remove from client list
        local_host_list = include_local_host(None)
        self.exclude_slurm_nodes.extend(local_host_list)
        if local_host_list[0] in self.hostlist_clients:
            self.hostlist_clients.remove((local_host_list[0]))
        # Check if requested reservation is allowed in partition
        # NOTE: Slurm reservation and partition are created before soak runs.
        # CI uses partition=daos_client and no reservation.
        # A21 uses partition=normal/default and reservation=daos-test.
        # Partition and reservation names are updated in the yaml file.
        # It is assumed that if there is no reservation (CI only), then all
        # the nodes in the partition will be used for soak.
        self.srun_params = {"partition": self.client_partition}
        slurm_reservation = self.params.get(
            "reservation", "/run/srun_params/*")
        if slurm_reservation is not None:
            # verify that the reservation is valid
            reserved_nodes = slurm_utils.get_reserved_nodes(
                slurm_reservation, self.client_partition)
            if not reserved_nodes:
                # client nodes are invalid for requested reservation
                self.hostlist_clients = []
                raise SoakTestError(
                    "<<FAILED: Reservation {} is invalid "
                    "in partition {}>>".format(
                        slurm_reservation, self.client_partition))
            # update srun params
            self.srun_params["reservation"] = slurm_reservation
        self.log.info(
            "<<Updated hostlist_clients %s >>", self.hostlist_clients)
        if not self.hostlist_clients:
            self.fail(
                "There are no valid nodes in this partition to run "
                "soak. Check partition {} for valid nodes".format(
                    self.client_partition))

    def pre_tear_down(self):
        """Tear down any test-specific steps prior to running tearDown().

        Returns:
            list: a list of error strings to report after all tear down
            steps have been attempted

        """
        errors = []
        # clear out any jobs in squeue;
        if self.failed_job_id_list:
            job_id = " ".join([str(job) for job in self.failed_job_id_list])
            self.log.info("<<Cancel jobs in queue with ids %s >>", job_id)
            try:
                run_command(
                    "scancel --partition {} -u {} {}".format(
                        self.client_partition, self.username, job_id))
            except DaosTestError as error:
                # Exception was raised due to a non-zero exit status
                errors.append("Failed to cancel jobs {}: {}".format(
                    self.failed_job_id_list, error))
        if self.all_failed_jobs:
            errors.append("SOAK FAILED: The following jobs failed {} ".format(
                " ,".join(str(j_id) for j_id in self.all_failed_jobs)))
        # Check if any dfuse mount points need to be cleaned
        try:
            self.cleanup_dfuse()
        except SoakTestError as error:
            self.log.info("Dfuse cleanup failed with %s", error)

        # daos_agent is always started on this node when start agent is false
        if not self.setup_start_agents:
            self.hostlist_clients = [socket.gethostname().split('.', 1)[0]]
        return errors

    def tearDown(self):
        """Define tearDown and clear any left over jobs in squeue."""
        # Perform any test-specific tear down steps and collect any
        # reported errors
        self.log.info("<<tearDown Started>> at %s", time.ctime())
        super(SoakTestBase, self).tearDown()

    def job_done(self, args):
        """Call this function when a job is done.

        Args:
            args (list):handle --which job, i.e. the job ID,
                        state  --string indicating job completion status
        """
        self.soak_results[args["handle"]] = args["state"]

    def add_pools(self, pool_names):
        """Create a list of pools that the various tests use for storage.

        Args:
            pool_names: list of pool namespaces from yaml file
                        /run/<test_params>/poollist/*
        """
        for pool_name in pool_names:
            path = "".join(["/run/", pool_name, "/*"])
            # Create a pool and add it to the overall list of pools
            self.pool.append(TestPool(
                self.context, self.log, dmg_command=self.get_dmg_command()))
            self.pool[-1].namespace = path
            self.pool[-1].get_params(self)
            self.pool[-1].create()
            self.log.info("Valid Pool UUID is %s", self.pool[-1].uuid)

    def get_remote_logs(self):
        """Copy files from remote dir to local dir.

        Raises:
            SoakTestError: if there is an error with the remote copy

        """
        # copy the files from the client nodes to a shared directory
        command = "/usr/bin/rsync -avtr --min-size=1B {0} {1}/..".format(
            self.test_log_dir, self.sharedsoakdir)
        result = slurm_utils.srun(
            NodeSet.fromlist(self.hostlist_clients), command, self.srun_params)
        if result.exit_status == 0:
            # copy the local logs and the logs in the shared dir to avocado dir
            for directory in [self.test_log_dir, self.sharedsoakdir]:
                command = "/usr/bin/cp -R -p {0}/ \'{1}\'".format(
                    directory, self.outputsoakdir)
                try:
                    result = run_command(command, timeout=30)
                except DaosTestError as error:
                    raise SoakTestError(
                        "<<FAILED: job logs failed to copy {}>>".format(
                            error))
            # remove the remote soak logs for this pass
            command = "/usr/bin/rm -rf {0}".format(self.test_log_dir)
            slurm_utils.srun(
                NodeSet.fromlist(self.hostlist_clients), command,
                self.srun_params)
            # remove the local log for this pass
            for directory in [self.test_log_dir, self.sharedsoakdir]:
                command = "/usr/bin/rm -rf {0}".format(directory)
                try:
                    result = run_command(command)
                except DaosTestError as error:
                    raise SoakTestError(
                        "<<FAILED: job logs failed to delete {}>>".format(
                            error))
        else:
            raise SoakTestError(
                "<<FAILED: Soak remote logfiles not copied "
                "from clients>>: {}".format(self.hostlist_clients))

    def is_harasser(self, harasser):
        """Check if harasser is defined in yaml.

        Args:
            harasser (list): list of harassers to launch

        Returns: bool

        """
        return self.h_list and harasser in self.h_list

    def launch_harassers(self, harassers, pools):
        """Launch any harasser tests if defined in yaml.

        Args:
            harasser (list): list of harassers to launch
            pools (TestPool): pool obj

        """
        job = None
        # Launch harasser after one complete pass
        for harasser in harassers:
            if harasser == "rebuild":
                method = self.launch_rebuild
                ranks = self.params.get(
                    "ranks_to_kill", "/run/" + harasser + "/*")
                param_list = (ranks, pools)
                name = "REBUILD"
            if harasser in "snapshot":
                method = self.launch_snapshot
                param_list = ()
                name = "SNAPSHOT"
            else:
                raise SoakTestError(
                    "<<FAILED: Harasser {} is not supported. ".format(
                        harasser))
            job = threading.Thread(
                target=method, args=param_list, name=name)
            self.harasser_joblist.append(job)

        # start all harassers
        for job in self.harasser_joblist:
            job.start()

    def harasser_completion(self, timeout):
        """Complete harasser jobs.

        Args:
            timeout (int): timeout in secs

        Returns:
            bool: status

        """
        status = True
        for job in self.harasser_joblist:
            job.join(timeout)
        for job in self.harasser_joblist:
            if job.is_alive():
                self.log.error(
                    "<< HARASSER is alive %s FAILED to join>> ", job.name)
                status &= False
        # Check if the completed job passed
        for harasser, status in self.harasser_results.items():
            if not status:
                self.log.error(
                    "<< HARASSER %s FAILED>> ", harasser)
                status &= False
        self.harasser_joblist = []
        return status

    def launch_rebuild(self, ranks, pools):
        """Launch the rebuild process.

        Args:
            ranks (list): Server ranks to kill
            pools (list): list of TestPool obj

        """
        self.log.info("<<Launch Rebuild>> at %s", time.ctime())
        status = True
        for pool in pools:
            # Kill the server
            try:
                pool.start_rebuild(ranks, self.d_log)
            except (RuntimeError, TestFail, DaosApiError) as error:
                self.log.error("Rebuild failed to start", exc_info=error)
                status &= False
                break
            # Wait for rebuild to start
            try:
                pool.wait_for_rebuild(True)
            except (RuntimeError, TestFail, DaosApiError) as error:
                self.log.error(
                    "Rebuild failed waiting to start", exc_info=error)
                status &= False
                break
            # Wait for rebuild to complete
            try:
                pool.wait_for_rebuild(False)
            except (RuntimeError, TestFail, DaosApiError) as error:
                self.log.error(
                    "Rebuild failed waiting to finish", exc_info=error)
                status &= False
                break
        with H_LOCK:
            self.harasser_results["REBUILD"] = status

    def launch_snapshot(self):
        """Create a basic snapshot of the reserved pool."""
        self.log.info("<<Launch Snapshot>> at %s", time.ctime())
        status = True
        # Create container
        container = TestContainer(self.pool[0])
        container.namespace = "/run/container_reserved/*"
        container.get_params(self)
        container.create()
        container.open()
        obj_cls = self.params.get(
            "object_class", '/run/container_reserved/*')

        # write data to object
        data_pattern = get_random_string(500)
        datasize = len(data_pattern) + 1
        dkey = "dkey"
        akey = "akey"
        obj = container.container.write_an_obj(
            data_pattern, datasize, dkey, akey, obj_cls=obj_cls)
        obj.close()
        # Take a snapshot of the container
        snapshot = DaosSnapshot(self.context)
        try:
            snapshot.create(container.container.coh)
        except (RuntimeError, TestFail, DaosApiError) as error:
            self.log.error("Snapshot failed", exc_info=error)
            status &= False
        if status:
            self.log.info("Snapshot Created")
            # write more data to object
            data_pattern2 = get_random_string(500)
            datasize2 = len(data_pattern2) + 1
            dkey = "dkey"
            akey = "akey"
            obj2 = container.container.write_an_obj(
                data_pattern2, datasize2, dkey, akey, obj_cls=obj_cls)
            obj2.close()
            self.log.info("Wrote additional data to container")
            # open the snapshot and read the data
            obj.open()
            snap_handle = snapshot.open(container.container.coh)
            try:
                data_pattern3 = container.container.read_an_obj(
                    datasize, dkey, akey, obj, txn=snap_handle.value)
            except (RuntimeError, TestFail, DaosApiError) as error:
                self.log.error(
                    "Error when retrieving the snapshot data %s", error)
                status &= False
            if status:
                # Compare the snapshot to the original written data.
                if data_pattern3.value != data_pattern:
                    self.log.error("Snapshot data miscompere")
                    status &= False
        # Destroy the snapshot
        try:
            snapshot.destroy(container.container.coh)
        except (RuntimeError, TestFail, DaosApiError) as error:
            self.log.error("Failed to destroy snapshot %s", error)
            status &= False
        # cleanup
        container.close()
        container.destroy()
        with H_LOCK:
            self.harasser_results["SNAPSHOT"] = status

    def create_ior_cmdline(self, job_spec, pool, ppn, nodesperjob):
        """Create an IOR cmdline to run in slurm batch.

        Args:

            job_spec (str):   ior job in yaml to run
            pool (obj):       TestPool obj
            ppn(int):         number of tasks to run on each node
            nodesperjob(int): number of nodes per job

        Returns:
            cmd: cmdline string

        """
        commands = []
        iteration = self.test_iteration
        ior_params = "/run/" + job_spec + "/*"
        # IOR job specs with a list of parameters; update each value
        api_list = self.params.get("api", ior_params + "*")
        tsize_list = self.params.get("transfer_size", ior_params + "*")
        bsize_list = self.params.get("block_size", ior_params + "*")
        oclass_list = self.params.get("dfs_oclass", ior_params + "*")
        # check if capable of doing rebuild; if yes then dfs_oclass = RP_*GX
        if self.is_harasser("rebuild"):
            oclass_list = self.params.get("dfs_oclass", "/run/rebuild/*")
        # update IOR cmdline for each additional IOR obj
        for api in api_list:
            for b_size in bsize_list:
                for t_size in tsize_list:
                    for o_type in oclass_list:
                        ior_cmd = IorCommand()
                        ior_cmd.namespace = ior_params
                        ior_cmd.get_params(self)
                        if iteration is not None and iteration < 0:
                            ior_cmd.repetitions.update(1000000)
                        if self.job_timeout is not None:
                            ior_cmd.max_duration.update(self.job_timeout)
                        else:
                            ior_cmd.max_duration.update(10)
                        ior_cmd.api.update(api)
                        ior_cmd.block_size.update(b_size)
                        ior_cmd.transfer_size.update(t_size)
                        ior_cmd.dfs_oclass.update(o_type)
                        if ior_cmd.api.value == "DFS":
                            ior_cmd.test_file.update(
                                os.path.join("/", "testfile"))
                        ior_cmd.set_daos_params(self.server_group, pool)
                        # srun cmdline
                        nprocs = nodesperjob * ppn
                        env = ior_cmd.get_default_env("srun")
                        cmd = Srun(ior_cmd)
                        cmd.assign_processes(nprocs)
                        cmd.assign_environment(env, True)
                        cmd.ntasks_per_node.update(ppn)
                        cmd.nodes.update(nodesperjob)
                        log_name = "{}_{}_{}_{}".format(
                            api, b_size, t_size, o_type)
                        commands.append([cmd.__str__(), log_name])
                        self.log.info(
                            "<<IOR cmdline>>:\n %s", cmd.__str__())
        return commands

    def start_dfuse(self, pool):
        """Create dfuse start command line for slurm.

        Args:
            pool (obj):             TestPool obj

        Returns dfuse(obj):         Dfuse obj
                cmd(list):          list of dfuse commands to add to jobscript
        """
        # Get Dfuse params
        dfuse = Dfuse(self.hostlist_clients, self.tmp)
        dfuse.get_params(self)
        # update dfuse params; mountpoint for each container
        unique = get_random_string(5, self.used)
        self.used.append(unique)
        mount_dir = dfuse.mount_dir.value + unique
        dfuse.mount_dir.update(mount_dir)
        dfuse.set_dfuse_params(pool)
        dfuse.set_dfuse_cont_param(self.get_container(pool))

        dfuse_start_cmds = [
            "mkdir -p {}".format(dfuse.mount_dir.value),
            "{}".format(dfuse.__str__()),
            "df -h {}".format(dfuse.mount_dir.value)
        ]
        return dfuse, dfuse_start_cmds

    def stop_dfuse(self, dfuse):
        """Create dfuse stop command line for slurm.

        Args:
            dfuse (obj): Dfuse obj

        Returns cmd(list):    list of cmds to pass to slurm script
        """
        self.log.info("\n")
        dfuse_stop_cmds = [
            "fusermount3 -uz {0}".format(dfuse.mount_dir.value),
            "rm -rf {0}".format(dfuse.mount_dir.value)
        ]
        return dfuse_stop_cmds

    def cleanup_dfuse(self):
        """Cleanup and remove any dfuse mount points."""
        cmd = [
            "/usr/bin/bash -c 'pkill dfuse",
            "for dir in /tmp/daos_dfuse*",
            "do fusermount3 -uz $dir",
            "rm -rf $dir",
            "done'"]
        try:
            slurm_utils.srun(
                NodeSet.fromlist(
                    self.hostlist_clients), "{}".format(
                        ";".join(cmd)), self.srun_params)
        except slurm_utils.SlurmFailed as error:
            self.log.info(
                "<<FAILED: Dfuse directories not deleted %s >>", error)

    def create_fio_cmdline(self, job_spec, pool):
        """Create the FOI commandline for job script.

        Args:

            job_spec (str): fio job in yaml to run
            pool (obj):   TestPool obj
            ppn(int): number of tasks to run on each node

        Returns:
            cmd(list): list of cmdlines

        """
        commands = []
        fio_namespace = "/run/{}".format(job_spec)
        # test params
        bs_list = self.params.get("blocksize", fio_namespace + "/soak/*")
        size_list = self.params.get("size", fio_namespace + "/soak/*")
        rw_list = self.params.get("rw", fio_namespace + "/soak/*")
        # Get the parameters for Fio
        fio_cmd = FioCommand()
        fio_cmd.namespace = "{}/*".format(fio_namespace)
        fio_cmd.get_params(self)
        for blocksize in bs_list:
            for size in size_list:
                for rw in rw_list:
                    # update fio params
                    fio_cmd.update(
                        "global", "blocksize", blocksize,
                        "fio --name=global --blocksize")
                    fio_cmd.update(
                        "global", "size", size,
                        "fio --name=global --size")
                    fio_cmd.update(
                        "global", "rw", rw,
                        "fio --name=global --rw")
                    srun_cmds = []
                    # add srun start dfuse cmds if api is POSIX
                    if fio_cmd.api.value == "POSIX":
                        # Connect to the pool, create container
                        # and then start dfuse
                        dfuse, srun_cmds = self.start_dfuse(pool)
                    # Update the FIO cmdline
                    fio_cmd.update(
                        "global", "directory",
                        dfuse.mount_dir.value,
                        "fio --name=global --directory")
                    # add fio cmline
                    srun_cmds.append(str(fio_cmd.__str__()))
                    srun_cmds.append("status=$?")
                    # If posix, add the srun dfuse stop cmds
                    if fio_cmd.api.value == "POSIX":
                        srun_cmds.extend(self.stop_dfuse(dfuse))
                    # exit code
                    srun_cmds.append("exit $status")
                    log_name = "{}_{}_{}".format(blocksize, size, rw)
                    commands.append([srun_cmds, log_name])
                    self.log.info("<<Fio cmdlines>>:")
                    for cmd in srun_cmds:
                        self.log.info("%s", cmd)
        return commands

    def build_job_script(self, commands, job, ppn, nodesperjob):
        """Create a slurm batch script that will execute a list of cmdlines.

        Args:
            commands(list): commandlines and cmd specific log_name
            job(str): the job name that will be defined in the slurm script
            ppn(int): number of tasks to run on each node

        Returns:
            script_list: list of slurm batch scripts

        """
        self.log.info("<<Build Script>> at %s", time.ctime())
        script_list = []
        # if additional cmds are needed in the batch script
        additional_cmds = []
        # Create the sbatch script for each list of cmdlines
        for cmd, log_name in commands:
            if isinstance(cmd, str):
                cmd = [cmd]
            output = os.path.join(
                self.test_log_dir, self.test_name + "_" + job + "_" +
                log_name + "_" + str(ppn*nodesperjob) + "_%N_" + "%j_")
            error = os.path.join(
                self.test_log_dir, self.test_name + "_" + job + "_" +
                log_name + "_" +
                str(ppn*nodesperjob) + "_%N_" + "%j_" + "ERROR_")
            sbatch = {
                "time": str(self.job_timeout) + ":00",
                "exclude": NodeSet.fromlist(self.exclude_slurm_nodes),
                "error": str(error),
                "export": "ALL"
                }
            # include the cluster specific params
            sbatch.update(self.srun_params)
            unique = get_random_string(5, self.used)
            script = slurm_utils.write_slurm_script(
                self.test_log_dir, job, output, nodesperjob,
                additional_cmds + cmd, unique, sbatch)
            script_list.append(script)
            self.used.append(unique)
        return script_list

    def job_setup(self, job, pool):
        """Create the cmdline needed to launch job.

        Args:
            job(str): single job from test params list of jobs to run
            pool (obj): TestPool obj

        Returns:
            job_cmdlist: list cmdline that can be launched
                         by specified job manager

        """
        job_cmdlist = []
        commands = []
        scripts = []
        nodesperjob = []
        self.log.info("<<Job_Setup %s >> at %s", self.test_name, time.ctime())
        for npj in self.nodesperjob:
            # nodesperjob = -1 indicates to use all nodes in client hostlist
            if npj < 0:
                npj = len(self.hostlist_clients)
            if len(self.hostlist_clients)/npj < 1:
                raise SoakTestError(
                    "<<FAILED: There are only {} client nodes for this job. "
                    "Job requires {}".format(
                        len(self.hostlist_clients), npj))
            nodesperjob.append(npj)
        if "ior" in job:
            for npj in nodesperjob:
                for ppn in self.task_list:
                    commands = self.create_ior_cmdline(
                        job, pool, ppn, npj)
                    # scripts are single cmdline
                    scripts = self.build_job_script(
                        commands, job, ppn, npj)
                    job_cmdlist.extend(scripts)
        elif "fio" in job:
            commands = self.create_fio_cmdline(job, pool)
            # scripts are single cmdline
            scripts = self.build_job_script(commands, job, 1, 1)
            job_cmdlist.extend(scripts)
        else:
            raise SoakTestError(
                "<<FAILED: Job {} is not supported. ".format(
                    self.job))
        return job_cmdlist

    def job_startup(self, job_cmdlist):
        """Submit job batch script.

        Args:
            job_cmdlist (list): list of jobs to execute
        Returns:
            job_id_list: IDs of each job submitted to slurm.

        """
        self.log.info(
            "<<Job Startup - %s >> at %s", self.test_name, time.ctime())
        job_id_list = []
        # before submitting the jobs to the queue, check the job timeout;
        if time.time() > self.end_time:
            self.log.info("<< SOAK test timeout in Job Startup>>")
            return job_id_list
        # job_cmdlist is a list of batch script files

        for script in job_cmdlist:
            try:
                job_id = slurm_utils.run_slurm_script(str(script))
            except slurm_utils.SlurmFailed as error:
                self.log.error(error)
                # Force the test to exit with failure
                job_id = None
            if job_id:
                self.log.info(
                    "<<Job %s started with %s >> at %s",
                    job_id, script, time.ctime())
                slurm_utils.register_for_job_results(
                    job_id, self, maxwait=self.test_timeout)
                # keep a list of the job_id's
                job_id_list.append(int(job_id))
            else:
                # one of the jobs failed to queue; exit on first fail for now.
                err_msg = "Slurm failed to submit job for {}".format(script)
                job_id_list = []
                raise SoakTestError(
                    "<<FAILED:  Soak {}: {}>>".format(self.test_name, err_msg))
        return job_id_list

    def job_completion(self, job_id_list):
        """Wait for job completion and cleanup.

        Args:
            job_id_list: IDs of each job submitted to slurm
        Returns:
            failed_job_id_list: IDs of each job that failed in slurm

        """
        self.log.info(
            "<<Job Completion - %s >> at %s", self.test_name, time.ctime())
        # If there is nothing to do; exit
        if job_id_list:
            # wait for all the jobs to finish
            while len(self.soak_results) < len(job_id_list):
                # wait for the jobs to complete.
                # enter tearDown before hitting the avocado timeout
                if time.time() > self.end_time:
                    self.log.info(
                        "<< SOAK test timeout in Job Completion at %s >>",
                        time.ctime())
                    for job in job_id_list:
                        _ = slurm_utils.cancel_jobs(int(job))
                time.sleep(5)
            # check for JobStatus = COMPLETED or CANCELLED (i.e. TEST TO)
            for job, result in self.soak_results.items():
                if result in ["COMPLETED", "CANCELLED"]:
                    job_id_list.remove(int(job))
                else:
                    self.log.info(
                        "<< Job %s failed with status %s>>", job, result)
            # gather all the logfiles for this pass and cleanup test nodes
            try:
                self.get_remote_logs()
            except SoakTestError as error:
                self.log.info("Remote copy failed with %s", error)
            self.soak_results = {}
        return job_id_list

    def execute_jobs(self, jobs, pools):
        """Execute the overall soak test.

        Args:
            pools (list): list of TestPool obj - self.pool[1:]

        Raise:
            SoakTestError

        """
        cmdlist = []
        # unique numbers per pass
        self.used = []
        # Update the remote log directories from new loop/pass
        self.sharedsoakdir = self.sharedlog_dir + "/pass" + str(self.loop)
        self.test_log_dir = self.log_dir + "/pass" + str(self.loop)
        local_pass_dir = self.outputsoakdir + "/pass" + str(self.loop)
        result = slurm_utils.srun(
            NodeSet.fromlist(self.hostlist_clients), "mkdir -p {}".format(
                self.test_log_dir), self.srun_params)
        if result.exit_status > 0:
            raise SoakTestError(
                "<<FAILED: logfile directory not"
                "created on clients>>: {}".format(self.hostlist_clients))
        # Create local log directory
        os.makedirs(local_pass_dir)
        os.makedirs(self.sharedsoakdir)
        # Setup cmdlines for job with specified pool
        if len(pools) < len(jobs):
            raise SoakTestError(
                "<<FAILED: There are not enough pools to run this test>>")
        for index, job in enumerate(jobs):
            cmdlist.extend(self.job_setup(job, pools[index]))
        # Gather the job_ids
        job_id_list = self.job_startup(cmdlist)
        # Initialize the failed_job_list to job_list so that any
        # unexpected failures will clear the squeue in tearDown
        self.failed_job_id_list = job_id_list
        # launch harassers if defined and enabled
        if self.h_list and self.loop > 1:
            self.log.info("<<Harassers are enabled>>")
            self.launch_harassers(self.h_list, pools)
            if not self.harasser_completion(self.harasser_timeout):
                raise SoakTestError("<<FAILED: Harassers failed ")
            # rebuild can only run once for now
            if self.is_harasser("rebuild"):
                self.h_list.remove("rebuild")
        # Wait for jobs to finish and cancel/kill jobs if necessary
        self.failed_job_id_list = self.job_completion(job_id_list)
        # Log the failing job ID
        if self.failed_job_id_list:
            self.log.info(
                "<<FAILED: The following jobs failed %s >>", (" ,".join(
                    str(j_id) for j_id in self.failed_job_id_list)))
            # accumulate failing job IDs
            self.all_failed_jobs.extend(self.failed_job_id_list)
            # clear out the failed jobs for this pass
            self.failed_job_id_list = []

    def run_soak(self, test_param):
        """Run the soak test specified by the test params.

        Args:
            test_param (str): test_params from yaml file

        """
        self.soak_results = {}
        self.pool = []
        self.harasser_joblist = []
        self.harasser_results = {}
        test_to = self.params.get("test_timeout", test_param)
        self.job_timeout = self.params.get("job_timeout", test_param)
        self.harasser_timeout = self.params.get("harasser_timeout", test_param)
        self.test_name = self.params.get("name", test_param)
        self.nodesperjob = self.params.get("nodesperjob", test_param)
        self.test_iteration = self.params.get("iteration", test_param)
        self.task_list = self.params.get("taskspernode", test_param + "*")
        self.h_list = self.params.get("harasserlist", test_param + "*")
        job_list = self.params.get("joblist", test_param + "*")
        pool_list = self.params.get("poollist", test_param + "*")
        rank = self.params.get("rank", "/run/container_reserved/*")
        if self.is_harasser("rebuild"):
            obj_class = "_".join(["OC", str(
                self.params.get("dfs_oclass", "/run/rebuild/*")[0])])
        else:
            obj_class = "OC_SX"
        # Create the reserved pool with data
        # self.pool is a list of all the pools used in soak
        # self.pool[0] will always be the reserved pool
        self.add_pools(["pool_reserved"])
        self.pool[0].connect()

        # Create the container and populate with a known data
        # TO-DO: use IOR to write and later read verify the data
        self.container = self.get_container(
            self.pool[0], "/run/container_reserved/*", True)
        self.container.write_objects(rank, obj_class)
        self.all_failed_jobs = []
        # cleanup soak log directories before test on all nodes
        result = slurm_utils.srun(
            NodeSet.fromlist(self.hostlist_clients), "rm -rf {}".format(
                self.log_dir), self.srun_params)
        if result.exit_status > 0:
            raise SoakTestError(
                "<<FAILED: Soak directories not removed"
                "from clients>>: {}".format(self.hostlist_clients))
        # cleanup test_node
        for log_dir in [self.log_dir, self.sharedlog_dir]:
            cmd = "rm -rf {}".format(log_dir)
            try:
                result = run_command(cmd, timeout=30)
            except DaosTestError as error:
                raise SoakTestError(
                    "<<FAILED: Soak directory {} was not removed {}>>".format(
                        log_dir, error))
        # Initialize time
        start_time = time.time()
        self.test_timeout = int(3600 * test_to)
        self.end_time = start_time + self.test_timeout
        self.log.info("<<START %s >> at %s", self.test_name, time.ctime())
        while time.time() < self.end_time:
            # Start new pass
            start_loop_time = time.time()
            self.log.info(
                "<<Soak1 PASS %s: time until done %s>>", self.loop,
                DDHHMMSS_format(self.end_time - time.time()))
            # Create all specified pools
            self.add_pools(pool_list)
            self.log.info(
                "Current pools: %s",
                " ".join([pool.uuid for pool in self.pool]))
            try:
                self.execute_jobs(job_list, self.pool[1:])
            except SoakTestError as error:
                self.fail(error)
            errors = self.destroy_pools(self.pool[1:])
            # remove the test pools from self.pool; preserving reserved pool
            self.pool = [self.pool[0]]
            self.log.info(
                "Current pools: %s",
                " ".join([pool.uuid for pool in self.pool]))
            self.assertEqual(len(errors), 0, "\n".join(errors))
            # Break out of loop if smoke
            if "smoke" in self.test_name:
                break
            loop_time = time.time() - start_loop_time
            self.log.info(
                "<<PASS %s completed in %s >>", self.loop, DDHHMMSS_format(
                    loop_time))
            self.loop += 1
        # TO-DO: use IOR
        self.assertTrue(
            self.container.read_objects(),
            "Data verification error on reserved pool"
            "after SOAK completed")
        # gather the daos logs from the client nodes
        self.log.info(
            "<<<<SOAK TOTAL TEST TIME = %s>>>", DDHHMMSS_format(
                time.time() - start_time))
