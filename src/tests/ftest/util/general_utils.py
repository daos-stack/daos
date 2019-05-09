#!/usr/bin/python
'''
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
'''
from daos_api import DaosApiError, DaosServer, DaosContainer, DaosPool

import os
import re
import json
import random
import string
from pathlib import Path
from errno import ENOENT
from avocado import fail_on
from time import sleep


class DaosTestError(Exception):
    """DAOS API exception class."""


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
    with open('../../../.build_vars.json') as json_file:
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
def get_pool(context, mode, size, name, svcn=1, log=None):
    """Return a DAOS pool that has been created an connected.

    Args:
        context (DaosContext): the context to use to create the pool
        mode (int): the pool mode
        size (int): the size of the pool
        name (str): the name of the pool
        svcn (int): the number of pool replica leaders
        log (DaosLog|None): object for logging messages

    Returns:
        DaosPool: an object representing a DAOS pool

    """
    if log:
        log.info("Creating a pool")
    pool = DaosPool(context)
    pool.create(mode, os.geteuid(), os.getegid(), size, name, svcn=svcn)
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
        server_group (str):
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
        None

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
