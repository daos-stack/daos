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
import random
import string
import time
from getpass import getuser
from importlib import import_module

from avocado.utils import process
from ClusterShell.Task import task_self
from ClusterShell.NodeSet import NodeSet, NodeSetParseError


class DaosTestError(Exception):
    """DAOS API exception class."""


class SimpleProfiler(object):
    """
    Simple profiler that counts the number of times a function is called
    and measure its execution time.
    """

    def __init__(self):
        self._stats = {}
        self._logger = None

    def clean(self):
        """
        Clean the metrics collect so far.
        """
        self._stats = {}

    def run(self, fn, tag, *args, **kwargs):
        """
        Run a function and update its stats.

        Parameters:
            fn (function): Function to be executed
            args  (tuple): Argument list
            kwargs (dict): Keyworded, variable-length argument list
        """
        self._log("Running function: {0}()".format(fn.__name__))

        start_time = time.time()

        ret = fn(*args, **kwargs)

        end_time = time.time()
        elapsed_time = end_time - start_time
        self._log(
            "Execution time: {0}".format(
                self._pretty_time(elapsed_time)))

        if tag not in self._stats:
            self._stats[tag] = [0, []]

        self._stats[tag][0] += 1
        self._stats[tag][1].append(elapsed_time)

        return ret

    def get_stat(self, tag):
        """
        Retrieves the stats of a function.

        Parameters:
            tag (str): Tag to be query

        Return:
            max, min, avg (tuple): A tuple with the slowest, fastest and
            average execution times.
        """
        data = self._stats.get(tag, [0, []])

        return self._calculate_metrics(data[1])

    def set_logger(self, fn):
        """
        Set the function that will be used to print the elapsed time on each
        function call. If this value is not set, the profiling will be
        performed silently.

        Parameters:
            fn (function): Function to be used for logging.
        """
        self._logger = fn

    def print_stats(self):
        """
        Prints all the stats collected so far. If the logger has not been set,
        the stats will be printed by using the built-in print function.
        """
        self._pmsg("{0:20} {1:5} {2:10} {3:10} {4:10}".format(
            "Function Tag", "Hits", "Max", "Min", "Average"))

        for fname, data in self._stats.items():
            max_time, min_time, avg_time = self._calculate_metrics(data[1])
            self._pmsg(
                "{0:20} {1:5} {2:10} {3:10} {4:10}".format(
                    fname,
                    data[0],
                    self._pretty_time(max_time),
                    self._pretty_time(min_time),
                    self._pretty_time(avg_time)))

    def _log(self, msg):
        """If logger function is set, print log messages"""
        if self._logger:
            self._logger(msg)

    def _pmsg(self, msg):
        """
        Print messages using the logger. If it has not been set, print
        messages using python print() function.
        """
        if self._logger:
            self._log(msg)
        else:
            print(msg)

    @classmethod
    def _pretty_time(cls, ftime):
        """Convert to pretty time string"""
        return time.strftime("%H:%M:%S", time.gmtime(ftime))

    @classmethod
    def _calculate_metrics(cls, data):
        """
        Calculate the maximum, minimum and average values of a given list.
        """
        max_time = max(data)
        min_time = min(data)

        if len(data):
            avg_time = sum(data) / len(data)
        else:
            avg_time = 0

        return max_time, min_time, avg_time


def human_to_bytes(size):
    """Convert a human readable size value to respective byte value.

    Args:
        size (str): human readable size value to be converted.

    Raises:
        DaosTestError: when an invalid human readable size value is provided

    Returns:
        int: value translated to bytes.

    """
    conversion_sizes = ("", "k", "m", "g", "t", "p", "e")
    conversion = {
        1000: ["{}b".format(item) for item in conversion_sizes],
        1024: ["{}ib".format(item) for item in conversion_sizes],
    }
    match = re.findall(r"([0-9.]+)\s*([a-zA-Z]+|)", size)
    try:
        multiplier = 1
        if match[0][1]:
            multiplier = -1
            unit = match[0][1].lower()
            for item in conversion:
                if unit in conversion[item]:
                    multiplier = item ** conversion[item].index(unit)
                    break
            if multiplier == -1:
                raise DaosTestError(
                    "Invalid unit detected, not in {}: {}".format(
                        conversion[1000] + conversion[1024][1:], unit))
        value = float(match[0][0]) * multiplier
    except IndexError:
        raise DaosTestError(
            "Invalid human readable size format: {}".format(size))
    return int(value) if value.is_integer() else value


def bytes_to_human(size, digits=2, binary=True):
    """Convert a byte value to the largest (> 1.0) human readable size.

    Args:
        size (int): byte size value to be converted.
        digits (int, optional): number of digits used to round the converted
            value. Defaults to 2.
        binary (bool, optional): convert to binary (True) or decimal (False)
            units. Defaults to True.

    Returns:
        str: value translated to a human readable size.

    """
    units = 1024 if binary else 1000
    conversion = ["B", "KB", "MB", "GB", "TB", "PB", "EB"]
    index = 0
    value = [size if isinstance(size, (int, float)) else 0, conversion.pop(0)]
    while value[0] > units and conversion:
        index += 1
        value[0] = float(size) / (units ** index)
        value[1] = conversion.pop(0)
        if units == 1024 and len(value[1]) > 1:
            value[1] = "{}i{}".format(*value[1])
    return "".join([str(round(value[0], digits)), value[1]])


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
        # Report if the command timed out or failed
        if error.result.interrupted:
            msg = "Timeout detected running '{}' with a {}s timeout".format(
                command, timeout)
        else:
            msg = "Error occurred running '{}': {}".format(
                command, error.result)

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
    data = {code: host_list for code, host_list in task.iter_retcodes()}

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
        for output, host_list in task.iter_buffers(data[0]):
            host_data[NodeSet.fromlist(host_list)] = str(output)

    return host_data


def pcmd(hosts, command, verbose=True, timeout=None, expect_rc=0):
    """Run a command on each host in parallel and get the return codes.

    Args:
        hosts (list): list of hosts
        command (str): the command to run in parallel
        verbose (bool, optional): display command output. Defaults to True.
        timeout (int, optional): command timeout in seconds. Defaults to None.
        expect_rc (int, optional): expected return code. Defaults to 0.

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


def process_host_list(hoststr):
    """
    Convert a slurm style host string into a list of individual hosts.

    e.g. server-[26-27] becomes a list with entries server-26, server-27

    This works for every thing that has come up so far but I don't know what
    all slurm finds acceptable so it might not parse everything possible.
    """
    # 1st split into cluster name and range of hosts
    split_loc = hoststr.index('-')
    cluster = hoststr[0:split_loc]
    num_range = hoststr[split_loc + 1:]

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
            for hostnum in range(int(host_range[0]), int(host_range[1]) + 1):
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
            "then pkill --signal ABRT {}".format(pattern),
            "sleep 1",
            "if pgrep --list-full {}".format(pattern),
            "then pkill --signal KILL {}".format(pattern),
            "fi",
            "fi",
            "fi",
            "exit $rc",
        ]
        result = pcmd(hosts, "; ".join(commands), verbose, timeout, None)
    return result


def get_partition_hosts(partition, reservation=None):
    """Get a list of hosts in the specified slurm partition and reservation.

    Args:
        partition (str): name of the partition
        reservation (str): name of reservation
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
                hosts = []
            if hosts and reservation is not None:
                # Get the list of hosts from the reservation information
                cmd = "scontrol show reservation {}".format(reservation)
                try:
                    result = process.run(cmd, timeout=10)
                except process.CmdError as error:
                    log.warning(
                        "Unable to obtain hosts from the %s slurm "
                        "reservation: %s", reservation, error)
                    result = None
                    hosts = []
                if result:
                    # Get the list of hosts from the reservation information
                    output = result.stdout
                    try:
                        reservation_hosts = list(
                            NodeSet(re.findall(r"\sNodes=(\S+)", output)[0]))
                    except (NodeSetParseError, IndexError):
                        log.warning(
                            "Unable to obtain hosts from the %s slurm "
                            "reservation output: %s", reservation, output)
                        reservation_hosts = []
                    is_subset = set(reservation_hosts).issubset(set(hosts))
                    if reservation_hosts and is_subset:
                        hosts = reservation_hosts
                    else:
                        hosts = []
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
    """Check for a correct UUID format.

    Args:
        uuid (str): Pool or Container UUID.

    Returns:
        bool: status of valid or invalid uuid

    """
    pattern = re.compile("([0-9a-fA-F-]+)")
    return bool(len(uuid) == 36 and pattern.match(uuid))


def get_numeric_list(numeric_range):
    """Convert a string of numeric ranges into an expanded list of integers.

    Example: "0-3,7,9-13,15" -> [0, 1, 2, 3, 7, 9, 10, 11, 12, 13, 15]

    Args:
        numeric_range (str): the string of numbers and/or ranges of numbers to
            convert

    Raises:
        AttributeError: if the syntax of the numeric_range argument is invalid

    Returns:
        list: an expanded list of integers

    """
    numeric_list = []
    try:
        for item in numeric_range.split(","):
            if "-" in item:
                range_args = [int(val) for val in item.split("-")]
                range_args[-1] += 1
                numeric_list.extend([int(val) for val in range(*range_args)])
            else:
                numeric_list.append(int(item))
    except (AttributeError, ValueError, TypeError):
        raise AttributeError(
            "Invalid 'numeric_range' argument - must be a string containing "
            "only numbers, dashes (-), and/or commas (,): {}".format(
                numeric_range))

    return numeric_list


def get_remote_file_size(host, file_name):
    """Obtain remote file size.

    Args:
        file_name (str): name of remote file

    Returns:
        int: file size

    """
    cmd = "ssh" " {}@{}" " stat -c%s {}".format(
        getuser(), host, file_name)
    result = run_command(cmd)

    return int(result.stdout)


def error_count(error, hostlist, log_file):
    """Count the number of specific ERRORs found in the log file.

    This function also returns a count of the other ERRORs from same log file.

    Args:
        error (str): DAOS error to look for in .log file. for example -1007
        hostlist (list): System list to looks for an error.
        log_file (str): Log file name (server/client log).

    Returns:
        tuple: a tuple of the count of errors matching the specified error type
            and the count of other errors (int, int)

    """
    # Get the Client side Error from client_log file.
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


def get_module_class(name, module):
    """Get the class object in the specified module by its name.

    Args:
        name (str): class name to obtain
        module (str): module name in which to find the class name

    Raises:
        DaosTestError: if the class name is not found in the module

    Returns:
        object: class matching the name in the specified module

    """
    try:
        name_module = import_module(module)
        name_class = getattr(name_module, name)
    except (ImportError, AttributeError) as error:
        raise DaosTestError(
            "Invalid '{}' class name for {}: {}".format(name, module, error))
    return name_class


def get_job_manager_class(name, job=None, subprocess=False, mpi="openmpi"):
    """Get the job manager class that matches the specified name.

    Enables assigning a JobManager class from the test's yaml settings.

    Args:
        name (str): JobManager-based class name
        job (ExecutableCommand, optional): command object to manage. Defaults
            to None.
        subprocess (bool, optional): whether the command is run as a
            subprocess. Defaults to False.
        mpi (str, optional): MPI type to use with the Mpirun class only.
            Defaults to "openmpi".

    Raises:
        DaosTestError: if an invalid JobManager class name is specified.

    Returns:
        JobManager: a JobManager class, e.g. Orterun, Mpirun, Srun, etc.

    """
    manager_class = get_module_class(name, "job_manager_utils")
    if name == "Mpirun":
        manager = manager_class(job, subprocess=subprocess, mpitype=mpi)
    else:
        manager = manager_class(job, subprocess=subprocess)
    return manager
