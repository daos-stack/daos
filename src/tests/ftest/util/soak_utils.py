"""
(C) Copyright 2019-2024 Intel Corporation.
(C) Copyright 2025 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines

import getpass
import os
import random
import re
import stat
import threading
import time
from itertools import count, product

from avocado.core.exceptions import TestFail
from avocado.utils.distro import detect
from ClusterShell.NodeSet import NodeSet
from command_utils import command_as_user
from command_utils_base import EnvironmentVariables
from daos_racer_utils import DaosRacerCommand
from data_mover_utils import DcpCommand, FsCopy
from dfuse_utils import get_dfuse
from dmg_utils import (check_system_query_status, get_storage_query_device_info,
                       get_storage_query_device_uuids)
from duns_utils import format_path
from exception_utils import CommandFailure
from fio_utils import FioCommand
from general_utils import (DaosTestError, check_ping, check_ssh, get_journalctl, get_log_file,
                           get_random_bytes, get_random_string, list_to_str, run_command,
                           wait_for_result)
from ior_utils import IorCommand
from job_manager_utils import Mpirun
from macsio_util import MacsioCommand
from mdtest_utils import MdtestCommand
from oclass_utils import extract_redundancy_factor
from pydaos.raw import DaosApiError, DaosSnapshot
from run_utils import run_local, run_remote
from test_utils_container import add_container

H_LOCK = threading.Lock()
id_counter = count(start=1)


def ddhhmmss_format(seconds):
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


def get_id():
    """Increment a counter to generate job ids

    Returns:
        int : next counter value
    """
    return next(id_counter)


def debug_logging(log, enable_debug_msg, log_msg):
    """Enable debug messages in log file.

    Args:
        log (logger): logger for the messages produced by this method
        enable_debug_msg (boolean): If true, the debug message will be written to log
        log_msg (str): debug message to write to log
    """
    if enable_debug_msg:
        log.debug(log_msg)


def add_pools(self, pool_names, ranks=None):
    """Create a list of pools that the various tests use for storage.

    Args:
        self (obj): soak obj
        pool_names (list): list of pool namespaces from yaml file
                    /run/<test_params>/poollist/*
        ranks (list, optional):  ranks to include in pool. Defaults to None
    """
    params = {}
    target_list = ranks if ranks else None
    for pool_name in pool_names:
        path = "".join(["/run/", pool_name, "/*"])
        properties = self.params.get('properties', path, None)
        # allow yaml pool property to override the scrub default; whether scrubber is enabled or not
        if self.enable_scrubber and "scrub" not in str(properties):
            scrubber_properties = "scrub:timed,scrub_freq:120"
            params['properties'] = (",").join(filter(None, [properties, scrubber_properties]))
        else:
            params['properties'] = properties
        # Create a pool and add it to the overall list of pools
        self.pool.append(self.get_pool(
            namespace=path,
            connect=False,
            target_list=target_list,
            dmg=self.dmg_command,
            **params))

        self.log.info("Valid Pool ID is %s", self.pool[-1].identifier)


def add_containers(self, pool, file_oclass=None, dir_oclass=None, path="/run/container/*"):
    """Create a list of containers that the various jobs use for storage.

    Args:
        pool (TestPool): pool to read/write random data file
        file_oclass (str): file oclass for daos container cmd
        dir oclass (str): directory oclass for daos container cmd
        path (str): namespace for container

    """
    kwargs = {}
    if file_oclass:
        kwargs['file_oclass'] = file_oclass
        properties = self.params.get('properties', path, None)
        redundancy_factor = extract_redundancy_factor(file_oclass)
        rd_fac = f'rd_fac:{str(redundancy_factor)}'
        kwargs['properties'] = (",").join(filter(None, [properties, rd_fac]))
    if dir_oclass:
        kwargs['dir_oclass'] = dir_oclass
    self.container.append(self.get_container(pool, path, **kwargs))


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
        with open(file, 'w', encoding="utf-8") as src_file:
            src_file.write(str(os.urandom(num_bytes)))
            src_file.close()
        dst_file = format_path(pool, container)
        fscopy_cmd.set_params(src=file, dst=dst_file)
        fscopy_cmd.run()
    # reads file_name from container and writes to file
    elif cmd == "read":
        dst = os.path.split(file)
        dst_name = dst[-1]
        dst_path = dst[0]
        src_file = format_path(pool, container, dst_name)
        fscopy_cmd.set_params(src=src_file, dst=dst_path)
        fscopy_cmd.run()


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
    with open(logfile, 'w', encoding="utf-8") as log_file:
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
    if events:
        for journalctl_type in ["kernel", "daos_server"]:
            for output in get_journalctl(self.hostlist_servers, since, until, journalctl_type):
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


def get_daos_server_logs(self):
    """Gather server logs.

    Args:
        self (obj): soak obj
    """
    daos_dir = os.path.join(self.outputsoak_dir, "daos_server_logs")
    logs_dir = os.path.join(self.test_env.log_dir, "*log*")
    hosts = self.hostlist_servers
    if not os.path.exists(daos_dir):
        os.mkdir(daos_dir)
        command = ["clush", "-w", str(hosts), "-v", "--rcopy", logs_dir, "--dest", daos_dir]
        try:
            run_command(" ".join(command), timeout=600)
        except DaosTestError as error:
            raise SoakTestError(f"<<FAILED: daos logs file from {hosts} not copied>>") from error


def get_job_logs(self):
    """Gather all job logs for the current pass of soak."""

    # gather all the logfiles for this pass and cleanup client nodes
    cmd = f"/usr/bin/rsync -avtr --min-size=1B {self.soak_log_dir} {self.outputsoak_dir}/"
    cmd2 = f"/usr/bin/rm -rf {self.soak_log_dir}"
    if self.enable_remote_logging:
        # Limit fan out to reduce burden on filesystem
        result = run_remote(self.log, self.hostlist_clients, cmd, timeout=600, fanout=64)
        if result.passed:
            result = run_remote(self.log, self.hostlist_clients, cmd2, timeout=600)
        if not result.passed:
            self.log.error("Remote copy failed on %s", str(result.failed_hosts))
        # copy script files from shared dir
        sharedscr_dir = self.sharedsoak_dir + "/pass" + str(self.loop)
        cmd3 = f"/usr/bin/rsync -avtr --min-size=1B {sharedscr_dir} {self.outputsoak_dir}/"
        cmd4 = f"/usr/bin/rm -rf {sharedscr_dir}"
        if not run_local(self.log, cmd3, timeout=600).passed:
            self.log.error("Script file copy failed with %s", cmd3)
        if not run_local(self.log, cmd4, timeout=600).passed:
            self.log.error("Script file copy failed with %s", cmd4)
    # copy the local files; local host not included in hostlist_client
    if not run_local(self.log, cmd, timeout=600).passed:
        self.log.error("Local copy failed: %s", cmd)
    if not run_local(self.log, cmd2, timeout=600).passed:
        self.log.error("Local copy failed: %s", cmd2)


def run_monitor_check(self):
    """Monitor server cpu, memory usage periodically.

    Args:
        self (obj): soak obj

    """
    monitor_cmds = self.params.get("monitor", "/run/*") or []
    for cmd in monitor_cmds:
        run_remote(self.log, self.hostlist_servers, cmd, timeout=30)


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
            name = "pass" + str(self.loop) + f"_metrics_{engine}.csv"
            if prefix:
                name = prefix + f"_metrics_{engine}.csv"
            destination = self.outputsoak_dir
            daos_metrics = f"{self.sudo_cmd} daos_metrics -S {engine} --csv"
            result = run_remote(
                self.log, self.hostlist_servers, daos_metrics, verbose=(not logging), timeout=60)
            if logging:
                for data in result.output:
                    log_name = name + "-" + str(data.hosts)
                    self.log.info("Logging %s output to %s", daos_metrics, log_name)
                    write_logfile(data.stdout, log_name, destination)


def get_harassers(harasser):
    """Create a valid harasser list from the yaml job harassers.

    Args:
        harasser (str): harasser job from yaml.

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
    self.log.info("<<Wait for %s rebuild on %s>> at %s", name, pool.identifier, time.ctime())
    if self.enable_rebuild_logmasks:
        self.dmg_command.server_set_logmasks("DEBUG", raise_exception=False)
    try:
        # # Wait for rebuild to start
        # pool.wait_for_rebuild_to_start()
        # Wait for rebuild to complete
        pool.wait_for_rebuild_to_end()
        rebuild_status = True
    except DaosTestError as error:
        self.log.error(f"<<<FAILED: {name} rebuild timed out: {error}", exc_info=error)
        rebuild_status = False
    except TestFail as error1:
        self.log.error(
            f"<<<FAILED: {name} rebuild failed due to test issue: {error1}", exc_info=error1)
    finally:
        if self.enable_rebuild_logmasks:
            self.dmg_command.server_set_logmasks(raise_exception=False)
    return rebuild_status


def job_cleanup(log, hosts):
    """Cleanup after job is done.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (list): list of node to pass to job script
    """
    current_user = getpass.getuser()
    for job in ["mpirun", "palsd", "dfuse"]:
        cmd = [f"/usr/bin/bash -c 'for pid in $(pgrep -u {current_user} {job})",
               "do kill -HUP $pid",
               "done'"]
        run_remote(
            log, hosts, ";".join(cmd), verbose=False, timeout=600, task_debug=False, stderr=False)
        if job == "dfuse":
            cmd2 = [
                "/usr/bin/bash -c 'for dir in $(find /tmp/soak_dfuse_*/)",
                "do fusermount3 -uz $dir",
                "rm -rf $dir",
                "done'"]
            run_remote(log, hosts, ";".join(cmd2), verbose=False, timeout=600, task_debug=False,
                       stderr=False)


def launch_jobscript(
        log, job_queue, job_id, host_list, env, script, job_log, error_log, timeout, test):
    """Launch the job script on remote node.

    Args:
        log (logger): logger for the messages produced by this method
        job_queue (Queue): job queue to post status of job
        job_id (int): unique job identifier
        host_list (list): list of node to pass to job script
        env (str): environment variables for job script
        script (str): full path to job script
        job_log (str): job std out
        error_log (str): job std error
        timeout (int): job timeout
        test (TestObj): soak test obj
    """

    debug_logging(log, test.enable_debug_msg, f"DBG: JOB {job_id} ENTERED launch_jobscript")
    job_results = []
    node_results = []
    down_nodes = NodeSet()
    state = "UNKNOWN"
    if time.time() >= test.end_time:
        results = {"handle": job_id, "state": "CANCELLED", "host_list": host_list}
        debug_logging(log, test.enable_debug_msg, f"DBG: JOB {job_id} EXITED launch_jobscript")
        job_queue.put(results)
        return
    if isinstance(host_list, str):
        # assume one host in list
        hosts = host_list
        rhost = host_list
    else:
        hosts = ",".join(sorted(host_list))
        rhost = NodeSet(hosts)[0]
    job_log1 = job_log.replace("JOBID", str(job_id))
    error_log1 = error_log.replace("JOBID", str(job_id))
    joblog = job_log1.replace("RHOST", str(rhost))
    errorlog = error_log1.replace("RHOST", str(rhost))
    cmd = ";".join([env, f"{script} {hosts} {job_id} {joblog} {errorlog}"])
    job_results = run_remote(
        log, rhost, cmd, verbose=False, timeout=timeout * 60, task_debug=False, stderr=False)
    if job_results:
        if job_results.timeout:
            state = "TIMEOUT"
        elif job_results.passed:
            state = "COMPLETED"
        elif not job_results.passed:
            state = "FAILED"
        else:
            state = "UNKNOWN"
    # attempt to cleanup any leftover job processes on timeout
    job_cleanup(log, hosts)
    if time.time() >= test.end_time:
        results = {"handle": job_id, "state": "CANCELLED", "host_list": host_list}
        debug_logging(log, test.enable_debug_msg, f"DBG: JOB {job_id} EXITED launch_jobscript")
        job_queue.put(results)
        # give time to update the queue before exiting
        time.sleep(0.5)
        return

    # check if all nodes are available
    cmd = f"ls {test.test_env.log_dir}"
    node_results = run_remote(log, NodeSet(hosts), cmd, verbose=False)
    if node_results.failed_hosts:
        for node in node_results.failed_hosts:
            host_list.remove(node)
            down_nodes.update(node)
            log.info(f"DBG: Node {node} is marked as DOWN in job {job_id}")

    log.info("FINAL STATE: soak job %s completed with : %s at %s", job_id, state, time.ctime())
    results = {"handle": job_id, "state": state, "host_list": host_list, "down_nodes": down_nodes}
    debug_logging(log, test.enable_debug_msg, f"DBG: JOB {job_id} EXITED launch_jobscript")
    job_queue.put(results)
    # give time to update the queue before exiting
    time.sleep(0.5)
    return


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
    container = add_container(self, pool, namespace="/run/container_reserved/*")
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
    self.log.info("<<<PASS %s: %s completed at %s>>>\n", self.loop, name, time.ctime())


def launch_vmd_identify_check(self, name, results, args):
    """Run dmg cmds to blink/check VMD leds.

    Args:
        self (obj): soak obj
        name (str): name of dmg subcommand
        results (queue): multiprocessing queue
        args (queue): multiprocessing queue
    """
    # pylint: disable=too-many-nested-blocks
    status = True
    failing_vmd = []
    dmg = self.get_dmg_command().copy()
    device_info = get_storage_query_device_info(self.dmg_command)
    uuid_list = [device['uuid'] for device in device_info]
    # limit the number of leds to blink to 1024
    if len(uuid_list) > 1024:
        uuids = random.sample(uuid_list, 1024)
    else:
        uuids = uuid_list
    self.log.info("VMD device UUIDs: %s", uuids)
    host_uuids = get_storage_query_device_uuids(self.dmg_command)
    for host, uuid_dict in host_uuids.items():
        uuid_list = sorted(uuid_dict.keys())
        self.log.info("Devices on host %s: %s", host, uuid_list)
        # Now check whether the random uuid belongs to a particular host.
        for uuid in uuids:
            if uuid in uuid_list:
                dmg.hostlist = host
                # Blink led
                dmg.storage_led_identify(ids=uuid, timeout=2)
                # check if led is blinking
                result = dmg.storage_led_check(ids=uuid)
                # determine if leds are blinking as expected
                for value in list(result['response']['host_storage_map'].values()):
                    if value['storage']['smd_info']['devices']:
                        for device in value['storage']['smd_info']['devices']:
                            if device['ctrlr']['led_state'] != "QUICK_BLINK":
                                failing_vmd.append([device['ctrlr']['pci_addr'], value['hosts']])
                                status = False
                            # reset leds to previous state
                            dmg.storage_led_identify(ids=uuid, reset=True)

    params = {"name": name,
              "status": status,
              "vars": {"failing_vmd_devices": failing_vmd}}
    self.harasser_job_done(params)
    results.put(self.harasser_results)
    args.put(self.harasser_args)
    self.log.info("Harasser results: %s", self.harasser_results)
    self.log.info("Harasser args: %s", self.harasser_args)
    self.log.info("<<<PASS %s: %s completed at %s>>>\n", self.loop, name, time.ctime())


def launch_reboot(self, pools, name, results, args):
    """Execute server unexpected reboot.

    Args:
        self (obj): soak obj
        pools (TestPool): list of TestPool obj
        name (str): name of dmg subcommand
        results (queue): multiprocessing queue
        args (queue): multiprocessing queue
    """
    # Harasser is run in two parts REBOOT and then REBOOT_REINTEGRATE
    # REBOOT test steps
    # shutdown random node
    # wait for node to reboot
    # If node rebooted ok wait for rebuild on both pool to complete
    # Update multiprocessing queue with results and args
    # REBOOT_REINTEGRATE test steps
    # if REBOOT completed ok then
    # Issue systemctl restart daos_server
    # Verify that all ranks are joined
    # If all ranks "joined", issue reintegrate for all pool on all ranks and wait for
    #    rebuild to complete
    # Update multiprocessing queue with results and args
    # pylint: disable=too-many-nested-blocks,too-many-branches
    status = False
    params = {}
    ranks = None
    reboot_host = None
    ranklist = None
    if name == "REBOOT":
        reboot_host = self.random.choice(self.hostlist_servers)
        ranklist = self.server_managers[0].get_host_ranks(reboot_host)
        ranks = ",".join(str(rank) for rank in ranklist)
        # init the status dictionary
        params = {"name": name,
                  "status": status,
                  "vars": {"host": reboot_host, "ranks": ranklist}}
        self.log.info(
            "<<<PASS %s: %s started on ranks %s at %s >>>\n", self.loop, name, ranks, time.ctime())
        # reboot host in 1 min
        result = run_remote(self.log, reboot_host, command_as_user("shutdown -r +1", "root"))
        if result.passed:
            status = True
        else:
            self.log.error(f"<<<FAILED: {name} - {reboot_host} failed to issue reboot")
            status = False

        if not wait_for_result(self.log, check_ping, 90, 5, True, host=reboot_host,
                               expected_ping=False, cmd_timeout=60, verbose=True):
            self.log.error(f"<<<FAILED: {name} - {reboot_host} failed to reboot")
            status = False

        if status:
            rebuild_status = True
            for pool in pools:
                rebuild_status &= wait_for_pool_rebuild(self, pool, name)
            status = rebuild_status

    elif name == "REBOOT_REINTEGRATE":
        if self.harasser_results["REBOOT"]:
            reboot_host = self.harasser_args["REBOOT"]["host"]
            ranklist = self.harasser_args["REBOOT"]["ranks"]
            ranks = ",".join(str(rank) for rank in ranklist)
            self.log.info("<<<PASS %s: %s started on host %s at %s>>>\n", self.loop, name,
                          reboot_host, time.ctime())
            status = True
            self.dmg_command.system_query()
            # wait for node to complete rebooting
            if not wait_for_result(self.log, check_ping, 60, 5, True, host=reboot_host,
                                   expected_ping=True, cmd_timeout=60, verbose=True):
                self.log.error(f"<<<FAILED: {name} - {reboot_host} failed to reboot")
                status = False
            if not wait_for_result(self.log, check_ssh, 120, 2, True, hosts=reboot_host,
                                   cmd_timeout=30, verbose=True):
                self.log.error(f"<<<FAILED: {name} - {reboot_host} failed to reboot")
                status = False
            if status:
                # issue a restart
                self.log.info("<<<PASS %s: Issue systemctl restart daos_server on %s at %s>>>\n",
                              self.loop, name, reboot_host, time.ctime())
                cmd_results = run_remote(
                    self.log, reboot_host, command_as_user("systemctl restart daos_server", "root"))
                if cmd_results.passed:
                    self.dmg_command.system_query()
                    for pool in pools:
                        pool.query()
                    # wait server to be started
                    try:
                        self.dmg_command.system_start(ranks=ranks)
                    except CommandFailure as error:
                        self.log.error("<<<FAILED:dmg system start failed", exc_info=error)
                        status = False
                    if status:
                        # Check the servers are in joined state.
                        all_joined = False
                        retry = 0
                        while not all_joined and retry < 10:
                            all_joined = check_system_query_status(
                                self.get_dmg_command().system_query())
                            retry += 1
                            time.sleep(10)
                        if not all_joined:
                            self.log.error("<<<FAILED: One or more servers failed to join")
                            status = False
                    for pool in pools:
                        pool.query()
                    self.dmg_command.system_query()
                else:
                    self.log.error("<<<FAILED: systemctl start daos_server failed")
                    status = False
            if status:
                # reintegrate ranks
                reintegrate_status = True
                for pool in pools:
                    for rank in ranklist:
                        try:
                            pool.reintegrate(rank)
                            status = True
                        except TestFail as error:
                            self.log.error(
                                f"<<<FAILED: dmg pool {pool.identifier} reintegrate failed on rank"
                                "{rank}", exc_info=error)
                            status = False
                        reintegrate_status &= status
                        if reintegrate_status:
                            reintegrate_status &= wait_for_pool_rebuild(self, pool, name)
                            status = reintegrate_status
                        else:
                            status = False
                        self.dmg_command.system_query()
        else:
            self.log.error("<<<PASS %s: %s failed due to REBOOT failure >>>", self.loop, name)
            status = False

    params = {"name": name,
              "status": status,
              "vars": {"host": reboot_host, "ranks": ranklist}}
    if not status:
        self.log.error("<<< %s failed - check logs for failure data>>>", name)
    self.dmg_command.system_query()
    self.harasser_job_done(params)
    results.put(self.harasser_results)
    args.put(self.harasser_args)
    self.log.info("Harasser results: %s", self.harasser_results)
    self.log.info("Harasser args: %s", self.harasser_args)
    self.log.info("<<<PASS %s: %s completed at %s>>>\n", self.loop, name, time.ctime())


def launch_extend(self, pool, name, results, args):
    """Execute dmg extend ranks.

    Args:
        self (obj): soak obj
        pool (TestPool): TestPool obj
        name (str): name of dmg subcommand
        results (queue): multiprocessing queue
        args (queue): multiprocessing queue
    """
    status = False
    params = {}
    ranks = None

    if self.selected_host:
        ranklist = self.server_managers[0].get_host_ranks(self.selected_host)
        ranks = ",".join(str(rank) for rank in ranklist)
        # init the status dictionary
        params = {"name": name,
                  "status": status,
                  "vars": {"host": self.selected_host, "ranks": ranks}}
        self.log.info(
            "<<<PASS %s: %s started on ranks %s at %s >>>\n", self.loop, name, ranks, time.ctime())
        try:
            pool.extend(ranks)
            status = True
        except TestFail as error:
            self.log.error("<<<FAILED:dmg pool extend failed", exc_info=error)
            status = False
        if status:
            status = wait_for_pool_rebuild(self, pool, name)

    params = {"name": name,
              "status": status,
              "vars": {"host": self.selected_host, "ranks": ranks}}
    if not status:
        self.log.error("<<< %s failed - check logs for failure data>>>", name)
    self.dmg_command.system_query()
    self.harasser_job_done(params)
    results.put(self.harasser_results)
    args.put(self.harasser_args)
    self.log.info("Harasser results: %s", self.harasser_results)
    self.log.info("Harasser args: %s", self.harasser_args)
    self.log.info("<<<PASS %s: %s completed at %s>>>\n", self.loop, name, time.ctime())


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
        exclude_servers = (len(self.hostlist_servers) * int(engine_count)) - 1
        # Exclude one rank.
        rank = random.randint(0, exclude_servers)  # nosec

        if targets >= 8:
            tgt_idx = None
        else:
            target_list = random.sample(range(0, 8), targets)
            tgt_idx = ','.join(str(tgt) for tgt in target_list)

        # init the status dictionary
        params = {"name": name,
                  "status": status,
                  "vars": {"rank": rank, "tgt_idx": tgt_idx}}
        self.log.info(
            "<<<PASS %s: %s started on rank %s at %s >>>\n", self.loop, name, rank, time.ctime())
        try:
            pool.exclude(rank, tgt_idx=tgt_idx)
            status = True
        except TestFail as error:
            self.log.error("<<<FAILED:dmg pool exclude failed", exc_info=error)
            status = False
        if status:
            status = wait_for_pool_rebuild(self, pool, name)
    elif name == "REINTEGRATE":
        if self.harasser_results["EXCLUDE"]:
            rank = self.harasser_args["EXCLUDE"]["rank"]
            tgt_idx = self.harasser_args["EXCLUDE"]["tgt_idx"]
            self.log.info(
                "<<<PASS %s: %s started on rank %s at %s>>>\n", self.loop, name, rank, time.ctime())
            try:
                pool.reintegrate(rank, tgt_idx=tgt_idx)
                status = True
            except TestFail as error:
                self.log.error("<<<FAILED:dmg pool reintegrate failed", exc_info=error)
                status = False
            if status:
                status = wait_for_pool_rebuild(self, pool, name)
        else:
            self.log.error("<<<PASS %s: %s failed due to EXCLUDE failure >>>", self.loop, name)
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
    self.log.info("<<<PASS %s: %s completed at %s>>>\n", self.loop, name, time.ctime())


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
                    self.dmg_command.server_set_logmasks("DEBUG", raise_exception=False)
                    pool.drain(rank)
                    self.dmg_command.server_set_logmasks(raise_exception=False)
                except TestFail as error:
                    self.log.error(
                        f"<<<FAILED:dmg pool {pool.identifier} drain failed", exc_info=error)
                    status = False
                drain_status &= status
                if drain_status:
                    drain_status &= wait_for_pool_rebuild(self, pool, "DRAIN")
                    status = drain_status
                else:
                    status = False
        if status:
            # Shutdown the server
            try:
                self.dmg_command.system_stop(force=True, ranks=rank)
            except TestFail as error:
                self.log.error("<<<FAILED:dmg system stop failed", exc_info=error)
                status = False
            time.sleep(30)
            if not drain and status:
                rebuild_status = True
                for pool in pools:
                    rebuild_status &= wait_for_pool_rebuild(self, pool, name)
                status = rebuild_status
    elif name == "SVR_START":
        if self.harasser_results["SVR_STOP"]:
            rank = self.harasser_args["SVR_STOP"]["rank"]
            self.log.info(
                "<<<PASS %s: %s started on rank %s at %s>>>\n", self.loop, name, rank, time.ctime())
            try:
                self.dmg_command.system_start(ranks=rank)
                status = True
            except TestFail as error:
                self.log.error("<<<FAILED:dmg system start failed", exc_info=error)
                status = False
        else:
            self.log.error("<<<PASS %s: %s failed due to SVR_STOP failure >>>", self.loop, name)
            status = False
    elif name == "SVR_REINTEGRATE":
        if self.harasser_results["SVR_STOP"]:
            rank = self.harasser_args["SVR_STOP"]["rank"]
            self.log.info(
                "<<<PASS %s: %s started on rank %s at %s>>>\n", self.loop, name, rank, time.ctime())
            try:
                self.dmg_command.system_start(ranks=rank)
                status = True
            except TestFail as error:
                self.log.error("<<<FAILED:dmg system start failed", exc_info=error)
                status = False
            for pool in pools:
                self.dmg_command.pool_query(pool.identifier)
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
                        self.log.error(f"<<<FAILED:dmg pool {pool.identifier} reintegrate failed",
                                       exc_info=error)
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


def start_dfuse(self, pool, container, name=None, job_spec=None):
    """Create dfuse start command line for slurm.

    Args:
        self (obj): soak obj
        pool (obj): TestPool obj

    Returns dfuse(obj): Dfuse obj
            cmd(list): list of dfuse commands to add to job script
    """
    # Get Dfuse params
    namespace = os.path.join(os.sep, "run", job_spec, "dfuse", "*")
    dfuse = get_dfuse(self, self.hostlist_clients, namespace)
    dfuse.bind_cores = self.params.get("cores", dfuse.namespace, None)
    # update dfuse params; mountpoint for each container
    unique = get_random_string(5, self.used)
    self.used.append(unique)
    mount_dir = dfuse.mount_dir.value + unique
    dfuse.update_params(mount_dir=mount_dir, pool=pool.identifier, cont=container.identifier)
    dfuselog = os.path.join(
        self.soak_log_dir,
        self.test_name + "_" + name + "_`hostname -s`_"
        "" + "${JOB_ID}_" + "daos_dfuse.log")
    dfuse_env = ";".join(
        ["export D_LOG_FILE_APPEND_PID=1",
         "export D_LOG_MASK=ERR",
         f"export D_LOG_FILE={dfuselog}"])
    module_load = ";".join([f"module use {self.mpi_module_use}", f"module load {self.mpi_module}"])

    dfuse_start_cmds = [
        "clush -S -w $HOSTLIST \"mkdir -p {}\"".format(dfuse.mount_dir.value),
        "clush -S -w $HOSTLIST \"cd {};{};{};{}\"".format(
            dfuse.mount_dir.value, dfuse_env, module_load, str(dfuse)),
        "sleep 10",
        "clush -S -w $HOSTLIST \"df -h {}\"".format(dfuse.mount_dir.value),
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
        f'clush -S -w $HOSTLIST "fusermount3 -uz {dfuse.mount_dir.value}"',
        f'clush -S -w $HOSTLIST "rm -rf {dfuse.mount_dir.value}"'])
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
        "/usr/bin/bash -c 'for dir in $(find /tmp/soak_dfuse_*/)",
        "do fusermount3 -uz $dir",
        "rm -rf $dir",
        "done'"]
    result = run_remote(self.log, self.hostlist_clients, ";".join(cmd), timeout=600)
    if not result.passed:
        self.log.info("Dfuse processes not stopped Error")
    result = run_remote(self.log, self.hostlist_clients, ";".join(cmd2), timeout=600)
    if not result.passed:
        self.log.info("Dfuse mount points not deleted Error")


def create_ior_cmdline(self, job_spec, pool, ppn, nodesperjob, oclass_list=None, cont=None):
    """Create an IOR cmdline to run in slurm batch.

    Args:

        self (obj): soak obj
        job_spec (str):   ior job in yaml to run
        pool (obj):       TestPool obj
        ppn(int):         number of tasks to run on each node
        nodesperjob(int): number of nodes per job
        oclass(list):     list of file_oclass and dir_oclass params
        cont (obj)        TestContainer obj

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
    if not oclass_list:
        oclass_list = self.params.get("dfs_oclass", ior_params)
    plugin_path = self.params.get("plugin_path", "/run/hdf5_vol/")
    # update IOR cmdline for each additional IOR obj
    for api in api_list:
        if not self.enable_il and api in ["POSIX-LIBIOIL", "POSIX-LIBPIL4DFS"]:
            continue
        if api in ["HDF5-VOL", "HDF5", "POSIX"] and ppn > 16:
            continue
        for b_size, t_size, file_dir_oclass in product(bsize_list,
                                                       tsize_list,
                                                       oclass_list):
            ior_cmd = IorCommand(self.test_env.log_dir)
            ior_cmd.namespace = ior_params
            ior_cmd.get_params(self)
            ior_cmd.max_duration.update(ior_timeout)
            if api == "HDF5-VOL":
                ior_cmd.api.update("HDF5")
            elif api in ["POSIX", "POSIX-LIBPIL4DFS", "POSIX-LIBIOIL"]:
                ior_cmd.api.update("POSIX")
            else:
                ior_cmd.api.update(api)
            ior_cmd.block_size.update(b_size)
            ior_cmd.transfer_size.update(t_size)
            if api in ["HDF5-VOL", "POSIX", "POSIX-LIBPIL4DFS", "POSIX-LIBIOIL"]:
                ior_cmd.dfs_oclass.update(None)
                ior_cmd.dfs_dir_oclass.update(None)
            else:
                ior_cmd.dfs_oclass.update(file_dir_oclass[0])
                ior_cmd.dfs_dir_oclass.update(file_dir_oclass[1])
            if ior_cmd.api.value == "DFS":
                ior_cmd.test_file.update(os.path.join("/", "testfile"))
            if not cont:
                add_containers(self, pool, file_dir_oclass[0], file_dir_oclass[1])
                container = self.container[-1]
            else:
                container = cont
            ior_cmd.set_daos_params(pool, container.identifier)
            log_name = "{}_{}_{}_{}_{}_{}_{}_{}".format(
                job_spec.replace("/", "_"), api, b_size, t_size,
                file_dir_oclass[0], nodesperjob * ppn, nodesperjob, ppn)
            daos_log = os.path.join(
                self.soak_log_dir, self.test_name + "_" + log_name
                + "_`hostname -s`_${JOB_ID}_daos.log")
            env = ior_cmd.get_default_env("mpirun", log_file=daos_log)
            env["D_LOG_FILE_APPEND_PID"] = "1"
            sbatch_cmds = [f"module use {self.mpi_module_use}", f"module load {self.mpi_module}"]
            # include dfuse cmdlines
            if api in ["HDF5-VOL", "POSIX", "POSIX-LIBPIL4DFS", "POSIX-LIBIOIL"]:
                dfuse, dfuse_start_cmdlist = start_dfuse(
                    self, pool, container, name=log_name, job_spec=job_spec)
                sbatch_cmds.extend(dfuse_start_cmdlist)
                ior_cmd.test_file.update(
                    os.path.join(dfuse.mount_dir.value, "testfile"))
            mpirun_cmd = Mpirun(ior_cmd, mpi_type=self.mpi_module)
            mpirun_cmd.get_params(self)
            if api == "POSIX-LIBPIL4DFS":
                env["LD_PRELOAD"] = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')
            if api == "POSIX-LIBIOIL":
                env["LD_PRELOAD"] = os.path.join(self.prefix, 'lib64', 'libioil.so')
            # add envs if api is HDF5-VOL
            if api == "HDF5-VOL":
                vol = True
                cont_props = container.properties.value
                env["HDF5_DAOS_FILE_PROP"] = '"' + cont_props.replace(",", ";") + '"'
                env["HDF5_DAOS_OBJ_CLASS"] = file_dir_oclass[0]
                env["HDF5_VOL_CONNECTOR"] = "daos"
                env["HDF5_PLUGIN_PATH"] = str(plugin_path)
            mpirun_cmd.assign_processes(nodesperjob * ppn)
            mpirun_cmd.assign_environment(env, True)
            mpirun_cmd.ppn.update(ppn)
            mpirun_cmd.hostlist.update("$HOSTLIST")
            sbatch_cmds.append(str(mpirun_cmd))
            sbatch_cmds.append("status=$?")
            if api in ["HDF5-VOL", "POSIX", "POSIX-LIBPIL4DFS", "POSIX-LIBIOIL"]:
                sbatch_cmds.extend(stop_dfuse(dfuse, vol))
            commands.append([sbatch_cmds, log_name])
            self.log.info(f"<<IOR {api} cmdlines>>: ")
            for cmd in sbatch_cmds:
                self.log.info(cmd)
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
        for file_oclass, dir_oclass in oclass_list:
            add_containers(self, pool, file_oclass, dir_oclass)
            macsio = MacsioCommand()
            macsio.namespace = macsio_params
            macsio.daos_pool = pool.identifier
            macsio.daos_svcl = list_to_str(pool.svc_ranks)
            macsio.daos_cont = self.container[-1].identifier
            macsio.get_params(self)
            log_name = "{}_{}_{}_{}_{}_{}".format(
                job_spec, api, file_oclass, nodesperjob * ppn, nodesperjob, ppn)
            daos_log = os.path.join(
                self.soak_log_dir, self.test_name
                + "_" + log_name + "_`hostname -s`_${JOB_ID}_daos.log")
            macsio_log = os.path.join(
                self.soak_log_dir, self.test_name
                + "_" + log_name + "_`hostname -s`_${JOB_ID}_macsio-log.log")
            macsio_timing_log = os.path.join(
                self.soak_log_dir, self.test_name
                + "_" + log_name + "_`hostname -s`_${JOB_ID}_macsio-timing.log")
            macsio.log_file_name.update(macsio_log)
            macsio.timings_file_name.update(macsio_timing_log)
            env = macsio.env.copy()
            env["D_LOG_FILE"] = get_log_file(daos_log or f"{macsio.command}_daos.log")
            env["D_LOG_FILE_APPEND_PID"] = "1"
            env["DAOS_UNS_PREFIX"] = format_path(macsio.daos_pool, macsio.daos_cont)
            sbatch_cmds = [f"module use {self.mpi_module_use}", f"module load {self.mpi_module}"]
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
                env["HDF5_PLUGIN_PATH"] = str(plugin_path)
                mpirun_cmd.working_dir.update(dfuse.mount_dir.value)
            mpirun_cmd.assign_environment(env, True)
            mpirun_cmd.ppn.update(ppn)
            mpirun_cmd.hostlist.update("$HOSTLIST")
            sbatch_cmds.append(str(mpirun_cmd))
            sbatch_cmds.append("status=$?")
            if api in ["HDF5-VOL"]:
                sbatch_cmds.extend(stop_dfuse(dfuse, vol=True))
            commands.append([sbatch_cmds, log_name])
            self.log.info("<<MACSio cmdlines>>: ")
            for cmd in sbatch_cmds:
                self.log.info(cmd)
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
        if api in ["POSIX", "POSIX-LIBPIL4DFS", "POSIX-LIBIOIL"] and ppn > 16:
            continue
        if not self.enable_il and api in ["POSIX-LIBIOIL", "POSIX-LIBPIL4DFS"]:
            continue
        for write_bytes, read_bytes, depth, file_dir_oclass in product(write_bytes_list,
                                                                       read_bytes_list,
                                                                       depth_list,
                                                                       oclass_list):
            # Get the parameters for Mdtest
            mdtest_cmd = MdtestCommand(self.test_env.log_dir)
            mdtest_cmd.namespace = mdtest_params
            mdtest_cmd.get_params(self)
            if api in ["POSIX", "POSIX-LIBPIL4DFS", "POSIX-LIBIOIL"]:
                mdtest_cmd.api.update("POSIX")
            else:
                mdtest_cmd.api.update(api)
            mdtest_cmd.write_bytes.update(write_bytes)
            mdtest_cmd.read_bytes.update(read_bytes)
            mdtest_cmd.depth.update(depth)
            mdtest_cmd.flags.update(flag)
            mdtest_cmd.num_of_files_dirs.update(num_of_files_dirs)
            mdtest_cmd.dfs_oclass.update(file_dir_oclass[0])
            mdtest_cmd.dfs_dir_oclass.update(file_dir_oclass[1])
            add_containers(self, pool, file_dir_oclass[0], file_dir_oclass[1])
            mdtest_cmd.update_params(
                dfs_pool=pool.identifier, dfs_cont=self.container[-1].identifier)
            log_name = "{}_{}_{}_{}_{}_{}_{}_{}_{}".format(
                job_spec, api, write_bytes, read_bytes, depth,
                file_dir_oclass[0], nodesperjob * ppn, nodesperjob,
                ppn)
            daos_log = os.path.join(
                self.soak_log_dir, self.test_name + "_" + log_name
                + "_`hostname -s`_${JOB_ID}_daos.log")
            env = mdtest_cmd.get_default_env("mpirun", log_file=daos_log)
            env["D_LOG_FILE_APPEND_PID"] = "1"
            sbatch_cmds = [f"module use {self.mpi_module_use}", f"module load {self.mpi_module}"]
            # include dfuse cmdlines
            if api in ["POSIX", "POSIX-LIBPIL4DFS", "POSIX-LIBIOIL"]:
                dfuse, dfuse_start_cmdlist = start_dfuse(
                    self, pool, self.container[-1], name=log_name, job_spec=job_spec)
                sbatch_cmds.extend(dfuse_start_cmdlist)
                mdtest_cmd.test_dir.update(dfuse.mount_dir.value)
                if self.enable_il and api == "POSIX-LIBPIL4DFS":
                    env["LD_PRELOAD"] = os.path.join(
                        self.prefix, 'lib64', 'libpil4dfs.so')
                if self.enable_il and api == "POSIX-LIBIOIL":
                    env["LD_PRELOAD"] = os.path.join(self.prefix, 'lib64', 'libioil.so')
            mpirun_cmd = Mpirun(mdtest_cmd, mpi_type=self.mpi_module)
            mpirun_cmd.get_params(self)
            mpirun_cmd.assign_processes(nodesperjob * ppn)
            mpirun_cmd.assign_environment(env, True)
            mpirun_cmd.ppn.update(ppn)
            mpirun_cmd.hostlist.update("$HOSTLIST")
            sbatch_cmds.append(str(mpirun_cmd))
            sbatch_cmds.append("status=$?")
            if api in ["POSIX", "POSIX-LIBPIL4DFS", "POSIX-LIBIOIL"]:
                sbatch_cmds.extend(stop_dfuse(dfuse))
            commands.append([sbatch_cmds, log_name])
            self.log.info(f"<<MDTEST {api} cmdlines>>: ")
            for cmd in sbatch_cmds:
                self.log.info(cmd)
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
        self.soak_log_dir,
        self.test_name + "_" + job_spec + "_`hostname -s`_"
        "${JOB_ID}_" + "racer_log")
    daos_racer.env["D_LOG_FILE"] = get_log_file(racer_log)
    log_name = job_spec
    cmds = []
    cmds.append(str(daos_racer.with_exports))
    cmds.append("status=$?")
    # add exit code
    commands.append([cmds, log_name])
    self.log.info("<<DAOS racer cmdlines>>: ")
    for cmd in cmds:
        self.log.info(cmd)
    return commands


def create_fio_cmdline(self, job_spec, pool):
    """Create the FOI command line for job script.

    Args:

        self (obj): soak obj
        job_spec (str): fio job in yaml to run
        pool (obj):   TestPool obj

    Returns:
        cmd(list): list of cmdlines

    """
    # pylint: disable=too-many-nested-blocks

    commands = []
    fio_namespace = os.path.join(os.sep, "run", job_spec, "*")
    fio_soak_namespace = os.path.join(os.sep, "run", job_spec, "soak", "*")
    # test params
    bs_list = self.params.get("blocksize", fio_soak_namespace)
    size_list = self.params.get("size", fio_soak_namespace)
    rw_list = self.params.get("rw", fio_soak_namespace)
    oclass_list = self.params.get("oclass", fio_soak_namespace)
    api_list = self.params.get("api", fio_namespace, default=["POSIX"])
    # Get the parameters for Fio
    fio_cmd = FioCommand()
    fio_cmd.namespace = fio_namespace
    fio_cmd.get_params(self)
    fio_cmd.aux_path.update(self.test_dir, "aux_path")
    for blocksize, size, rw_val, file_dir_oclass, api in product(bs_list,
                                                                 size_list,
                                                                 rw_list,
                                                                 oclass_list,
                                                                 api_list):
        if not self.enable_il and api in ["POSIX-LIBIOIL", "POSIX-LIBPIL4DFS"]:
            continue
        # update fio params
        fio_cmd.update(
            "global", "blocksize", blocksize,
            "fio --name=global --blocksize")
        fio_cmd.update(
            "global", "size", size,
            "fio --name=global --size")
        fio_cmd.update(
            "global", "rw", rw_val,
            "fio --name=global --rw")
        if api == "POSIX-LIBPIL4DFS":
            fio_cmd.update(
                "global", "ioengine", "sync",
                "fio --name=global --ioengine")
        else:
            fio_cmd.update(
                "global", "ioengine", "libaio",
                "fio --name=global --ioengine")
        cmds = []
        # add start dfuse cmds; api is always POSIX
        fio_cmd.api.update("POSIX")
        # Connect to the pool, create container and then start dfuse
        add_containers(self, pool, file_dir_oclass[0], file_dir_oclass[1])
        log_name = "{}_{}_{}_{}_{}_{}".format(
            job_spec, api, blocksize, size, rw_val, file_dir_oclass[0])

        dfuse, cmds = start_dfuse(
            self, pool, self.container[-1], name=log_name, job_spec=job_spec)
        # Update the FIO cmdline
        fio_cmd.update(
            "global", "directory",
            dfuse.mount_dir.value,
            "fio --name=global --directory")
        # add fio cmdline
        cmds.append(f"cd {dfuse.mount_dir.value};")
        if self.enable_il and api == "POSIX-LIBPIL4DFS":
            cmds.append(f"export LD_PRELOAD={os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')}")
        if self.enable_il and api == "POSIX-LIBIOIL":
            cmds.append(f"export LD_PRELOAD={os.path.join(self.prefix, 'lib64', 'libioil.so')}")
        cmds.append(str(fio_cmd))
        cmds.append("status=$?")
        cmds.append("cd -")
        cmds.extend(stop_dfuse(dfuse))
        commands.append([cmds, log_name])
        self.log.info("<<Fio cmdlines>>: ")
        for cmd in cmds:
            self.log.info(cmd)
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
    app_params = os.path.join(os.sep, "run", job_spec, "*")
    mpi_module = self.params.get("module", app_params, self.mpi_module)
    mpi_module_use = self.params.get("module_use", app_params, self.mpi_module_use)
    api_list = self.params.get("api", app_params, default=["DFS"])
    apps_dir = os.environ["DAOS_TEST_APP_DIR"]
    # Update DAOS_TEST_APP_DIR if used in the cmdline param in yaml
    # ${DAOS_TEST_APP_SRC}                  =>  apps built with el8 and mpi/mpich (default)
    # pylint: disable-next=wrong-spelling-in-comment,fixme
    # ${DAOS_TEST_APP_SRC}/intelmpi         =>  apps built with el8 and intelmpi
    # ${DAOS_TEST_APP_SRC}/suse             =>  apps built with suse and gnu-mpich
    # pylint: disable-next=wrong-spelling-in-comment,fixme
    # ${DAOS_TEST_APP_SRC}/suse/intelmpi    =>  apps built with suse and intelmpi
    if "suse" in detect().name.lower() and os.environ.get("DAOS_TEST_MODE") is None:
        os.environ["DAOS_TEST_APP_DIR"] += os.path.join(os.sep, "suse")
    if "mpi/latest" in mpi_module and os.environ.get("DAOS_TEST_MODE") is None:
        os.environ["DAOS_TEST_APP_DIR"] += os.path.join(os.sep, "intelmpi")
        os.environ["I_MPI_OFI_LIBRARY_INTERNAL"] = "0"
    app_cmd = os.path.expandvars(self.params.get("cmdline", app_params, default=None))
    if app_cmd is None:
        self.log.info(f"<<{job_spec} command line not specified in yaml>>")
        return commands
    oclass_list = self.params.get("oclass", app_params)
    for file_oclass, dir_oclass in oclass_list:
        for api in api_list:
            sbatch_cmds = [f"module use {mpi_module_use}", f"module load {mpi_module}"]
            if not self.enable_il and api in ["POSIX-LIBIOIL", "POSIX-LIBPIL4DFS"]:
                continue
            add_containers(self, pool, file_oclass, dir_oclass)
            log_name = "{}_{}_{}_{}_{}_{}".format(
                job_spec, api, file_oclass, nodesperjob * ppn, nodesperjob, ppn)
            # include dfuse cmdlines
            if api in ["POSIX", "POSIX-LIBIOIL", "POSIX-LIBPIL4DFS"]:
                dfuse, dfuse_start_cmdlist = start_dfuse(
                    self, pool, self.container[-1], name=log_name, job_spec=job_spec)
                sbatch_cmds.extend(dfuse_start_cmdlist)
            mpirun_cmd = Mpirun(app_cmd, False, mpi_module)
            mpirun_cmd.get_params(self)
            env = EnvironmentVariables()
            env["D_LOG_FILE_APPEND_PID"] = "1"
            if "mpich" in mpi_module:
                # Pass pool and container information to the commands
                env["DAOS_UNS_PREFIX"] = format_path(pool, self.container[-1])
            if self.enable_il and api == "POSIX-LIBPIL4DFS":
                env["LD_PRELOAD"] = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')
            if self.enable_il and api == "POSIX-LIBIOIL":
                env["LD_PRELOAD"] = os.path.join(self.prefix, 'lib64', 'libioil.so')
            mpirun_cmd.assign_environment(env, True)
            mpirun_cmd.assign_processes(nodesperjob * ppn)
            mpirun_cmd.ppn.update(ppn)
            mpirun_cmd.hostlist.update("$HOSTLIST")
            if api in ["POSIX", "POSIX-LIBIOIL", "POSIX-LIBPIL4DFS"]:
                mpirun_cmd.working_dir.update(dfuse.mount_dir.value)
            cmdline = str(mpirun_cmd)
            sbatch_cmds.append(str(cmdline))
            sbatch_cmds.append("status=$?")
            if api in ["POSIX", "POSIX-LIBIOIL", "POSIX-LIBPIL4DFS"]:
                sbatch_cmds.extend(stop_dfuse(dfuse))
            commands.append([sbatch_cmds, log_name])
            self.log.info(f"<<{job_spec.upper()} cmdlines>>: ")
            for cmd in sbatch_cmds:
                self.log.info("%s", cmd)
    if mpi_module != self.mpi_module:
        mpirun_cmd = Mpirun(app_cmd, False, self.mpi_module)
        mpirun_cmd.get_params(self)
    os.environ["DAOS_TEST_APP_DIR"] = apps_dir
    return commands


def create_dm_cmdline(self, job_spec, pool, ppn, nodesperjob):
    """Create datamover cmdlines for job script.

    Args:
        self (obj): soak obj
        job_spec (str):   datamover job in yaml to run
        pool (obj):       TestPool obj
        ppn(int):         number of tasks to run on each node
        nodesperjob(int): number of nodes per job
    """
    commands = []
    dm_params = os.path.join(os.sep, "run", job_spec, "*")
    oclass_list = self.params.get("oclass", dm_params)
    for file_oclass, dir_oclass in oclass_list:
        log_name = f"{job_spec}_{file_oclass}_{nodesperjob * ppn}_{nodesperjob}_{ppn}"
        ior_spec = "/".join([job_spec, "ior_write"])
        add_containers(self, pool, file_oclass, dir_oclass)
        cont_1 = self.container[-1]
        dm_commands = create_ior_cmdline(
            self, ior_spec, pool, ppn, nodesperjob, [[file_oclass, dir_oclass]], cont_1)
        sbatch_cmds = dm_commands[0][0]
        add_containers(self, pool, file_oclass, dir_oclass)
        cont_2 = self.container[-1]

        dcp_cmd = DcpCommand(hosts=None, tmp=None)
        dcp_cmd.namespace = os.path.join(os.sep, "run", job_spec, "dcp")
        dcp_cmd.get_params(self)
        dst_file = format_path(pool, cont_2)
        src_file = format_path(pool, cont_1)
        dcp_cmd.set_params(src=src_file, dst=dst_file)
        env_vars = {
            "D_LOG_FILE": os.path.join(self.soak_log_dir, self.test_name + "_"
                                       + log_name + "_`hostname -s`_${JOB_ID}_daos.log"),
            "D_LOG_FILE_APPEND_PID": "1"
        }
        mpirun_cmd = Mpirun(dcp_cmd, mpi_type=self.mpi_module)
        mpirun_cmd.get_params(self)
        mpirun_cmd.assign_processes(nodesperjob * ppn)
        mpirun_cmd.assign_environment(EnvironmentVariables(env_vars), True)
        mpirun_cmd.ppn.update(ppn)
        mpirun_cmd.hostlist.update("$HOSTLIST")
        sbatch_cmds.append(str(mpirun_cmd))
        sbatch_cmds.append("status=$?")

        ior_spec = "/".join([job_spec, "ior_read"])
        dm_commands = create_ior_cmdline(
            self, ior_spec, pool, ppn, nodesperjob, [[file_oclass, dir_oclass]], cont_2)
        sbatch_cmds.extend(dm_commands[0][0])
        self.log.info("<<DATA_MOVER cmdlines>>: ")
        for cmd in sbatch_cmds:
            self.log.info("%s", cmd)
        commands.append([sbatch_cmds, log_name])
    return commands


def build_job_script(self, commands, job, nodesperjob, ppn):
    """Generate a script that will execute a list of commands.

    Args:
        path (str): where to write the script file
        name (str): job name
        output (str): where to put the output (full path)
        nodecount (int): number of compute nodes to execute on
        cmds (list): shell commands that are to be executed
        uniq (str): a unique string to append to the job and log files
        sbatch_params (dict, optional): dictionary containing other less often used parameters to
                sbatch, e.g. mem:100. Defaults to None.

    Raises:
        SoakTestError: if missing require parameters for the job script

    Returns:
        str: the full path of the script

    """
    self.log.info("<<Create Job Script>> at %s", time.ctime())
    script_list = []
    # Additional commands needed in the job script
    prepend_cmds = ["set +e",
                    "echo Job_Start_Time `date \\+\"%Y-%m-%d %T\"`",
                    "daos pool query {} ".format(self.pool[1].identifier),
                    "daos pool query {} ".format(self.pool[0].identifier)]

    append_cmds = ["daos pool query {} ".format(self.pool[1].identifier),
                   "daos pool query {} ".format(self.pool[0].identifier),
                   "echo Job_End_Time `date \\+\"%Y-%m-%d %T\"`"]
    exit_cmd = ["exit $status"]

    for cmd, log_name in commands:
        unique = get_random_string(5, self.used)
        self.used.append(unique)
        if isinstance(cmd, str):
            cmd = [cmd]
        if self.job_scheduler == "slurm":
            job_timeout = self.params.get("job_timeout", "/run/" + job + "/*", 10)
            job_log = os.path.join(
                self.soak_log_dir, self.test_name + "_" + log_name + "_%N_" + "%j_")
            output = job_log + unique
            error = job_log + "ERROR_" + unique
            sbatch_params = {
                "time": str(job_timeout) + ":00",
                "exclude": str(self.slurm_exclude_nodes),
                "error": str(error),
                "export": "ALL",
                "exclusive": None,
                "ntasks": str(nodesperjob * ppn)
            }
            # include the cluster specific params
            sbatch_params.update(self.srun_params)
        else:
            job_log = os.path.join(
                self.soak_log_dir, self.test_name + "_" + log_name + "_RHOST" + "_JOBID_")
            output = job_log + unique
            error = job_log + "ERROR_" + unique

        job_cmds = prepend_cmds + cmd + append_cmds + exit_cmd
        # Write script file to shared dir
        sharedscript_dir = self.sharedsoak_dir + "/pass" + str(self.loop)
        scriptfile = sharedscript_dir + '/jobscript' + "_" + str(unique) + ".sh"
        with open(scriptfile, 'w') as script_file:
            script_file.write("#!/bin/bash\n#\n")
            if self.job_scheduler == "slurm":
                # write the slurm directives in the job script
                script_file.write("#SBATCH --job-name={}\n".format(job))
                script_file.write("#SBATCH --nodes={}\n".format(nodesperjob))
                script_file.write("#SBATCH --distribution=cyclic\n")
                script_file.write("#SBATCH --output={}\n".format(output))
                if sbatch_params:
                    for key, value in list(sbatch_params.items()):
                        if value is not None:
                            script_file.write("#SBATCH --{}={}\n".format(key, value))
                        else:
                            script_file.write("#SBATCH --{}\n".format(key))
                script_file.write("\n")
                script_file.write("if [ -z \"$VIRTUAL_ENV\" ]; then \n")
                script_file.write("    echo \"VIRTUAL_ENV not defined\" \n")
                script_file.write("else \n")
                script_file.write("    source $VIRTUAL_ENV/bin/activate \n")
                script_file.write("fi \n")
                script_file.write("HOSTLIST=`nodeset -e -S \",\" $SLURM_JOB_NODELIST` \n")
                script_file.write("JOB_ID=$SLURM_JOB_ID \n")
                script_file.write("echo \"SLURM NODES: $SLURM_JOB_NODELIST \" \n")
                script_file.write("echo \"NODE COUNT: $SLURM_JOB_NUM_NODES \" \n")
                script_file.write("echo \"JOB ID: $JOB_ID \" \n")
                script_file.write("echo \"HOSTLIST: $HOSTLIST \" \n")
                script_file.write("\n")
            else:
                script_file.write("HOSTLIST=$1 \n")
                script_file.write("JOB_ID=$2 \n")
                script_file.write("JOB_LOG=$3 \n")
                script_file.write("JOB_ERROR_LOG=$4 \n")
                script_file.write("echo \"JOB NODES: $HOSTLIST \" \n")
                script_file.write("echo \"JOB ID: $JOB_ID \" \n")
                script_file.write("if [ -z \"$VIRTUAL_ENV\" ]; then \n")
                script_file.write("    echo \"VIRTUAL_ENV not defined\" \n")
                script_file.write("else \n")
                script_file.write("    source $VIRTUAL_ENV/bin/activate \n")
                script_file.write("fi \n")
                script_file.write("exec 1> $JOB_LOG \n")
                script_file.write("exec 2> $JOB_ERROR_LOG \n")

            for cmd in list(job_cmds):
                script_file.write(cmd + "\n")
        os.chmod(scriptfile, stat.S_IXUSR | stat.S_IRUSR)
        script_list.append([scriptfile, output, error])
    return script_list


class SoakTestError(Exception):
    """Soak exception class."""
