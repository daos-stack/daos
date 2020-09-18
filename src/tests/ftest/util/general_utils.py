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
from logging import getLogger

import os
import re
import json
import random
import string
from pathlib import Path
from errno import ENOENT
from getpass import getuser
from avocado.utils import process
from ClusterShell.Task import task_self
from ClusterShell.NodeSet import NodeSet, NodeSetParseError


class DaosTestError(Exception):
    """DAOS API exception class."""


def human_to_bytes(h_size):
    """Convert human readable size values to respective byte value.

    Args:
        h_size (str): human readable size value to be converted.

    Returns:
        int: value translated to bytes.

    """
    units = {"b": 1,
             "kb": (2**10),
             "k": (2**10),
             "mb": (2**20),
             "m": (2**20),
             "gb": (2**30),
             "g": (2**30)}
    pattern = r"([0-9.]+|[a-zA-Z]+)"
    val, unit = re.findall(pattern, h_size)

    # Check if float or int and then convert
    val = float(val) if "." in val else int(val)
    if unit.lower() in units:
        val = val * units[unit.lower()]
    else:
        print("Unit not found! Provide a valid unit i.e: b,k,kb,m,mb,g,gb")
        val = -1

    return val


def run_command(command, timeout=60, verbose=True, raise_exception=True,
                output_check="combined", env=None):
    """Run the command on the local host.

    This method uses the avocado.utils.process.run() method to run the specified
    command string on the local host using subprocess.Popen(). Even though the
    command is specified as a string, since shell=False is passed to process.run
    it will use shlex.spit() to break up the command into a list before it is
    passed to subprocess.Popen. The shell=False is forced for security. As a
    result typically any command containing ";", "|", "&&", etc. will fail.

    Args:
        command (str): command to run.
        timeout (int, optional): command timeout. Defaults to 60 seconds.
        verbose (bool, optional): whether to log the command run and
            stdout/stderr. Defaults to True.
        raise_exception (bool, optional): whether to raise an exception if the
            command returns a non-zero exit status. Defaults to True.
        output_check (str, optional): whether to record the output from the
            command (from stdout and stderr) in the test output record files.
            Valid values:
                "stdout"    - standard output *only*
                "stderr"    - standard error *only*
                "both"      - both standard output and error in separate files
                "combined"  - standard output and error in a single file
                "none"      - disable all recording
            Defaults to "combined".
        env (dict, optional): dictionary of environment variable names and
            values to set when running the command. Defaults to None.

    Raises:
        DaosTestError: if there is an error running the command

    Returns:
        CmdResult: an avocado.utils.process CmdResult object containing the
            result of the command execution.  A CmdResult object has the
            following properties:
                command         - command string
                exit_status     - exit_status of the command
                stdout          - the stdout
                stderr          - the stderr
                duration        - command execution time
                interrupted     - whether the command completed within timeout
                pid             - command's pid

    """
    msg = None
    kwargs = {
        "cmd": command,
        "timeout": timeout,
        "verbose": verbose,
        "ignore_status": not raise_exception,
        "allow_output_check": output_check,
        "shell": False,
        "env": env,
    }
    if verbose:
        print("Command environment vars:\n  {}".format(env))
    try:
        # Block until the command is complete or times out
        return process.run(**kwargs)

    except TypeError as error:
        # Can occur if using env with a non-string dictionary values
        msg = "Error running '{}': {}".format(command, error)
        if env is not None:
            msg = "\n".join([
                msg,
                "Verify env values are defined as strings: {}".format(env)])

    except process.CmdError as error:
        # Command failed or possibly timed out
        msg = "Error occurred running '{}': {}".format(command, error)

    if msg is not None:
        print(msg)
        raise DaosTestError(msg)


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


def get_host_data(hosts, command, text, error, timeout=None):
    """Get the data requested for each host using the specified command.

    Args:
        hosts (list): list of hosts
        command (str): command used to obtain the data on each server
        text (str): data identification string
        error (str): data error string

    Returns:
        dict: a dictionary of data values for each NodeSet key

    """
    # Find the data for each specified servers
    print("  Obtaining {} data on {}".format(text, hosts))
    task = run_task(hosts, command, timeout)
    host_data = {}
    DATA_ERROR = "[ERROR]"

    # Create a list of NodeSets with the same return code
    data = {code: hosts for code, hosts in task.iter_retcodes()}

    # Multiple return codes or a single non-zero return code
    # indicate at least one error obtaining the data
    if len(data) > 1 or 0 not in data:
        # Report the errors
        messages = []
        for code, host_list in data.items():
            if code != 0:
                output_data = list(task.iter_buffers(host_list))
                if not output_data:
                    messages.append(
                        "{}: rc={}, command=\"{}\"".format(
                            NodeSet.fromlist(host_list), code, command))
                else:
                    for output, o_hosts in output_data:
                        lines = str(output).splitlines()
                        info = "rc={}{}".format(
                            code,
                            ", {}".format(output) if len(lines) < 2 else
                            "\n  {}".format("\n  ".join(lines)))
                        messages.append(
                            "{}: {}".format(
                                NodeSet.fromlist(o_hosts), info))
        print("    {} on the following hosts:\n      {}".format(
            error, "\n      ".join(messages)))

        # Return an error data set for all of the hosts
        host_data = {NodeSet.fromlist(hosts): DATA_ERROR}

    else:
        for output, hosts in task.iter_buffers(data[0]):
            host_data[NodeSet.fromlist(hosts)] = str(output)

    return host_data


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

    # Report any errors
    retcode_dict = {}
    errors = False
    for retcode, rc_nodes in task.iter_retcodes():
        # Create a NodeSet for this list of nodes
        nodeset = NodeSet.fromlist(rc_nodes)

        # Include this NodeSet for this return code
        if retcode not in retcode_dict:
            retcode_dict[retcode] = NodeSet()
        retcode_dict[retcode].add(nodeset)

        # Keep track of any errors
        if expect_rc is not None and expect_rc != retcode:
            errors = True

    # Report command output if requested or errors are detected
    if verbose or errors:
        print("Command:\n  {}".format(command))
        print("Command return codes:")
        for retcode in sorted(retcode_dict):
            print("  {}: rc={}".format(retcode_dict[retcode], retcode))

        print("Command output:")
        for output, bf_nodes in task.iter_buffers():
            # Create a NodeSet for this list of nodes
            nodeset = NodeSet.fromlist(bf_nodes)

            # Display the output per node set
            print("  {}:\n    {}".format(
                nodeset, "\n    ".join(str(output).splitlines())))

    # Report any timeouts
    if timeout and task.num_timeout() > 0:
        nodes = task.iter_keys_timeout()
        print(
            "{}: timeout detected running '{}' on {}/{} hosts after {}s".format(
                NodeSet.fromlist(nodes),
                command, task.num_timeout(), len(hosts), timeout))
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


def convert_list(value, separator=","):
    """Convert a list into a separator-separated string of its items.

    Examples:
        convert_list([1,2,3])        -> '1,2,3'
        convert_list([1,2,3], " ")   -> '1 2 3'
        convert_list([1,2,3], ", ")  -> '1, 2, 3'

    Args:
        value (list): list to convert into a string
        separator (str, optional): list item separator. Defaults to ",".

    Returns:
        str: a single string containing all the list items separated by the
            separator.

    """
    return separator.join([str(item) for item in value])


def stop_processes(hosts, pattern, verbose=True, timeout=60):
    """Stop the processes on each hosts that match the pattern.

    Args:
        hosts (list): hosts on which to stop the processes
        pattern (str): regular expression used to find process names to stop
        verbose (bool, optional): display command output. Defaults to True.
        timeout (int, optional): command timeout in seconds. Defaults to 60
            seconds.

    Returns:
        dict: a dictionary of return codes keys and accompanying NodeSet
            values indicating which hosts yielded the return code.
            Return code keys:
                0   No processes matched the criteria / No processes killed.
                1   One or more processes matched the criteria and a kill was
                    attempted.

    """
    result = {}
    log = getLogger()
    log.info("Killing any processes on %s that match: %s", hosts, pattern)
    if hosts is not None:
        commands = [
            "rc=0",
            "if pgrep --list-full {}".format(pattern),
            "then rc=1",
            "sudo pkill {}".format(pattern),
            "sleep 5",
            "if pgrep --list-full {}".format(pattern),
            "then pkill --signal KILL {}".format(pattern),
            "fi",
            "fi",
            "exit $rc",
        ]
        result = pcmd(hosts, "; ".join(commands), verbose, timeout, None)
    return result


def get_partition_hosts(partition):
    """Get a list of hosts in the specified slurm partition.

    Args:
        partition (str): name of the partition

    Returns:
        list: list of hosts in the specified partition

    """
    log = getLogger()
    hosts = []
    if partition is not None:
        # Get the partition name information
        cmd = "scontrol show partition {}".format(partition)
        try:
            result = process.run(cmd, timeout=10)
        except process.CmdError as error:
            log.warning(
                "Unable to obtain hosts from the %s slurm "
                "partition: %s", partition, error)
            result = None

        if result:
            # Get the list of hosts from the partition information
            output = result.stdout
            try:
                hosts = list(NodeSet(re.findall(r"\s+Nodes=(.*)", output)[0]))
            except (NodeSetParseError, IndexError):
                log.warning(
                    "Unable to obtain hosts from the %s slurm partition "
                    "output: %s", partition, output)
    return hosts


def get_log_file(name):
    """Get the full log file name and path.

    Prepends the DAOS_TEST_LOG_DIR path (or /tmp) to the specified file name.

    Args:
        name (str): log file name

    Returns:
        str: full log file name including path

    """
    return os.path.join(os.environ.get("DAOS_TEST_LOG_DIR", "/tmp"), name)


def check_uuid_format(uuid):
    """Checks a correct UUID format.

    Args:
        uuid (str): Pool or Container UUID.

    Returns:
        bool: status of valid or invalid uuid
    """
    pattern = re.compile("([0-9a-fA-F-]+)")
    return bool(len(uuid) == 36 and pattern.match(uuid))


def get_remote_file_size(host, file_name):
    """Obtain remote file size.

      Args:
        file_name (str): name of remote file

      Returns:
        integer value of file size
    """

    cmd = "ssh" " {}@{}" " stat -c%s {}".format(
        getuser(), host, file_name)
    result = run_command(cmd)

    return int(result.stdout)

def error_count(error, hostlist, log_file):
    """
    Function to count any specific ERROR in client log. This function also
    return other ERROR count from same log file.

    Args:
        error (str): DAOS error to look for in .log file. for example -1007
        hostlist (list): System list to looks for an error.
        log_file (str): Log file name (server/client log).

    return:
        daos_error_count (int): requested error count
        other_error_count (int): Other error count

    """
    #Get the Client side Error from client_log file.
    output = []
    requested_error_count = 0
    other_error_count = 0
    cmd = 'cat {} | grep ERR'.format(get_log_file(log_file))
    task = run_task(hostlist, cmd)
    for buf, _nodes in task.iter_buffers():
        output = str(buf).split('\n')

    for line in output:
        if 'ERR' in line:
            if error in line:
                requested_error_count += 1
            else:
                other_error_count += 1

    return requested_error_count, other_error_count
