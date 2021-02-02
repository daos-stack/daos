#!/usr/bin/python
"""
(C) Copyright 2019-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import time
import random
import threading
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
        self (obj): soak obj
        pool_names: list of pool namespaces from yaml file
                    /run/<test_params>/poollist/*
    """
    for pool_name in pool_names:
        path = "".join(["/run/", pool_name, "/*"])
        # Create a pool and add it to the overall list of pools
        self.pool.append(TestPool(self.context, self.dmg_command))
        self.pool[-1].namespace = path
        self.pool[-1].get_params(self)
        self.pool[-1].create()
        self.pool[-1].set_property("reclaim", "time")
        self.log.info("Valid Pool UUID is %s", self.pool[-1].uuid)


def add_containers(self, pool, oclass=None):
    """Create a list of containers that the various jobs use for storage.

    Args:
        pool: pool to create container
        oclass: object class of container

    """
    # Create a container and add it to the overall list of containers
    path = "".join(["/run/container/*"])
    self.container.append(
        TestContainer(pool, daos_command=self.get_daos_command()))
    self.container[-1].namespace = path
    self.container[-1].get_params(self)
    if oclass:
        self.container[-1].oclass.update(oclass)
    self.container[-1].create()


def get_remote_logs(self):
    """Copy files from remote dir to local dir.

    Args:
        self (obj): soak obj

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


def get_harassers(harassers):
    """Create a valid harasserlist from the yaml job harassers.

    Args:
        harassers (list): harasser jobs from yaml.

    Returns:
        harasserlist (list): Ordered list of harassers to execute
                             per pass of soak

    """
    harasserlist = []
    for harasser in harassers:
        harasserlist.extend(harasser.split("_"))
    return harasserlist


def wait_for_pool_rebuild(self, pool, name):
    """Launch the rebuild process with system.

    Args:

        self (obj): soak obj
        pools (obj): TestPool obj
        name (str): name of soak harasser

    """
    rebuild_status = False
    self.log.info(
        "<<Wait for %s rebuild on %s>> at %s", name, pool.uuid, time.ctime())

    try:
        pool.wait_for_rebuild(False)
        rebuild_status = True
    except DaosTestError as error:
        self.log.error(
            "<<<FAILED:{} rebuild timed out".format(name), exc_info=error)
        rebuild_status = False
    except TestFail as error1:
        self.log.error("<<<FAILED:{} rebuild failed due to test issue".format(
            name), exc_info=error1)

    return rebuild_status


def launch_snapshot(self, pool, name):
    """Create a basic snapshot of the reserved pool.

    Args:

        self (obj): soak obj
        pool (obj): TestPool obj
        name (str): harasser

    """
    self.log.info(
        "<<<PASS %s: %s started at %s>>>", self.loop, name, time.ctime())
    status = True
    # Create container
    container = TestContainer(pool)
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
        self.log.info("Sanpshot Created")
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
    params = {"name": name, "status": status, "vars": {}}
    with H_LOCK:
        self.harasser_job_done(params)
    self.log.info(
        "<<<PASS %s: %s completed at %s>>>\n", self.loop, name, time.ctime())


def launch_exclude_reintegrate(self, pool, name, results, args):
    """Launch the dmg cmd to exclude a rank in a pool.

    Args:
        self (obj): soak obj
        pool (obj): TestPool obj
        name (str): name of dmg subcommand
        results (queue): multiprocessing queue
        args (queue): multiprocessing queue
    """
    status = False
    params = {}
    rank = None
    if name == "EXCLUDE":
        exclude_servers = len(self.hostlist_servers) - 1
        # Exclude target : random two targets  (target idx : 0-7)
        n = random.randint(0, 6)
        target_list = [n, n+1]
        tgt_idx = "{}".format(','.join(str(tgt) for tgt in target_list))
        # Exclude one rank : other than rank 0 and 1.
        rank = random.randint(2, exclude_servers)
        # init the status dictionary
        params = {"name": name,
                  "status": status,
                  "vars": {"rank": rank, "tgt_idx": tgt_idx}}
        self.log.info("<<<PASS %s: %s started on rank %s at %s >>>\n",
                      self.loop, name, rank, time.ctime())
        try:
            pool.exclude(rank, tgt_idx=tgt_idx)
            status = True
        except TestFail as error:
            self.log.error(
                "<<<FAILED:dmg pool exclude failed", exc_info=error)
            status = False
        if status:
            status = wait_for_pool_rebuild(self, pool, name)

    elif name == "REINTEGRATE":
        if self.harasser_results["EXCLUDE"]:
            rank = self.harasser_args["EXCLUDE"]["rank"]
            tgt_idx = self.harasser_args["EXCLUDE"]["tgt_idx"]
            self.log.info("<<<PASS %s: %s started on rank %s at %s>>>\n",
                          self.loop, name, rank, time.ctime())
            status = pool.reintegrate(rank, tgt_idx)
            try:
                pool.reintegrate(rank, tgt_idx)
                status = True
            except TestFail as error:
                self.log.error(
                    "<<<FAILED:dmg pool reintegrate failed", exc_info=error)
                status = False
            if status:
                status = wait_for_pool_rebuild(self, pool, name)
        else:
            self.log.error("<<<PASS %s: %s failed due to EXCLUDE failure >>>",
                           self.loop, name)
            status = False
    params = {"name": name,
              "status": status,
              "vars": {"rank": rank, "tgt_idx": tgt_idx}}
    if not status:
        self.log.error("<<< %s failed - check logs for failure data>>>", name)
    self.dmg_command.system_query()
    self.harasser_job_done(params)
    results.put(self.harasser_results)
    args.put(self.harasser_args)
    self.log.info("Harasser results: %s", self.harasser_results)
    self.log.info("Harasser args: %s", self.harasser_args)
    self.log.info(
        "<<<PASS %s: %s completed at %s>>>\n", self.loop, name, time.ctime())


def launch_server_stop_start(self, pools, name, results, args):
    """Launch dmg server stop/start.

    Args:
        self (obj): soak obj
        pools (list): list of TestPool obj
        name (str): name of dmg subcommand
        results (queue): multiprocessing queue
        args (queue): multiprocessing queue

    """
    status = True
    params = {}
    rank = None
    drain = self.params.get("enable_drain", "/run/soak_harassers/*", False)
    if name == "SVR_STOP":
        exclude_servers = len(self.hostlist_servers) - 1
        # Exclude one rank : other than rank 0 and 1.
        rank = random.randint(2, exclude_servers)
        # init the status dictionary
        params = {"name": name,
                  "status": status,
                  "vars": {"rank": rank}}
        self.log.info("<<<PASS %s: %s - stop server: rank %s at %s >>>\n",
                      self.loop, name, rank, time.ctime())
        # drain pools
        drain_status = True
        if drain:
            for pool in pools:
                try:
                    pool.drain(rank)
                except TestFail as error:
                    self.log.error(
                        "<<<FAILED:dmg pool {} drain failed".format(
                            pool.uuid), exc_info=error)
                    status = False
                drain_status &= status
            if drain_status:
                for pool in pools:
                    drain_status &= wait_for_pool_rebuild(self, pool, name)
                status = drain_status
            else:
                status = False

        if status:
            # Shutdown the server
            try:
                self.dmg_command.system_stop(ranks=rank)
            except TestFail as error:
                self.log.error(
                    "<<<FAILED:dmg system stop failed", exc_info=error)
                status = False

    elif name == "SVR_REINTEGRATE":
        if self.harasser_results["SVR_STOP"]:
            rank = self.harasser_args["SVR_STOP"]["rank"]
            self.log.info("<<<PASS %s: %s started on rank %s at %s>>>\n",
                          self.loop, name, rank, time.ctime())
            try:
                self.dmg_command.system_start(ranks=rank)
                status = True
            except TestFail as error:
                self.log.error(
                    "<<<FAILED:dmg system start failed", exc_info=error)
                status = False
            # reintegrate ranks
            reintegrate_status = True
            for pool in pools:
                try:
                    pool.reintegrate(rank)
                    status = True
                except TestFail as error:
                    self.log.error(
                        "<<<FAILED:dmg pool {} reintegrate failed".format(
                            pool.uuid), exc_info=error)
                    status = False
                reintegrate_status &= status
            if reintegrate_status:
                for pool in pools:
                    reintegrate_status &= wait_for_pool_rebuild(
                        self, pool, name)
                status = reintegrate_status
            else:
                status = False
        else:
            self.log.error("<<<PASS %s: %s failed due to SVR_STOP failure >>>",
                           self.loop, name)
            status = False
    params = {"name": name,
              "status": status,
              "vars": {"rank": rank}}
    if not status:
        self.log.error("<<< %s failed - check logs for failure data>>>", name)
    self.dmg_command.system_query()
    self.harasser_job_done(params)
    results.put(self.harasser_results)
    args.put(self.harasser_args)
    self.log.info("Harasser results: %s", self.harasser_results)
    self.log.info("Harasser args: %s", self.harasser_args)
    self.log.info(
        "<<<PASS %s: %s completed at %s>>>\n", self.loop, name, time.ctime())


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


def start_dfuse(self, pool, container, nodesperjob, resource_mgr=None):
    """Create dfuse start command line for slurm.

    Args:
        self (obj): soak obj
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
    dfuse.set_dfuse_cont_param(container)

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
    """Cleanup and remove any dfuse mount points.

    Args:
        self (obj): soak obj

    """
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
        raise SoakTestError(
            "<<FAILED: Dfuse directories not deleted {} >>".format(error))


def create_ior_cmdline(self, job_spec, pool, ppn, nodesperjob):
    """Create an IOR cmdline to run in slurm batch.

    Args:

        self (obj): soak obj
        job_spec (str):   ior job in yaml to run
        pool (obj):       TestPool obj
        ppn(int):         number of tasks to run on each node
        nodesperjob(int): number of nodes per job

    Returns:
        cmd: cmdline string

    """
    commands = []
    ior_params = "/run/" + job_spec + "/*"
    mpi_module = self.params.get(
        "mpi_module", "/run/*", default="mpi/mpich-x86_64")
    # IOR job specs with a list of parameters; update each value
    api_list = self.params.get("api", ior_params + "*")
    tsize_list = self.params.get("transfer_size", ior_params + "*")
    bsize_list = self.params.get("block_size", ior_params + "*")
    oclass_list = self.params.get("dfs_oclass", ior_params + "*")
    plugin_path = self.params.get("plugin_path", "/run/hdf5_vol/")
    # update IOR cmdline for each additional IOR obj
    for api in api_list:
        for b_size in bsize_list:
            for t_size in tsize_list:
                for o_type in oclass_list:
                    # Cancel for ticket DAOS-6095
                    if (api in ["HDF5-VOL", "HDF5", "POSIX"]
                            and t_size == "4k"
                            and o_type in ["RP_2G1", 'RP_2GX']):
                        self.add_cancel_ticket(
                            "DAOS-6095",
                            "IOR -a {} with -t {} and -o {}".format(
                                api, t_size, o_type))
                        continue
                    # Cancel for ticket DAOS-6308
                    if api == "MPIIO" and o_type == "RP_2GX":
                        self.add_cancel_ticket(
                            "DAOS-6308",
                            "IOR -a {} with -o {}".format(api, o_type))
                        continue
                    ior_cmd = IorCommand()
                    ior_cmd.namespace = ior_params
                    ior_cmd.get_params(self)
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
                    ior_cmd.dfs_dir_oclass.update(o_type)
                    if ior_cmd.api.value == "DFS":
                        ior_cmd.test_file.update(
                            os.path.join("/", "testfile"))
                    add_containers(self, pool, o_type)
                    ior_cmd.set_daos_params(
                        self.server_group, pool, self.container[-1].uuid)
                    env = ior_cmd.get_default_env("srun")
                    sbatch_cmds = ["module load -q {}".format(mpi_module)]
                    # include dfuse cmdlines
                    if api in ["HDF5-VOL", "POSIX"]:
                        dfuse, dfuse_start_cmdlist = start_dfuse(
                            self, pool, self.container[-1],
                            nodesperjob, "SLURM")
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
                    log_name = "{}_{}_{}_{}".format(
                        api, b_size, t_size, o_type)
                    commands.append([sbatch_cmds, log_name])
                    self.log.info(
                        "<<IOR {} cmdlines>>:".format(api))
                    for cmd in sbatch_cmds:
                        self.log.info("%s", cmd)
    return commands


def create_fio_cmdline(self, job_spec, pool):
    """Create the FOI commandline for job script.

    Args:

        self (obj): soak obj
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
    oclass_list = self.params.get("oclass", fio_namespace + "/soak/*")
    # Get the parameters for Fio
    fio_cmd = FioCommand()
    fio_cmd.namespace = "{}/*".format(fio_namespace)
    fio_cmd.get_params(self)
    for blocksize in bs_list:
        for size in size_list:
            for rw in rw_list:
                for o_type in oclass_list:
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
                        add_containers(self, pool, o_type)
                        dfuse, srun_cmds = start_dfuse(
                            self, pool, self.container[-1], nodesperjob=1)
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
                    log_name = "{}_{}_{}_{}".format(blocksize, size, rw, o_type)
                    commands.append([srun_cmds, log_name])
                    self.log.info("<<Fio cmdlines>>:")
                    for cmd in srun_cmds:
                        self.log.info("%s", cmd)
    return commands


def build_job_script(self, commands, job, ppn, nodesperjob):
    """Create a slurm batch script that will execute a list of cmdlines.

    Args:
        self (obj): soak obj
        commands(list): commandlines and cmd specific log_name
        job(str): the job name that will be defined in the slurm script
        ppn(int): number of tasks to run on each node

    Returns:
        script_list: list of slurm batch scripts

    """
    self.log.info("<<Build Script>> at %s", time.ctime())
    script_list = []
    # if additional cmds are needed in the batch script
    dmg_config = self.dmg_command.configpath.value
    prepend_cmds = [
        "/usr/bin/dmg pool query -o {} --pool {} ".format(
            dmg_config, self.pool[1].uuid),
        "/usr/bin/dmg pool query -o {} --pool {} ".format(
            dmg_config, self.pool[0].uuid)]
    append_cmds = [
        "/usr/bin/dmg pool query -o {} --pool {} ".format(
            dmg_config, self.pool[1].uuid),
        "/usr/bin/dmg pool query -o {} --pool {} ".format(
            dmg_config, self.pool[0].uuid)]
    exit_cmd = ["exit $status"]
    # Create the sbatch script for each list of cmdlines
    for cmd, log_name in commands:
        if isinstance(cmd, str):
            cmd = [cmd]
        output = os.path.join(
            self.test_log_dir, self.test_name + "_" + job + "_" +
            log_name + "_" + str(ppn * nodesperjob) + "_" + str(nodesperjob) +
            "_" + str(ppn) + "_%N_" + "%j_")
        error = os.path.join(str(output) + "ERROR_")
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
            prepend_cmds + cmd + append_cmds + exit_cmd, unique, sbatch)
        script_list.append(script)
        self.used.append(unique)
    return script_list


class SoakTestError(Exception):
    """Soak exception class."""
