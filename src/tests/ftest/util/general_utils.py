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
import re
import json
import random
import string
from pathlib import Path
from errno import ENOENT
from time import sleep

from avocado import fail_on
from pydaos.raw import DaosApiError, DaosServer, DaosContainer, DaosPool
from ClusterShell.Task import task_self
from ClusterShell.NodeSet import NodeSet


class DaosTestError(Exception):
    """DAOS API exception class."""


def run_task(hosts, command, timeout=None):
    """Create a task to run a command on each host in parallel.

    Args:
        hosts (list): list of hosts
        command (str): the command to run in parallel
        timeout (int, optional): command timeout in seconds. Defaults to None.

    Returns:
        Task: a ClusterShell.Task.Task object for the executed command

    """
    task = task_self()
    kwargs = {"command": command, "nodes": NodeSet.fromlist(hosts)}
    if timeout is not None:
        kwargs["timeout"] = timeout
    task.run(**kwargs)
    return task


def pcmd(hosts, command, verbose=True, timeout=None, expect_rc=0):
    """Run a command on each host in parallel and get the return codes.

    Args:
        hosts (list): list of hosts
        command (str): the command to run in parallel
        verbose (bool, optional): display command output. Defaults to True.
        timeout (int, optional): command timeout in seconds. Defaults to None.
        expect_rc (int, optional): exepcted return code. Defaults to 0.

    Returns:
        dict: a dictionary of return codes keys and accompanying NodeSet
            values indicating which hosts yielded the return code.

    """
    # Run the command on each host in parallel
    task = run_task(hosts, command, timeout)

    # Report any errors / display output if requested
    retcode_dict = {}
    for retcode, rc_nodes in task.iter_retcodes():
        # Create a NodeSet for this list of nodes
        nodeset = NodeSet.fromlist(rc_nodes)

        # Include this NodeSet for this return code
        if retcode not in retcode_dict:
            retcode_dict[retcode] = NodeSet()
        retcode_dict[retcode].add(nodeset)

        # Display any errors or requested output
        if retcode != expect_rc or verbose:
            msg = "failure running" if retcode != expect_rc else "output from"
            if len(list(task.iter_buffers(rc_nodes))) == 0:
                print(
                    "{}: {} '{}': rc={}".format(
                        nodeset, msg, command, retcode))
            else:
                for output, nodes in task.iter_buffers(rc_nodes):
                    nodeset = NodeSet.fromlist(nodes)
                    lines = str(output).splitlines()
                    output = "rc={}{}".format(
                        retcode,
                        ", {}".format(output) if len(lines) < 2 else
                        "\n  {}".format("\n  ".join(lines)))
                    print(
                        "{}: {} '{}': {}".format(
                            NodeSet.fromlist(nodes), msg, command, output))

    # Report any timeouts
    if timeout and task.num_timeout() > 0:
        nodes = task.iter_keys_timeout()
        print(
            "{}: timeout detected running '{}' on {}/{} hosts".format(
                NodeSet.fromlist(nodes),
                command, task.num_timeout(), len(hosts)))
        retcode = 255
        if retcode not in retcode_dict:
            retcode_dict[retcode] = NodeSet()
        retcode_dict[retcode].add(NodeSet.fromlist(nodes))

    return retcode_dict


def check_file_exists(hosts, filename, user=None, directory=False):
    """Check if a specified file exist on each specified hosts.

    If specified, verify that the file exists and is owned by the user.

    Args:
        hosts (list): list of hosts
        filename (str): file to check for the existence of on each host
        user (str, optional): owner of the file. Defaults to None.

    Returns:
        (bool, NodeSet): A tuple of:
            - True if the file exists on each of the hosts; False otherwise
            - A NodeSet of hosts on which the file does not exist

    """
    missing_file = NodeSet()
    command = "test -e {0}".format(filename)
    if user is not None and not directory:
        command = "test -O {0}".format(filename)
    elif user is not None and directory:
        command = "test -O {0} && test -d {0}".format(filename)
    elif directory:
        command = "test -d '{0}'".format(filename)

    task = run_task(hosts, command)
    for ret_code, node_list in task.iter_retcodes():
        if ret_code != 0:
            missing_file.add(NodeSet.fromlist(node_list))

    return len(missing_file) == 0, missing_file


def get_file_path(bin_name, dir_path=""):
    """
    Find the binary path name in daos_m and return the list of path.

    args:
        bin_name: bin file to be.
        dir_path: Directory location on top of daos_m to find the
                  bin.
    return:
        list: list of the paths for bin_name file
    Raises:
        OSError: If failed to find the bin_name file
    """
    with open('../../.build_vars.json') as json_file:
        build_paths = json.load(json_file)
    basepath = os.path.normpath(build_paths['PREFIX'] + "/../{0}"
                                .format(dir_path))

    file_path = list(Path(basepath).glob('**/{0}'.format(bin_name)))
    if not file_path:
        raise OSError(ENOENT, "File {0} not found inside {1} Directory"
                      .format(bin_name, basepath))
    else:
        return file_path


def process_host_list(hoststr):
    """
    Convert a slurm style host string into a list of individual hosts.

    e.g. boro-[26-27] becomes a list with entries boro-26, boro-27

    This works for every thing that has come up so far but I don't know what
    all slurmfinds acceptable so it might not parse everything possible.
    """
    # 1st split into cluster name and range of hosts
    split_loc = hoststr.index('-')
    cluster = hoststr[0:split_loc]
    num_range = hoststr[split_loc+1:]

    # if its just a single host then nothing to do
    if num_range.isdigit():
        return [hoststr]

    # more than 1 host, remove the brackets
    host_list = []
    num_range = re.sub(r'\[|\]', '', num_range)

    # differentiate between ranges and single numbers
    hosts_and_ranges = num_range.split(',')
    for item in hosts_and_ranges:
        if item.isdigit():
            hostname = cluster + '-' + item
            host_list.append(hostname)
        else:
            # split the two ends of the range
            host_range = item.split('-')
            for hostnum in range(int(host_range[0]), int(host_range[1])+1):
                hostname = "{}-{}".format(cluster, hostnum)
                host_list.append(hostname)

    return host_list


def get_random_string(length, exclude=None):
    """Create a specified length string of random ascii letters and numbers.

    Optionally exclude specific random strings from being returned.

    Args:
        length (int): length of the string to return
        exclude (list|None): list of strings to not return

    Returns:
        str: a string of random ascii letters and numbers

    """
    exclude = exclude if isinstance(exclude, list) else []
    random_string = None
    while not isinstance(random_string, str) or random_string in exclude:
        random_string = "".join(
            random.choice(string.ascii_uppercase + string.digits)
            for _ in range(length))
    return random_string


@fail_on(DaosApiError)
def get_pool(context, mode, size, name, svcn=1, log=None, connect=True):
    """Return a DAOS pool that has been created an connected.

    Args:
        context (DaosContext): the context to use to create the pool
        mode (int): the pool mode
        size (int): the size of the pool
        name (str): the name of the pool
        svcn (int): the pool service leader quantity
        log (DaosLog, optional): object for logging messages. Defaults to None.
        connect (bool, optional): connect to the new pool. Defaults to True.

    Returns:
        DaosPool: an object representing a DAOS pool

    """
    if log:
        log.info("Creating a pool")
    pool = DaosPool(context)
    pool.create(mode, os.geteuid(), os.getegid(), size, name, svcn=svcn)
    if connect:
        if log:
            log.info("Connecting to the pool")
        pool.connect(1 << 1)
    return pool


@fail_on(DaosApiError)
def get_container(context, pool, log=None):
    """Retrun a DAOS a container that has been created an opened.

    Args:
        context (DaosContext): the context to use to create the container
        pool (DaosPool): pool in which to create the container
        log (DaosLog|None): object for logging messages

    Returns:
        DaosContainer: an object representing a DAOS container

    """
    if log:
        log.info("Creating a container")
    container = DaosContainer(context)
    container.create(pool.handle)
    if log:
        log.info("Opening a container")
    container.open()
    return container


@fail_on(DaosApiError)
def kill_server(server_group, context, rank, pool, log=None):
    """Kill a specific server rank.

    Args:
        server_group (str): daos server group name
        context (DaosContext): the context to use to create the DaosServer
        rank (int): daos server rank to kill
        pool (DaosPool): the DaosPool from which to exclude the rank
        log (DaosLog|None): object for logging messages

    Returns:
        None

    """
    if log:
        log.info("Killing DAOS server {} (rank {})".format(server_group, rank))
    server = DaosServer(context, server_group, rank)
    server.kill(1)
    if log:
        log.info("Excluding server rank {}".format(rank))
    pool.exclude([rank])


@fail_on(DaosApiError)
def get_pool_status(pool, log):
    """Determine if the pool rebuild is complete.

    Args:
        pool (DaosPool): pool for which to determine if rebuild is complete
        log (logging): logging object used to report the pool status

    Returns:
        PoolInfo: the PoolInfo object returned by the pool's pool_query()
            function

    """
    pool_info = pool.pool_query()
    message = "Pool: pi_ntargets={}".format(pool_info.pi_ntargets)
    message += ", pi_nnodes={}".format(
        pool_info.pi_nnodes)
    message += ", pi_ndisabled={}".format(
        pool_info.pi_ndisabled)
    message += ", rs_version={}".format(
        pool_info.pi_rebuild_st.rs_version)
    message += ", rs_done={}".format(
        pool_info.pi_rebuild_st.rs_done)
    message += ", rs_toberb_obj_nr={}".format(
        pool_info.pi_rebuild_st.rs_toberb_obj_nr)
    message += ", rs_obj_nr={}".format(
        pool_info.pi_rebuild_st.rs_obj_nr)
    message += ", rs_rec_nr={}".format(
        pool_info.pi_rebuild_st.rs_rec_nr)
    message += ", rs_errno={}".format(
        pool_info.pi_rebuild_st.rs_errno)
    log.info(message)
    return pool_info


def is_pool_rebuild_complete(pool, log):
    """Determine if the pool rebuild is complete.

    Args:
        pool (DaosPool): pool for which to determine if rebuild is complete
        log (logging): logging object used to report the pool status

    Returns:
        bool: pool rebuild completion status

    """
    get_pool_status(pool, log)
    return pool.pool_info.pi_rebuild_st.rs_done == 1


def wait_for_rebuild(pool, log, to_start, interval):
    """Wait for the rebuild to start or end.

    Args:
        pool (DaosPool): pool for which to determine if rebuild is complete
        log (logging): logging object used to report the pool status
        to_start (bool): whether to wait for rebuild to start or end
        interval (int): number of seconds to wait in between rebuild
            completion checks

    Returns:
        None

    """
    log.info(
        "Waiting for rebuild to %s ...",
        "start" if to_start else "complete")
    while is_pool_rebuild_complete(pool, log) == to_start:
        log.info(
            "  Rebuild %s ...",
            "has not yet started" if to_start else "in progress")
        sleep(interval)


def verify_rebuild(pool, log, to_be_rebuilt, object_qty, record_qty, errors=0):
    """Confirm the number of rebuilt objects reported by the pool query.

    Args:
        pool (DaosPool): pool for which to determine if rebuild is complete
        log (logging): logging object used to report the pool status
        to_be_rebuilt (int): expected number of objects to be rebuilt
        object_qty (int): expected number of rebuilt records
        record_qty (int): expected total number of rebuilt records
        errors (int): expected number of rebuild errors

    Returns:
        list: a list of error messages for each expected value that did not
            match the actual value.  If all expected values were detected the
            list will be empty.

    """
    messages = []
    expected_pool_info = {
        "rs_toberb_obj_nr": to_be_rebuilt,
        "rs_obj_nr": object_qty,
        "rs_rec_nr": record_qty,
        "rs_errno": errors
    }
    log.info("Verifying the number of rebuilt objects and status")
    pool_info = get_pool_status(pool, log)
    for key, expected in expected_pool_info.items():
        detected = getattr(pool_info.pi_rebuild_st, key)
        if detected != expected:
            messages.append(
                "Unexpected {} value: expected={}, detected={}".format(
                    key, expected, detected))
    return messages


def check_pool_files(log, hosts, uuid):
    """Check if pool files exist on the specified list of hosts.

    Args:
        log (logging): logging object used to display messages
        hosts (list): list of hosts
        uuid (str): uuid file name to look for in /mnt/daos.

    Returns:
        bool: True if the files for this pool exist on each host; False
            otherwise

    """
    status = True
    log.info("Checking for pool data on %s", NodeSet.fromlist(hosts))
    pool_files = [uuid, "superblock"]
    for filename in ["/mnt/daos/{}".format(item) for item in pool_files]:
        result = check_file_exists(hosts, filename)
        if not result[0]:
            log.error("%s: %s not found", result[1], filename)
            status = False
    return status
