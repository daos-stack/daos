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
    # Enable forwarding of the ssh authentication agent connection
    task.set_info("ssh_options", "-oForwardAgent=yes")
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

    e.g. server-[26-27] becomes a list with entries server-26, server-27

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
