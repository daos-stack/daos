"""
(C) Copyright 2019-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines

import os
import time
import random
import threading
import re
from ior_utils import IorCommand
from fio_utils import FioCommand
from mdtest_utils import MdtestCommand
from daos_racer_utils import DaosRacerCommand
from data_mover_utils import FsCopy
from dfuse_utils import Dfuse
from job_manager_utils import Srun, Mpirun
from general_utils import get_host_data, get_random_string, \
    run_command, DaosTestError, pcmd, get_random_bytes, \
    run_pcmd, convert_list, get_log_file
from command_utils_base import EnvironmentVariables
import slurm_utils
from daos_utils import DaosCommand
from test_utils_container import TestContainer
from ClusterShell.NodeSet import NodeSet
from avocado.core.exceptions import TestFail
from pydaos.raw import DaosSnapshot, DaosApiError
from macsio_util import MacsioCommand
from oclass_utils import extract_redundancy_factor

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
    num_days = int(seconds / 86400)
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
        self.pool.append(self.get_pool(namespace=path, connect=False))
        self.log.info("Valid Pool UUID is %s", self.pool[-1].uuid)


def add_containers(self, pool, oclass=None, path="/run/container/*"):
    """Create a list of containers that the various jobs use for storage.

    Args:
        pool: pool to create container
        oclass: object class of container


    """
    rf = None
    # Create a container and add it to the overall list of containers
    self.container.append(
        TestContainer(pool, daos_command=self.get_daos_command()))
    self.container[-1].namespace = path
    self.container[-1].get_params(self)
    # include rf based on the class
    if oclass:
        self.container[-1].oclass.update(oclass)
        redundancy_factor = extract_redundancy_factor(oclass)
        rf = 'rf:{}'.format(str(redundancy_factor))
    properties = self.container[-1].properties.value
    cont_properties = (",").join(filter(None, [properties, rf]))
    if cont_properties is not None:
        self.container[-1].properties.update(cont_properties)
    self.container[-1].create()


def reserved_file_copy(self, file, pool, container, num_bytes=None, cmd="read"):
    """Move data between a POSIX file and a container.

    Args:
        text_file (str): posix path/file to write random data to
        num_bytes (int): num of bytes to write to file
        pool (TestPool obj): pool to read/write random data file
        container (TestContainer obj): container to read/write random data file
        cmd (str): whether the data is a read
                    (daos->posix) or write(posix -> daos)
    """
    os.makedirs(os.path.dirname(file), exist_ok=True)
    fscopy_cmd = FsCopy(self.get_daos_command(), self.log)
    # writes random data to file and then copy the file to container
    if cmd == "write":
        with open(file, 'w') as src_file:
            src_file.write(str(os.urandom(num_bytes)))
            src_file.close()
        dst_file = "daos://{}/{}".format(pool.uuid, container.uuid)
        fscopy_cmd.set_params(src=file, dst=dst_file)
        fscopy_cmd.run()
    # reads file_name from container and writes to file
    elif cmd == "read":
        dst = os.path.split(file)
        dst_name = dst[-1]
        dst_path = dst[0]
        src_file = "daos://{}/{}/{}".format(
            pool.uuid, container.uuid, dst_name)
        fscopy_cmd.set_params(src=src_file, dst=dst_path)
        fscopy_cmd.run()


def get_remote_dir(self, source_dir, dest_dir, host_list, shared_dir=None,
                   rm_remote=True, append=None):
    """Copy files from remote dir to local dir.

    Args:
        self (obj): soak obj
        source_dir (str): Source directory to archive
        dest_dir (str): Destination directory
        host_list (list): list of hosts

    Raises:
        SoakTestError: if there is an error with the remote copy

    """
    if shared_dir is None:
        shared_dir = self.sharedsoaktest_dir
    if append:
        for host in host_list:
            shared_dir_tmp = shared_dir + append + "{}".format(host)
            dest_dir_tmp = dest_dir + append + "{}".format(host)
            if not os.path.exists(shared_dir_tmp):
                os.mkdir(shared_dir_tmp)
            if not os.path.exists(dest_dir_tmp):
                os.mkdir(dest_dir_tmp)
            # copy the directory from each client node to a shared directory
            # tagged with the hostname
            command = "/usr/bin/rsync -avtr --min-size=1B {0} {1}/..".format(
                source_dir, shared_dir_tmp)
            result = slurm_utils.srun(
                self.log, self.control, NodeSet.fromlist([host]), command, self.srun_params,
                timeout=300)
            if not result.passed:
                raise SoakTestError(
                    "<<FAILED: Soak remote logfiles not copied from clients>>: {}".format(host))
            command = "/usr/bin/cp -R -p {0}/ \'{1}\'".format(shared_dir_tmp, dest_dir)
            try:
                run_command(command, timeout=30)
            except DaosTestError as error:
                raise SoakTestError(
                    "<<FAILED: Soak logfiles not copied from shared area>>: {}".format(
                        shared_dir_tmp)) from error

    else:
        # copy the remote dir on all client nodes to a shared directory
        command = "/usr/bin/rsync -avtr --min-size=1B {0} {1}/..".format(source_dir, shared_dir)
        result = slurm_utils.srun(
            self.log, self.control, NodeSet.fromlist(host_list), command, self.srun_params,
            timeout=300)
        if not result.passed:
            raise SoakTestError(
                "<<FAILED: Soak remote logfiles not copied from clients>>: {}".format(host_list))

        # copy the local logs and the logs in the shared dir to avocado dir
        for directory in [source_dir, shared_dir]:
            command = "/usr/bin/cp -R -p {0}/ \'{1}\'".format(directory, dest_dir)
            try:
                run_command(command, timeout=30)
            except DaosTestError as error:
                raise SoakTestError(
                    "<<FAILED: Soak logfiles not copied from shared area>>: {}".format(
                        directory)) from error

    if rm_remote:
        # remove the remote soak logs for this pass
        command = "/usr/bin/rm -rf {0}".format(source_dir)
        result = slurm_utils.srun(
            self.log, self.control, NodeSet.fromlist(host_list), command, self.srun_params)
        if not result.passed:
            raise SoakTestError(
                "<<FAILED: Soak logfiles removal failed>>: {} on {}".format(directory, host_list))
        # remove the local log for this pass
        for directory in [source_dir, shared_dir]:
            command = "/usr/bin/rm -rf {0}".format(directory)
            try:
                run_command(command, timeout=30)
            except DaosTestError as error:
                raise SoakTestError(
                    "<<FAILED: Soak logfiles removal failed>>: {}".format(directory)) from error


def write_logfile(data, name, destination):
    """Write data to the local destination file.

    Args:
        self (obj): soak obj
        data (str): data to write to file
        destination (str): local avocado directory
    """
    if not os.path.exists(destination):
        os.makedirs(destination)
    logfile = destination + "/" + str(name)
    with open(logfile, 'w') as log_file:
        # identify what be used to run this script
        if isinstance(data, list):
            text = "\n".join(data)
            log_file.write(text)
        else:
            log_file.write(str(data))


def run_event_check(self, since, until):
    """Run a check on specific events in journalctl.

    Args:
        self (obj): soak obj
        since (str): start time
        until (str): end time
        log (bool):  If true; write the events to a logfile

    Returns list of any matched events found in system log

    """
    # pylint: disable=too-many-nested-blocks

    events_found = []
    detected = 0
    events = self.params.get("events", "/run/*")
    # check events on all server nodes
    hosts = list(set(self.hostlist_servers))
    if events:
        for journalctl_type in ["kernel", "daos_server"]:
            for output in get_journalctl(self, hosts, since, until, journalctl_type):
                for event in events:
                    lines = output["data"].splitlines()
                    for line in lines:
                        match = re.search(r"{}".format(event), str(line))
                        if match:
                            events_found.append(line)
                            detected += 1
                    self.log.info(
                        "Found %s instances of %s in system log from %s through %s",
                        detected, event, since, until)
    return events_found


def get_journalctl(self, hosts, since, until, journalctl_type, logging=False):
    """Run the journalctl on daos servers.

    Args:
        self (obj): soak obj
        since (str): start time
        until (str): end time
        journalctl_type (str): the -t param for journalctl
        log (bool):  If true; write the events to a logfile

    Returns:
        list: a list of dictionaries containing the following key/value pairs:
            "hosts": NodeSet containing the hosts with this data
            "data":  data requested for the group of hosts

    """
    command = "{} /usr/bin/journalctl --system -t {} --since=\"{}\" --until=\"{}\"".format(
        self.sudo_cmd, journalctl_type, since, until)
    err = "Error gathering system log events"
    results = get_host_data(hosts, command, "journalctl", err)
    name = "journalctl_{}.log".format(journalctl_type)
    destination = self.outputsoak_dir
    if logging:
        for result in results:
            host = result["hosts"]
            log_name = name + "-" + str(host)
            self.log.info("Logging %s output to %s", command, log_name)
            write_logfile(result["data"], log_name, destination)
    return results


def get_daos_server_logs(self):
    """Gather server logs.

    Args:
        self (obj): soak obj

    """
    for host in self.hostlist_servers:
        daos_dir = self.outputsoak_dir + "/daos_logs-" + "{}".format(host)
        if not os.path.exists(daos_dir):
            os.mkdir(daos_dir)
            commands = ["scp {}:/var/tmp/daos_testing/daos*.log.* {}".format(host, daos_dir),
                        "scp {}:/var/tmp/daos_testing/daos*.log {}".format(host, daos_dir)]
            for command in commands:
                try:
                    run_command(command, timeout=30)
                except DaosTestError as error:
                    raise SoakTestError(
                        "<<FAILED: daos logs file from {} not copied>>".format(host)) from error


def run_monitor_check(self):
    """Monitor server cpu, memory usage periodically.

    Args:
        self (obj): soak obj

    """
    monitor_cmds = self.params.get("monitor", "/run/*")
    hosts = self.hostlist_servers
    if monitor_cmds:
        for cmd in monitor_cmds:
            pcmd(hosts, cmd, timeout=30)


def run_metrics_check(self, logging=True, prefix=None):
    """Monitor telemetry data.

    Args:
        self (obj): soak obj
        logging (bool): If True; output is logged to file
        prefix (str): add prefix to name; ie initial or final
    """
    enable_telemetry = self.params.get("enable_telemetry", "/run/*")
    engine_count = self.params.get("engines_per_host", "/run/server_config/*", default=1)

    if enable_telemetry:
        for engine in range(engine_count):
            name = "pass" + str(self.loop) + "_metrics_{}.csv".format(engine)
            if prefix:
                name = prefix + "_metrics_{}.csv".format(engine)
            destination = self.outputsoak_dir
            daos_metrics = "{} daos_metrics -S {} --csv".format(self.sudo_cmd, engine)
            self.log.info("Running %s", daos_metrics)
            results = run_pcmd(hosts=self.hostlist_servers,
                               command=daos_metrics,
                               verbose=(not logging),
                               timeout=60)
            if logging:
                for result in results:
                    hosts = result["hosts"]
                    log_name = name + "-" + str(hosts)
                    self.log.info("Logging %s output to %s", daos_metrics, log_name)
                    write_logfile(result["stdout"], log_name, destination)


def get_harassers(harasser):
    """Create a valid harasser list from the yaml job harassers.

    Args:
        harassers (list): harasser jobs from yaml.

    Returns:
        harasserlist (list): Ordered list of harassers to execute
                             per pass of soak

    """
    harasserlist = []
    offline_harasserlist = []
    if "-offline" in harasser:
        offline_harasser = harasser.replace("-offline", "")
        offline_harasserlist.extend(offline_harasser.split("_"))
    else:
        harasserlist.extend(harasser.split("_"))
    return harasserlist, offline_harasserlist


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
        # # Wait for rebuild to start
        # pool.wait_for_rebuild(True)
        # Wait for rebuild to complete
        pool.wait_for_rebuild(False)
        rebuild_status = True
    except DaosTestError as error:
        self.log.error(
            "<<<FAILED:{} rebuild timed out: {}".format(
                name, error), exc_info=error)
        rebuild_status = False
    except TestFail as error1:
        self.log.error(
            "<<<FAILED:{} rebuild failed due to test issue: {}".format(
                name, error1), exc_info=error1)
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
    data_pattern = get_random_bytes(500)
    datasize = len(data_pattern) + 1
    dkey = b"dkey"
    akey = b"akey"
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
        data_pattern2 = get_random_bytes(500)
        datasize2 = len(data_pattern2) + 1
        dkey = b"dkey"
        akey = b"akey"
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
                self.log.error("Snapshot data miscompare")
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
    tgt_idx = None
    if name == "EXCLUDE":
        targets = self.params.get("targets_exclude", "/run/soak_harassers/*", 8)
        engine_count = self.params.get("engines_per_host", "/run/server_config/*", default=1)
        exclude_servers = (
            len(self.hostlist_servers) * int(engine_count)) - 1
        # Exclude one rank.
        rank = random.randint(0, exclude_servers)  # nosec

        if targets >= 8:
            tgt_idx = None
        else:
            target_list = random.sample(range(0, 8), targets)
            tgt_idx = "{}".format(','.join(str(tgt) for tgt in target_list))

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
            try:
                pool.reintegrate(rank, tgt_idx=tgt_idx)
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
    engine_count = self.params.get("engines_per_host", "/run/server_config/*", default=1)
    if name == "SVR_STOP":
        exclude_servers = (
            len(self.hostlist_servers) * int(engine_count)) - 1
        # Exclude one rank.
        rank = random.randint(0, exclude_servers)  # nosec
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
                    drain_status &= wait_for_pool_rebuild(self, pool, name)
                    status = drain_status
                else:
                    status = False
        if status:
            # Shutdown the server
            try:
                self.dmg_command.system_stop(force=True, ranks=rank)
            except TestFail as error:
                self.log.error(
                    "<<<FAILED:dmg system stop failed", exc_info=error)
                status = False
            time.sleep(30)
            if not drain:
                rebuild_status = True
                for pool in pools:
                    rebuild_status &= wait_for_pool_rebuild(self, pool, name)
                status = rebuild_status
    elif name == "SVR_START":
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
        else:
            self.log.error(
                "<<<PASS %s: %s failed due to SVR_STOP failure >>>",
                self.loop, name)
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
            for pool in pools:
                self.dmg_command.pool_query(pool.uuid)
            if status:
                # Wait ~ 30 sec before issuing the reintegrate
                time.sleep(30)
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
        for key, value in list(srun_params.items()):
            key_obj = getattr(srun_cmd, key)
            if key_obj is not None and hasattr(key_obj, "update"):
                key_obj.update(value, key)
            else:
                raise SoakTestError(
                    "<<FAILED: The srun param {} does not exist".format(key))
    if env:
        srun_cmd.assign_environment(env)
    return str(srun_cmd)


def start_dfuse(self, pool, container, name=None, job_spec=None):
    """Create dfuse start command line for slurm.

    Args:
        self (obj): soak obj
        pool (obj):             TestPool obj

    Returns dfuse(obj):         Dfuse obj
            cmd(list):          list of dfuse commands to add to job script
    """
    # Get Dfuse params
    dfuse = Dfuse(self.hostlist_clients, self.tmp)
    dfuse.namespace = os.path.join(os.sep, "run", job_spec, "dfuse", "*")

    dfuse.get_params(self)
    # update dfuse params; mountpoint for each container
    unique = get_random_string(5, self.used)
    self.used.append(unique)
    mount_dir = dfuse.mount_dir.value + unique
    dfuse.update_params(mount_dir=mount_dir, pool=pool.identifier, cont=container.uuid)
    dfuse_log = os.path.join(
        self.soaktest_dir,
        self.test_name + "_" + name + "_`hostname -s`_"
        "" + "${SLURM_JOB_ID}_" + "daos_dfuse_" + unique)
    dfuse_env = "export D_LOG_MASK=ERR;export D_LOG_FILE={}".format(dfuse_log)
    module_load = "module load {}".format(self.mpi_module)
    dfuse_start_cmds = [
        "clush -S -w $SLURM_JOB_NODELIST \"mkdir -p {}\"".format(dfuse.mount_dir.value),
        "clush -S -w $SLURM_JOB_NODELIST \"cd {};{};{};{}\"".format(
            dfuse.mount_dir.value, dfuse_env, module_load, str(dfuse)),
        "sleep 10",
        "clush -S -w $SLURM_JOB_NODELIST \"df -h {}\"".format(dfuse.mount_dir.value),
    ]
    return dfuse, dfuse_start_cmds


def stop_dfuse(dfuse, vol=False):
    """Create dfuse stop command line for slurm.

    Args:
        dfuse (obj): Dfuse obj
        vol:(bool): cmd is ior hdf5 with vol connector

    Returns cmd(list):    list of cmds to pass to slurm script
    """
    dfuse_stop_cmds = []
    if vol:
        dfuse_stop_cmds.extend([
            "for file in $(ls {0}) ; "
            "do daos container destroy --path={0}/\"$file\" ; done".format(
                dfuse.mount_dir.value)])

    dfuse_stop_cmds.extend([
        "clush -S -w $SLURM_JOB_NODELIST \"fusermount3 -uz {0}\"".format(dfuse.mount_dir.value),
        "clush -S -w $SLURM_JOB_NODELIST \"rm -rf {0}\"".format(dfuse.mount_dir.value)])
    return dfuse_stop_cmds


def cleanup_dfuse(self):
    """Cleanup and remove any dfuse mount points.

    Args:
        self (obj): soak obj

    """
    cmd = [
        "/usr/bin/bash -c 'for pid in $(pgrep dfuse)",
        "do kill $pid",
        "done'"]
    cmd2 = [
        "/usr/bin/bash -c 'for dir in $(find /tmp/daos_dfuse/)",
        "do fusermount3 -uz $dir",
        "rm -rf $dir",
        "done'"]
    result = slurm_utils.srun(
        self.log, self.control, NodeSet.fromlist(self.hostlist_clients), ";".join(cmd),
        self.srun_params, timeout=600)
    if not result.passed:
        self.log.info("Dfuse processes not stopped Error")
    result = slurm_utils.srun(
        self.log, self.control, NodeSet.fromlist(self.hostlist_clients), ";".join(cmd2),
        self.srun_params, timeout=600)
    if not result.passed:
        self.log.info("Dfuse mount points not deleted Error")


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
    vol = False
    ior_params = os.path.join(os.sep, "run", job_spec, "*")
    ior_timeout = self.params.get("job_timeout", ior_params, 10)
    # IOR job specs with a list of parameters; update each value
    api_list = self.params.get("api", ior_params)
    tsize_list = self.params.get("transfer_size", ior_params)
    bsize_list = self.params.get("block_size", ior_params)
    oclass_list = self.params.get("dfs_oclass", ior_params)
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
                    if api in ["HDF5-VOL", "HDF5", "POSIX"] and ppn > 16:
                        continue
                    ior_cmd = IorCommand()
                    ior_cmd.namespace = ior_params
                    ior_cmd.get_params(self)
                    ior_cmd.max_duration.update(ior_timeout)
                    if api == "HDF5-VOL":
                        ior_cmd.api.update("HDF5")
                    else:
                        ior_cmd.api.update(api)
                    ior_cmd.block_size.update(b_size)
                    ior_cmd.transfer_size.update(t_size)
                    if (api in ["HDF5-VOL", "POSIX"]):
                        ior_cmd.dfs_oclass.update(None)
                        ior_cmd.dfs_dir_oclass.update(None)
                    else:
                        ior_cmd.dfs_oclass.update(o_type)
                        ior_cmd.dfs_dir_oclass.update(o_type)
                    if ior_cmd.api.value == "DFS":
                        ior_cmd.test_file.update(
                            os.path.join("/", "testfile"))
                    add_containers(self, pool, o_type)
                    ior_cmd.set_daos_params(
                        self.server_group, pool, self.container[-1].uuid)
                    log_name = "{}_{}_{}_{}_{}_{}_{}_{}".format(
                        job_spec, api, b_size, t_size,
                        o_type, nodesperjob * ppn, nodesperjob, ppn)
                    daos_log = os.path.join(
                        self.soaktest_dir, self.test_name + "_" + log_name +
                        "_`hostname -s`_${SLURM_JOB_ID}_daos.log")
                    env = ior_cmd.get_default_env("mpirun", log_file=daos_log)
                    sbatch_cmds = ["module purge", "module load {}".format(self.mpi_module)]
                    # include dfuse cmdlines
                    if api in ["HDF5-VOL", "POSIX"]:
                        dfuse, dfuse_start_cmdlist = start_dfuse(
                            self, pool, self.container[-1], name=log_name, job_spec=job_spec)
                        sbatch_cmds.extend(dfuse_start_cmdlist)
                        ior_cmd.test_file.update(
                            os.path.join(dfuse.mount_dir.value, "testfile"))
                    mpirun_cmd = Mpirun(ior_cmd, mpi_type=self.mpi_module)
                    mpirun_cmd.get_params(self)
                    # add envs if api is HDF5-VOL
                    if api == "HDF5-VOL":
                        vol = True
                        env["HDF5_VOL_CONNECTOR"] = "daos"
                        env["HDF5_PLUGIN_PATH"] = "{}".format(plugin_path)
                    mpirun_cmd.assign_processes(nodesperjob * ppn)
                    mpirun_cmd.assign_environment(env, True)
                    mpirun_cmd.ppn.update(ppn)
                    sbatch_cmds.append(str(mpirun_cmd))
                    sbatch_cmds.append("status=$?")
                    if api in ["HDF5-VOL", "POSIX"]:
                        sbatch_cmds.extend(stop_dfuse(dfuse, vol))
                    commands.append([sbatch_cmds, log_name])
                    self.log.info(
                        "<<IOR {} cmdlines>>:".format(api))
                    for cmd in sbatch_cmds:
                        self.log.info("%s", cmd)
    return commands


def create_macsio_cmdline(self, job_spec, pool, ppn, nodesperjob):
    """Create an MACsio cmdline to run in slurm batch.

    Args:

        self (obj): soak obj
        job_spec (str):   macsio job in yaml to run
        pool (obj):       TestPool obj
        ppn(int):         number of tasks to run on each node
        nodesperjob(int): number of nodes per job

    Returns:
        cmd: cmdline string

    """
    commands = []
    macsio_params = os.path.join(os.sep, "run", job_spec, "*")
    oclass_list = self.params.get("oclass", macsio_params)
    api_list = self.params.get("api", macsio_params)
    plugin_path = self.params.get("plugin_path", "/run/hdf5_vol/")
    # update macsio cmdline for each additional MACsio obj
    for api in api_list:
        for o_type in oclass_list:
            add_containers(self, pool, o_type)
            macsio = MacsioCommand()
            macsio.namespace = macsio_params
            macsio.get_params(self)
            macsio.daos_pool = pool.uuid
            macsio.daos_svcl = convert_list(pool.svc_ranks)
            macsio.daos_cont = self.container[-1].uuid
            log_name = "{}_{}_{}_{}_{}_{}".format(
                job_spec, api, o_type, nodesperjob * ppn, nodesperjob, ppn)
            daos_log = os.path.join(
                self.soaktest_dir, self.test_name +
                "_" + log_name + "_`hostname -s`_${SLURM_JOB_ID}_daos.log")
            macsio_log = os.path.join(
                self.soaktest_dir, self.test_name +
                "_" + log_name + "_`hostname -s`_${SLURM_JOB_ID}_macsio-log.log")
            macsio_timing_log = os.path.join(
                self.soaktest_dir, self.test_name +
                "_" + log_name + "_`hostname -s`_${SLURM_JOB_ID}_macsio-timing.log")
            macsio.log_file_name.update(macsio_log)
            macsio.timings_file_name.update(macsio_timing_log)
            env = macsio.env.copy()
            env["D_LOG_FILE"] = get_log_file(daos_log or "{}_daos.log".format(macsio.command))

            sbatch_cmds = ["module purge", "module load {}".format(self.mpi_module)]
            mpirun_cmd = Mpirun(macsio, mpi_type=self.mpi_module)
            mpirun_cmd.get_params(self)
            mpirun_cmd.assign_processes(nodesperjob * ppn)
            if api in ["HDF5-VOL"]:
                # include dfuse cmdlines
                dfuse, dfuse_start_cmdlist = start_dfuse(
                    self, pool, self.container[-1], name=log_name, job_spec=job_spec)
                sbatch_cmds.extend(dfuse_start_cmdlist)
                # add envs for HDF5-VOL
                env["HDF5_VOL_CONNECTOR"] = "daos"
                env["HDF5_PLUGIN_PATH"] = "{}".format(plugin_path)
                mpirun_cmd.working_dir.update(dfuse.mount_dir.value)
            mpirun_cmd.assign_environment(env, True)
            mpirun_cmd.ppn.update(ppn)
            sbatch_cmds.append(str(mpirun_cmd))
            sbatch_cmds.append("status=$?")
            if api in ["HDF5-VOL"]:
                sbatch_cmds.extend(stop_dfuse(dfuse, vol=True))
            commands.append([sbatch_cmds, log_name])
            self.log.info("<<MACSio cmdlines>>:")
            for cmd in sbatch_cmds:
                self.log.info("%s", cmd)
    return commands


def create_mdtest_cmdline(self, job_spec, pool, ppn, nodesperjob):
    """Create an MDTEST cmdline to run in slurm batch.

    Args:

        self (obj): soak obj
        job_spec (str):   mdtest job in yaml to run
        pool (obj):       TestPool obj
        ppn(int):         number of tasks to run on each node
        nodesperjob(int): number of nodes per job

    Returns:
        cmd: cmdline string

    """
    # pylint: disable=too-many-nested-blocks

    commands = []
    mdtest_params = os.path.join(os.sep, "run", job_spec, "*")
    # mdtest job specs with a list of parameters; update each value
    api_list = self.params.get("api", mdtest_params)
    write_bytes_list = self.params.get("write_bytes", mdtest_params)
    read_bytes_list = self.params.get("read_bytes", mdtest_params)
    depth_list = self.params.get("depth", mdtest_params)
    flag = self.params.get("flags", mdtest_params)
    oclass_list = self.params.get("dfs_oclass", mdtest_params)
    num_of_files_dirs = self.params.get(
        "num_of_files_dirs", mdtest_params)
    # update mdtest cmdline for each additional mdtest obj
    for api in api_list:
        if api in ["POSIX"] and ppn > 16:
            continue
        for write_bytes in write_bytes_list:
            for read_bytes in read_bytes_list:
                for depth in depth_list:
                    for oclass in oclass_list:
                        # Get the parameters for Mdtest
                        mdtest_cmd = MdtestCommand()
                        mdtest_cmd.namespace = mdtest_params
                        mdtest_cmd.get_params(self)
                        mdtest_cmd.api.update(api)
                        mdtest_cmd.write_bytes.update(write_bytes)
                        mdtest_cmd.read_bytes.update(read_bytes)
                        mdtest_cmd.depth.update(depth)
                        mdtest_cmd.flags.update(flag)
                        mdtest_cmd.num_of_files_dirs.update(num_of_files_dirs)
                        mdtest_cmd.dfs_oclass.update(oclass)
                        mdtest_cmd.dfs_dir_oclass.update(oclass)
                        if "EC" in oclass:
                            # oclass_dir can not be EC must be RP based on rf
                            rf = extract_redundancy_factor(oclass)
                            if rf >= 2:
                                mdtest_cmd.dfs_dir_oclass.update("RP_3G1")
                            elif rf == 1:
                                mdtest_cmd.dfs_dir_oclass.update("RP_2G1")
                            else:
                                mdtest_cmd.dfs_dir_oclass.update("SX")
                        add_containers(self, pool, oclass)
                        mdtest_cmd.set_daos_params(
                            self.server_group, pool,
                            self.container[-1].uuid)
                        log_name = "{}_{}_{}_{}_{}_{}_{}_{}_{}".format(
                            job_spec, api, write_bytes, read_bytes, depth,
                            oclass, nodesperjob * ppn, nodesperjob,
                            ppn)
                        daos_log = os.path.join(
                            self.soaktest_dir, self.test_name + "_" + log_name +
                            "_`hostname -s`_${SLURM_JOB_ID}_daos.log")
                        env = mdtest_cmd.get_default_env("mpirun", log_file=daos_log)
                        sbatch_cmds = [
                            "module purge", "module load {}".format(self.mpi_module)]
                        # include dfuse cmdlines

                        if api in ["POSIX"]:
                            dfuse, dfuse_start_cmdlist = start_dfuse(
                                self, pool, self.container[-1], name=log_name, job_spec=job_spec)
                            sbatch_cmds.extend(dfuse_start_cmdlist)
                            mdtest_cmd.test_dir.update(
                                dfuse.mount_dir.value)
                        mpirun_cmd = Mpirun(mdtest_cmd, mpi_type=self.mpi_module)
                        mpirun_cmd.get_params(self)
                        mpirun_cmd.assign_processes(nodesperjob * ppn)
                        mpirun_cmd.assign_environment(env, True)
                        mpirun_cmd.ppn.update(ppn)
                        sbatch_cmds.append(str(mpirun_cmd))
                        sbatch_cmds.append("status=$?")
                        if api in ["POSIX"]:
                            sbatch_cmds.extend(stop_dfuse(dfuse))
                        commands.append([sbatch_cmds, log_name])
                        self.log.info(
                            "<<MDTEST {} cmdlines>>:".format(api))
                        for cmd in sbatch_cmds:
                            self.log.info("%s", cmd)
    return commands


def create_racer_cmdline(self, job_spec):
    """Create the srun cmdline to run daos_racer.

    Args:
        self (obj): soak obj
        job_spec (str): fio job in yaml to run
    Returns:
        cmd(list): list of cmdlines

    """
    commands = []
    # daos_racer needs its own pool; does not run using jobs pool
    add_pools(self, ["pool_racer"])
    add_containers(self, self.pool[-1], "SX")
    racer_namespace = os.path.join(os.sep, "run", job_spec, "*")
    daos_racer = DaosRacerCommand(
        self.bin, self.hostlist_clients[0])
    daos_racer.namespace = racer_namespace
    daos_racer.get_params(self)
    daos_racer.pool_uuid.update(self.pool[-1].uuid)
    daos_racer.cont_uuid.update(self.container[-1].uuid)
    racer_log = os.path.join(
        self.soaktest_dir,
        self.test_name + "_" + job_spec + "_`hostname -s`_"
        "${SLURM_JOB_ID}_" + "racer_log")
    daos_racer.env["D_LOG_FILE"] = get_log_file(racer_log)
    log_name = job_spec
    cmds = []
    cmds.append(str(daos_racer))
    cmds.append("status=$?")
    # add exit code
    commands.append([cmds, log_name])
    self.log.info("<<DAOS racer cmdlines>>:")
    for cmd in cmds:
        self.log.info("%s", cmd)
    return commands


def create_fio_cmdline(self, job_spec, pool):
    """Create the FOI command line for job script.

    Args:

        self (obj): soak obj
        job_spec (str): fio job in yaml to run
        pool (obj):   TestPool obj
        ppn(int): number of tasks to run on each node

    Returns:
        cmd(list): list of cmdlines

    """
    commands = []
    fio_namespace = os.path.join(os.sep, "run", job_spec, "*")
    fio_soak_namespace = os.path.join(os.sep, "run", job_spec, "soak", "*")
    # test params
    bs_list = self.params.get("blocksize", fio_soak_namespace)
    size_list = self.params.get("size", fio_soak_namespace)
    rw_list = self.params.get("rw", fio_soak_namespace)
    oclass_list = self.params.get("oclass", fio_soak_namespace)
    # Get the parameters for Fio
    fio_cmd = FioCommand()
    fio_cmd.namespace = fio_namespace
    fio_cmd.get_params(self)
    fio_cmd.aux_path.update(self.test_dir, "aux_path")
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
                    cmds = []
                    # add srun start dfuse cmds if api is POSIX
                    if fio_cmd.api.value == "POSIX":
                        # Connect to the pool, create container
                        # and then start dfuse
                        add_containers(self, pool, o_type)
                        daos_cmd = DaosCommand(self.bin)
                        daos_cmd.container_set_attr(pool.uuid,
                                                    self.container[-1].uuid,
                                                    'dfuse-direct-io-disable',
                                                    'on')
                        log_name = "{}_{}_{}_{}_{}".format(
                            job_spec, blocksize, size, rw, o_type)
                        dfuse, cmds = start_dfuse(
                            self, pool, self.container[-1], name=log_name, job_spec=job_spec)
                    # Update the FIO cmdline
                    fio_cmd.update(
                        "global", "directory",
                        dfuse.mount_dir.value,
                        "fio --name=global --directory")
                    # add fio cmdline
                    cmds.append(str(fio_cmd))
                    cmds.append("status=$?")
                    # If posix, add the srun dfuse stop cmds
                    if fio_cmd.api.value == "POSIX":
                        cmds.extend(stop_dfuse(dfuse))
                    commands.append([cmds, log_name])
                    self.log.info("<<Fio cmdlines>>:")
                    for cmd in cmds:
                        self.log.info("%s", cmd)
    return commands


def create_app_cmdline(self, job_spec, pool, ppn, nodesperjob):
    """Create the srun cmdline to run app.

    This method will use a cmdline specified in the yaml file to
    execute a local binary until the rpms are available
    Args:
        self (obj):       soak obj
        job_spec (str):   job in yaml to run
        pool (obj):       TestPool obj
        ppn(int):         number of tasks to run on each node
        nodesperjob(int): number of nodes per job
    Returns:
        cmd(list): list of cmdlines

    """
    commands = []
    sbatch_cmds = []
    app_params = os.path.join(os.sep, "run", job_spec, "*")
    app_cmd = os.path.expandvars(self.params.get("cmdline", app_params, default=None))
    mpi_module = self.params.get("module", app_params, self.mpi_module)
    posix = self.params.get("posix", app_params, default=False)
    if app_cmd is None:
        self.log.info(
            "<<{} command line not specified in yaml; job will not be run>>".format(job_spec))
        return commands

    oclass_list = self.params.get("oclass", app_params)
    for oclass in oclass_list:
        add_containers(self, pool, oclass)
        sbatch_cmds = ["module purge", "module load {}".format(self.mpi_module)]
        log_name = "{}_{}_{}_{}_{}".format(
            job_spec, oclass, nodesperjob * ppn, nodesperjob, ppn)
        # include dfuse cmdlines
        if posix:
            dfuse, dfuse_start_cmdlist = start_dfuse(
                self, pool, self.container[-1], name=log_name, job_spec=job_spec)
            sbatch_cmds.extend(dfuse_start_cmdlist)
        # allow apps that use an mpi other than default (self.mpi_module)
        if mpi_module != self.mpi_module:
            sbatch_cmds.append("module load {}".format(mpi_module))
        mpirun_cmd = Mpirun(app_cmd, False, mpi_module)
        mpirun_cmd.get_params(self)
        if "mpich" in mpi_module:
            # Pass pool and container information to the commands
            env = EnvironmentVariables()
            env["DAOS_UNS_PREFIX"] = "daos://{}/{}/".format(pool.uuid, self.container[-1].uuid)
            mpirun_cmd.assign_environment(env, True)
        mpirun_cmd.assign_processes(nodesperjob * ppn)
        mpirun_cmd.ppn.update(ppn)
        if posix:
            mpirun_cmd.working_dir.update(dfuse.mount_dir.value)
        cmdline = "{}".format(str(mpirun_cmd))
        sbatch_cmds.append(str(cmdline))
        sbatch_cmds.append("status=$?")
        if posix:
            if mpi_module != self.mpi_module:
                sbatch_cmds.extend(["module purge", "module load {}".format(self.mpi_module)])
                sbatch_cmds.extend(stop_dfuse(dfuse))
        commands.append([sbatch_cmds, log_name])
        self.log.info("<<{} cmdlines>>:".format(job_spec.upper()))
        for cmd in sbatch_cmds:
            self.log.info("%s", cmd)
        if mpi_module != self.mpi_module:
            mpirun_cmd = Mpirun(app_cmd, False, self.mpi_module)
            mpirun_cmd.get_params(self)
    return commands


def build_job_script(self, commands, job, nodesperjob):
    """Create a slurm batch script that will execute a list of cmdlines.

    Args:
        self (obj): soak obj
        commands(list): command lines and cmd specific log_name
        job(str): the job name that will be defined in the slurm script

    Returns:
        script_list: list of slurm batch scripts

    """
    job_timeout = self.params.get("job_timeout", "/run/" + job + "/*", 10)
    self.log.info("<<Build Script>> at %s", time.ctime())
    script_list = []
    # if additional cmds are needed in the batch script
    prepend_cmds = ["set -e",
                    "echo Job_Start_Time `date \\+\"%Y-%m-%d %T\"`",
                    "daos pool query {} ".format(self.pool[1].uuid),
                    "daos pool query {} ".format(self.pool[0].uuid)]
    append_cmds = ["daos pool query {} ".format(self.pool[1].uuid),
                   "daos pool query {} ".format(self.pool[0].uuid),
                   "echo Job_End_Time `date \\+\"%Y-%m-%d %T\"`"]
    exit_cmd = ["exit $status"]
    # Create the sbatch script for each list of cmdlines
    for cmd, log_name in commands:
        if isinstance(cmd, str):
            cmd = [cmd]
        output = os.path.join(
            self.soaktest_dir, self.test_name + "_" + log_name + "_%N_" + "%j_")
        error = os.path.join(str(output) + "ERROR_")
        sbatch = {
            "time": str(job_timeout) + ":00",
            "exclude": str(self.slurm_exclude_nodes),
            "error": str(error),
            "export": "ALL",
            "exclusive": None
        }
        # include the cluster specific params
        sbatch.update(self.srun_params)
        unique = get_random_string(5, self.used)
        script = slurm_utils.write_slurm_script(
            self.soaktest_dir, job, output, nodesperjob,
            prepend_cmds + cmd + append_cmds + exit_cmd, unique, sbatch)
        script_list.append(script)
        self.used.append(unique)
    return script_list


class SoakTestError(Exception):
    """Soak exception class."""
