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

import os
import time
from ior_utils import IorCommand
from fio_utils import FioCommand
from dfuse_utils import Dfuse
from job_manager_utils import Srun
from command_utils_base import BasicParameter
from general_utils import get_random_string, run_command, DaosTestError
import slurm_utils
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from ClusterShell.NodeSet import NodeSet
import threading
from avocado.core.exceptions import TestFail
from pydaos.raw import DaosSnapshot, DaosApiError


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
    num_days = seconds / 86400
    return "{} {} {}".format(
        num_days, 'Day' if num_days == 1 else 'Days', time.strftime(
            "%H:%M:%S", time.gmtime(seconds % 86400)))


def add_pools(self, pool_names):
    """Create a list of pools that the various tests use for storage.

    Args:
        pool_names: list of pool namespaces from yaml file
                    /run/<test_params>/poollist/*
    """
    for pool_name in pool_names:
        path = "".join(["/run/", pool_name, "/*"])
        # Create a pool and add it to the overall list of pools
        self.pool.append(
            TestPool(self.context, self.get_dmg_command()))
        self.pool[-1].namespace = path
        self.pool[-1].get_params(self)
        self.pool[-1].create()
        self.pool[-1].set_property("reclaim", "time")
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
            method = launch_rebuild
            ranks = self.params.get(
                "ranks_to_kill", "/run/" + harasser + "/*")
            param_list = (ranks, pools)
            name = "REBUILD"
        if harasser in "snapshot":
            method = launch_snapshot
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


def get_srun_cmd(cmd, nodesperjob=1, ppn=1, srun_params=None, env=None):
    """Wrap cmdline in a srun cmdline.

    Args:
        cmd (str): cmdline to wrap in srun cmdline
        ppn (int): processes per node
        nodesperjob(int): number of nodes
        srun_params(dict): additional srun_params
        env (dict): env variables to pass on cmdline

    Returns:
        cmdlines: cmdline string

    """
    srun_cmd = Srun(cmd)
    srun_cmd.nodes.update(nodesperjob)
    srun_cmd.ntasks_per_node.update(ppn)
    if srun_params:
        for key, value in srun_params.items():
            key_obj = getattr(srun_cmd, key)
            if isinstance(key_obj, BasicParameter):
                key_obj.update(value, key)
            else:
                raise SoakTestError(
                    "<<FAILED: The srun param {} does not exist".format(key))
    if env:
        srun_cmd.assign_environment(env)
    return str(srun_cmd)


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
    mpi_module = self.params.get(
        "mpi_module", "/run/", default="mpi/mpich-x86_64")
    # IOR job specs with a list of parameters; update each value
    api_list = self.params.get("api", ior_params + "*")
    tsize_list = self.params.get("transfer_size", ior_params + "*")
    bsize_list = self.params.get("block_size", ior_params + "*")
    oclass_list = self.params.get("dfs_oclass", ior_params + "*")
    plugin_path = self.params.get("plugin_path", "/run/hdf5_vol/")
    # check if capable of doing rebuild; if yes then dfs_oclass = RP_*GX
    if is_harasser(self, "rebuild"):
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
                    if api == "HDF5-VOL":
                        ior_cmd.api.update("HDF5")
                    else:
                        ior_cmd.api.update(api)
                    ior_cmd.block_size.update(b_size)
                    ior_cmd.transfer_size.update(t_size)
                    ior_cmd.dfs_oclass.update(o_type)
                    if ior_cmd.api.value == "DFS":
                        ior_cmd.test_file.update(
                            os.path.join("/", "testfile"))
                    ior_cmd.set_daos_params(self.server_group, pool)
                    env = ior_cmd.get_default_env("srun")
                    sbatch_cmds = ["module load -q {}".format(mpi_module)]
                    # include dfuse cmdlines
                    if api in ["HDF5-VOL", "POSIX"]:
                        dfuse, dfuse_start_cmdlist = start_dfuse(
                            self, pool, nodesperjob, "SLURM")
                        sbatch_cmds.extend(dfuse_start_cmdlist)
                        ior_cmd.test_file.update(
                            os.path.join(dfuse.mount_dir.value, "testfile"))
                    # add envs if api is HDF5-VOL
                    if api == "HDF5-VOL":
                        env["HDF5_VOL_CONNECTOR"] = "daos"
                        env["HDF5_PLUGIN_PATH"] = "{}".format(plugin_path)
                        # env["H5_DAOS_BYPASS_DUNS"] = 1
                    srun_cmd = Srun(ior_cmd)
                    srun_cmd.assign_processes(nodesperjob * ppn)
                    srun_cmd.assign_environment(env, True)
                    srun_cmd.ntasks_per_node.update(ppn)
                    srun_cmd.nodes.update(nodesperjob)
                    sbatch_cmds.append(str(srun_cmd))
                    sbatch_cmds.append("status=$?")
                    if api in ["HDF5-VOL", "POSIX"]:
                        sbatch_cmds.extend(
                            stop_dfuse(dfuse, nodesperjob, "SLURM"))
                    sbatch_cmds.append("exit $status")
                    log_name = "{}_{}_{}_{}".format(
                        api, b_size, t_size, o_type)
                    commands.append([sbatch_cmds, log_name])
                    self.log.info(
                        "<<IOR {} cmdlines>>:".format(api))
                    for cmd in sbatch_cmds:
                        self.log.info("%s", cmd)
    return commands


def start_dfuse(self, pool, nodesperjob, resource_mgr=None):
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
        "clush -w $SLURM_JOB_NODELIST \"cd {};{}\"".format(
            dfuse.mount_dir.value, dfuse.__str__()),
        "sleep 10",
        "df -h {}".format(dfuse.mount_dir.value),
    ]
    if resource_mgr == "SLURM":
        cmds = []
        for cmd in dfuse_start_cmds:
            if cmd.startswith("clush") or cmd.startswith("sleep"):
                cmds.append(cmd)
            else:
                cmds.append(get_srun_cmd(cmd, nodesperjob))
        dfuse_start_cmds = cmds
    return dfuse, dfuse_start_cmds


def stop_dfuse(dfuse, nodesperjob, resource_mgr=None):
    """Create dfuse stop command line for slurm.

    Args:
        dfuse (obj): Dfuse obj

    Returns cmd(list):    list of cmds to pass to slurm script
    """
    dfuse_stop_cmds = [
        "fusermount3 -uz {0}".format(dfuse.mount_dir.value),
        "rm -rf {0}".format(dfuse.mount_dir.value)
    ]
    if resource_mgr == "SLURM":
        cmds = []
        for dfuse_stop_cmd in dfuse_stop_cmds:
            cmds.append(get_srun_cmd(dfuse_stop_cmd, nodesperjob))
        dfuse_stop_cmds = cmds
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
                    dfuse, srun_cmds = start_dfuse(self, pool, nodesperjob=1)
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
                    srun_cmds.extend(stop_dfuse(dfuse, nodesperjob=1))
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
            log_name + "_" + str(ppn * nodesperjob) + "_%N_" + "%j_")
        error = os.path.join(
            self.test_log_dir, self.test_name + "_" + job + "_" +
            log_name + "_" +
            str(ppn * nodesperjob) + "_%N_" + "%j_" + "ERROR_")
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


class SoakTestError(Exception):
    """Soak exception class."""
