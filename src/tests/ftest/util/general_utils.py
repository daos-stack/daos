#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines

from logging import getLogger
import grp
import os
import pwd
import re
import random
import string
import time
import ctypes
from getpass import getuser
from importlib import import_module
from socket import gethostname

from avocado.core.settings import settings
from avocado.core.version import MAJOR
from avocado.utils import process
from ClusterShell.Task import task_self
from ClusterShell.NodeSet import NodeSet, NodeSetParseError


class DaosTestError(Exception):
    """DAOS API exception class."""


class SimpleProfiler():
    """Simple profiler class.
    Counts the number of times a function is called and measure its execution
    time.
    """

    def __init__(self):
        """Initialize a SimpleProfiler object."""
        self._stats = {}
        self._logger = getLogger()

    def clean(self):
        """Clean the metrics collect so far."""
        self._stats = {}

    def run(self, fn, tag, *args, **kwargs):
        """Run a function and update its stats.
        Args:
            fn (function): Function to be executed
            args  (tuple): Argument list
            kwargs (dict): Keyworded, variable-length argument list
        """
        self._logger.info("Running function: %s()", fn.__name__)

        start_time = time.time()

        ret = fn(*args, **kwargs)

        end_time = time.time()
        elapsed_time = end_time - start_time
        self._logger.info(
            "Execution time: %s", self._pretty_time(elapsed_time))

        if tag not in self._stats:
            self._stats[tag] = [0, []]

        self._stats[tag][0] += 1
        self._stats[tag][1].append(elapsed_time)

        return ret

    def get_stat(self, tag):
        """Retrieve the stats of a function.
        Args:
            tag (str): Tag to be query
        Returns:
            tuple: A tuple of the fastest (max), slowest (min), and average
                execution times.
        """
        data = self._stats.get(tag, [0, []])

        return self._calculate_metrics(data[1])

    def set_logger(self, fn):
        """Assign the function to be used for logging.
        Set the function that will be used to print the elapsed time on each
        function call. If this value is not set, the profiling will be
        performed silently.
        Parameters:
            fn (function): Function to be used for logging.
        """
        self._logger = fn

    def print_stats(self):
        """Print all the stats collected so far.
        If the logger has not been set, the stats will be printed by using the
        built-in print function.
        """
        self._logger.info("{0:20} {1:5} {2:10} {3:10} {4:10}".format(
            "Function Tag", "Hits", "Max", "Min", "Average"))

        for fname, data in list(self._stats.items()):
            max_time, min_time, avg_time = self._calculate_metrics(data[1])
            self._logger.info(
                "{0:20} {1:5} {2:10} {3:10} {4:10}".format(
                    fname,
                    data[0],
                    self._pretty_time(max_time),
                    self._pretty_time(min_time),
                    self._pretty_time(avg_time)))

    @classmethod
    def _pretty_time(cls, ftime):
        """Convert to pretty time string."""
        return time.strftime("%H:%M:%S", time.gmtime(ftime))

    @classmethod
    def _calculate_metrics(cls, data):
        """Calculate the maximum, minimum and average values of a given list."""
        max_time = max(data)
        min_time = min(data)
        avg_time = sum(data) / len(data) if data else 0

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
    except IndexError as error:
        raise DaosTestError(
            "Invalid human readable size format: {}".format(size)) from error
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
                output_check="both", env=None):
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
            Defaults to "both".
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
                stdout_text     - decoded stdout
                stderr          - the stderr
                stderr_text     - decoded stderr
                duration        - command execution time
                interrupted     - whether the command completed within timeout
                pid             - command's pid
    """
    log = getLogger()
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
        log.info("Command environment vars:\n  %s", env)
    try:
        # Block until the command is complete or times out
        return process.run(**kwargs)

    except (TypeError, FileNotFoundError) as error:
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
        elif verbose:
            msg = "Error occurred running '{}': {}".format(command, error)
        else:
            msg = "Error occurred running '{}':\n{}".format(
                command, error.result)

    if msg is not None:
        log.info(msg)
        raise DaosTestError(msg)


def run_task(hosts, command, timeout=None, verbose=False):
    """Create a task to run a command on each host in parallel.
    Args:
        hosts (list): list of hosts
        command (str): the command to run in parallel
        timeout (int, optional): command timeout in seconds. Defaults to None.
        verbose (bool, optional): display message for command execution. Defaults to False.
    Returns:
        Task: a ClusterShell.Task.Task object for the executed command
    """
    if not isinstance(hosts, NodeSet):
        hosts = NodeSet.fromlist(hosts)
    task = task_self()
    # Enable forwarding of the ssh authentication agent connection
    task.set_info("ssh_options", "-oForwardAgent=yes")
    kwargs = {"command": command, "nodes": hosts}
    if timeout is not None:
        kwargs["timeout"] = timeout
    if verbose:
        log = getLogger()
        log.info("Running on %s: %s", hosts, command)
    task.run(**kwargs)
    return task


def check_task(task, logger=None):
    """Check the results of the executed task and get the output.
    Args:
        task (Task): a ClusterShell.Task.Task object for the executed command
    Returns:
        bool: if the command returned an 0 exit status on every host
    """
    def check_task_log(message):
        """Log the provided text if a logger is present.
        Args:
            message (str): text to display
        """
        if logger:
            logger.info(message)

    # Create a dictionary of hosts for each unique return code
    results = dict(task.iter_retcodes())

    # Display the command output
    for code in sorted(results):
        output_data = list(task.iter_buffers(results[code]))
        if not output_data:
            output_data = [["<NONE>", results[code]]]
        for output, o_hosts in output_data:
            node_set = NodeSet.fromlist(o_hosts)
            lines = list(output.splitlines())
            if len(lines) > 1:
                # Print the sub-header for multiple lines of output
                check_task_log("    {}: rc={}, output:".format(node_set, code))
            for number, line in enumerate(lines):
                if isinstance(line, bytes):
                    line = line.decode("utf-8")
                if len(lines) == 1:
                    # Print the sub-header and line for one line of output
                    check_task_log("    {}: rc={}, output: {}".format(node_set, code, line))
                    continue
                try:
                    check_task_log("      {}".format(line))
                except IOError:
                    # DAOS-5781 Jenkins doesn't like receiving large amounts of data in a short
                    # space of time so catch this and retry.
                    check_task_log(
                        "*** DAOS-5781: Handling IOError detected while processing line {}/{} with "
                        "retry ***".format(*number + 1, len(lines)))
                    time.sleep(5)
                    check_task_log("      {}".format(line))

    # List any hosts that timed out
    timed_out = [str(hosts) for hosts in task.iter_keys_timeout()]
    if timed_out:
        check_task_log("    {}: timeout detected".format(NodeSet.fromlist(timed_out)))

    # Determine if the command completed successfully across all the hosts
    return len(results) == 1 and 0 in results


def display_task(task):
    """Display the output for the executed task.
    Args:
        task (Task): a ClusterShell.Task.Task object for the executed command
    Returns:
        bool: if the command returned an 0 exit status on every host
    """
    log = getLogger()
    return check_task(task, log)


def log_task(hosts, command, timeout=None):
    """Display the output of the command executed on each host in parallel.
    Args:
        hosts (list): list of hosts
        command (str): the command to run in parallel
        timeout (int, optional): command timeout in seconds. Defaults to None.
    Returns:
        bool: if the command returned an 0 exit status on every host
    """
    return display_task(run_task(hosts, command, timeout, True))


def run_pcmd(hosts, command, verbose=True, timeout=None, expect_rc=0):
    """Run a command on each host in parallel and get the results.
    Args:
        hosts (list): list of hosts
        command (str): the command to run in parallel
        verbose (bool, optional): display command output. Defaults to True.
        timeout (int, optional): command timeout in seconds. Defaults to None.
        expect_rc (int, optional): display output if the command return code
            does not match this value. Defaults to 0. A value of None will
            bypass this feature.
    Returns:
        list: a list of dictionaries with each entry containing output, exit
            status, and interrupted status common to each group of hosts, e.g.:
                [
                    {
                        "command": "ls my_dir",
                        "hosts": NodeSet(wolf-[1-3]),
                        "exit_status": 0,
                        "interrupted": False,
                        "stdout": ["file1.txt", "file2.json"],
                    },
                    {
                        "command": "ls my_dir",
                        "hosts": NodeSet(wolf-[4]),
                        "exit_status": 1,
                        "interrupted": False,
                        "stdout": ["No such file or directory"],
                    },
                    {
                        "command": "ls my_dir",
                        "hosts": NodeSet(wolf-[5-6]),
                        "exit_status": 255,
                        "interrupted": True,
                        "stdout": [""]
                    },
                ]
    """
    log = getLogger()
    results = []

    # Run the command on each host in parallel
    task = run_task(hosts, command, timeout)

    # Get the exit status of each host
    host_exit_status = {host: None for host in hosts}
    for exit_status, host_list in task.iter_retcodes():
        for host in host_list:
            host_exit_status[host] = exit_status

    # Get a list of any interrupted hosts
    host_interrupted = []
    if timeout and task.num_timeout() > 0:
        host_interrupted.extend(list(task.iter_keys_timeout()))

    # Iterate through all the groups of common output
    output_data = list(task.iter_buffers())
    if not output_data:
        output_data = [["", hosts]]
    for output, host_list in output_data:
        # Determine the unique exit status for each host with the same output
        output_exit_status = {}
        for host in host_list:
            if host_exit_status[host] not in output_exit_status:
                output_exit_status[host_exit_status[host]] = NodeSet()
            output_exit_status[host_exit_status[host]].add(host)

        # Determine the unique interrupted state for each host with the same
        # output and exit status
        for exit_status in output_exit_status:
            output_interrupted = {}
            for host in list(output_exit_status[exit_status]):
                is_interrupted = host in host_interrupted
                if is_interrupted not in output_interrupted:
                    output_interrupted[is_interrupted] = NodeSet()
                output_interrupted[is_interrupted].add(host)

            # Add a result entry for each group of hosts with the same output,
            # exit status, and interrupted status
            for interrupted in output_interrupted:
                results.append({
                    "command": command,
                    "hosts": output_interrupted[interrupted],
                    "exit_status": exit_status,
                    "interrupted": interrupted,
                    "stdout": [
                        line.decode("utf-8").rstrip(os.linesep)
                        for line in output],
                })

    # Display results if requested or there is an unexpected exit status
    bad_exit_status = [
        item["exit_status"]
        for item in results
        if expect_rc is not None and item["exit_status"] != expect_rc]
    if verbose or bad_exit_status:
        log.info(colate_results(command, results))

    return results


def colate_results(command, results):
    """Colate the output of run_pcmd.
    Args:
        command (str): command used to obtain the data on each server
        results (list): list: a list of dictionaries with each entry
                        containing output, exit status, and interrupted
                        status common to each group of hosts (see run_pcmd()'s
                        return for details)
    Returns:
        str: a string colating run_pcmd()'s results
    """
    res = ""
    res += "Command: %s\n" % command
    res += "Results:\n"
    for result in results:
        res += "  %s: exit_status=%s, interrupted=%s:" % (
               result["hosts"], result["exit_status"], result["interrupted"])
        for line in result["stdout"]:
            res += "    %s\n" % line

    return res


def get_host_data(hosts, command, text, error, timeout=None):
    """Get the data requested for each host using the specified command.
    Args:
        hosts (list): list of hosts
        command (str): command used to obtain the data on each server
        text (str): data identification string
        error (str): data error string
    Returns:
        list: a list of dictionaries containing the following key/value pairs:
            "hosts": NodeSet containing the hosts with this data
            "data":  data requested for the group of hosts
    """
    log = getLogger()
    host_data = []
    DATA_ERROR = "[ERROR]"

    # Find the data for each specified servers
    log.info("  Obtaining %s data on %s", text, hosts)
    results = run_pcmd(hosts, command, False, timeout, None)
    errors = [
        item["exit_status"]
        for item in results if item["exit_status"] != 0]
    if errors:
        log.info("    %s on the following hosts:", error)
        for result in results:
            if result["exit_status"] in errors:
                log.info(
                    "      %s: rc=%s, interrupted=%s, command=\"%s\":",
                    result["hosts"], result["exit_status"],
                    result["interrupted"], result["command"])
                for line in result["stdout"]:
                    log.info("        %s", line)
        host_data.append({"hosts": NodeSet.fromlist(hosts), "data": DATA_ERROR})
    else:
        for result in results:
            host_data.append(
                {"hosts": result["hosts"], "data": "\n".join(result["stdout"])})

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
    results = run_pcmd(hosts, command, verbose, timeout, expect_rc)
    exit_status = {}
    for result in results:
        if result["exit_status"] not in exit_status:
            exit_status[result["exit_status"]] = NodeSet()
        exit_status[result["exit_status"]].add(result["hosts"])
    return exit_status


def check_file_exists(hosts, filename, user=None, directory=False,
                      sudo=False):
    """Check if a specified file exist on each specified hosts.
    If specified, verify that the file exists and is owned by the user.
    Args:
        hosts (list): list of hosts
        filename (str): file to check for the existence of on each host
        user (str, optional): owner of the file. Defaults to None.
        sudo (bool, optional): whether to run the command via sudo. Defaults to
            False.
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

    if sudo:
        command = "sudo " + command

    task = run_task(hosts, command, verbose=True)
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


def get_random_string(length, exclude=None, include=None):
    """Create a specified length string of random ascii letters and numbers.
    Optionally exclude specific random strings from being returned.
    Args:
        length (int): length of the string to return
        exclude (list, optional): list of strings to not return. Defaults to
            None.
        include (list, optional): list of characters to use in the random
            string. Defaults to None, in which case use all upper case and
            digits.
    Returns:
        str: a string of random ascii letters and numbers
    """
    exclude = exclude if isinstance(exclude, list) else []

    if include is None:
        include = string.ascii_uppercase + string.digits

    random_string = None
    while not isinstance(random_string, str) or random_string in exclude:
        random_string = "".join(random.choice(include) for _ in range(length))  # nosec
    return random_string


def get_random_bytes(length, exclude=None, encoding="utf-8"):
    """Create a specified length string of random ascii letters and numbers.
    Optionally exclude specific random strings from being returned.
    Args:
        length (int): length of the string to return
        exclude (list, optional): list of strings to not return. Defaults to
            None.
        encoding (str, optional): bytes encoding. Defaults to "utf-8"
    Returns:
        bytes : a string of random ascii letters and numbers converted to
                bytes object
    """
    return get_random_string(length, exclude).encode(encoding)


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
        result = check_file_exists(hosts, filename, sudo=True)
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


def dump_engines_stacks(hosts, verbose=True, timeout=60, added_filter=None):
    """Signal the engines on each hosts to generate their ULT stacks dump.
    Args:
        hosts (list): hosts on which to signal the engines
        verbose (bool, optional): display command output. Defaults to True.
        timeout (int, optional): command timeout in seconds. Defaults to 60
            seconds.
        added_filter (str, optional): negative filter to better identify
            engines.
    Returns:
        dict: a dictionary of return codes keys and accompanying NodeSet
            values indicating which hosts yielded the return code.
            Return code keys:
                0   No engine matched the criteria / No engine signaled.
                1   One or more engine matched the criteria and a signal was
                    sent.
    """
    result = {}
    log = getLogger()
    log.info("Dumping ULT stacks of engines on %s", hosts)

    if added_filter:
        ps_cmd = "/usr/bin/ps xa | grep daos_engine | grep -vE {}".format(
            added_filter)
    else:
        ps_cmd = "/usr/bin/pgrep --list-full daos_engine"

    if hosts is not None:
        commands = [
            "rc=0",
            "if " + ps_cmd,
            "then rc=1",
            "sudo pkill --signal USR2 daos_engine",
            # leave some time for ABT info/stacks dump to complete.
            # at this time there is no way to know when Argobots ULTs stacks
            # has completed, see DAOS-1452/DAOS-9942.
            "sleep 30",
            "fi",
            "exit $rc",
        ]
        result = pcmd(hosts, "; ".join(commands), verbose, timeout,
                      None)

    return result


def stop_processes(hosts, pattern, verbose=True, timeout=60, added_filter=None):
    """Stop the processes on each hosts that match the pattern.
    Args:
        hosts (list): hosts on which to stop the processes
        pattern (str): regular expression used to find process names to stop
        verbose (bool, optional): display command output. Defaults to True.
        timeout (int, optional): command timeout in seconds. Defaults to 60
            seconds.
        added_filter (str, optional): negative filter to better identify
            processes.
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

    if added_filter:
        ps_cmd = "/usr/bin/ps xa | grep -E {} | grep -vE {}".format(
            pattern, added_filter)
    else:
        ps_cmd = "/usr/bin/pgrep --list-full {}".format(pattern)

    if hosts is not None:
        commands = [
            "rc=0",
            "if " + ps_cmd,
            "then rc=1",
            "sudo /usr/bin/pkill {}".format(pattern),
            "sleep 5",
            "if " + ps_cmd,
            "then sudo /usr/bin/pkill --signal ABRT {}".format(pattern),
            "sleep 1",
            "if " + ps_cmd,
            "then sudo /usr/bin/pkill --signal KILL {}".format(pattern),
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
            output = result.stdout_text
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
                    output = result.stdout_text
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
    except (AttributeError, ValueError, TypeError) as error:
        raise AttributeError(
            "Invalid 'numeric_range' argument - must be a string containing "
            "only numbers, dashes (-), and/or commas (,): {}".format(
                numeric_range)) from error

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

    return int(result.stdout_text)


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
    requested_error_count = 0
    other_error_count = 0
    command = 'cat {} | grep \" ERR \"'.format(get_log_file(log_file))
    results = run_pcmd(hostlist, command, False, None, None)
    for result in results:
        for line in result["stdout"]:
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
            "Invalid '{}' class name for {}: {}".format(
                name, module, error)) from error
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
    if name in ["Mpirun", "Orterun"]:
        manager = manager_class(job, subprocess=subprocess, mpi_type=mpi)
    elif name == "Systemctl":
        manager = manager_class(job)
    else:
        manager = manager_class(job, subprocess=subprocess)
    return manager


def convert_string(item, separator=","):
    """Convert the object into a string.
    If the object is a list, tuple, NodeSet, etc. return a comma-separated
    string of the values.
    Args:
        separator (str, optional): list item separator. Defaults to ",".
    Returns:
        str: item to convert into a string
    """
    if isinstance(item, (list, tuple, set)):
        item = convert_list(item, separator)
    elif not isinstance(item, str):
        item = str(item)
    return item


def create_directory(hosts, directory, timeout=15, verbose=True,
                     raise_exception=True, sudo=False):
    """Create the specified directory on the specified hosts.
    Args:
        hosts (list): hosts on which to create the directory
        directory (str): the directory to create
        timeout (int, optional): command timeout. Defaults to 15 seconds.
        verbose (bool, optional): whether to log the command run and
            stdout/stderr. Defaults to True.
        raise_exception (bool, optional): whether to raise an exception if the
            command returns a non-zero exit status. Defaults to True.
        sudo (bool, optional): whether to run the command via sudo. Defaults to
            False.
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
    return run_command(
        "{} /usr/bin/mkdir -p {}".format(
            get_clush_command(hosts, "-S -v", sudo), directory),
        timeout=timeout, verbose=verbose, raise_exception=raise_exception)


def change_file_owner(hosts, filename, owner, group, timeout=15, verbose=True,
                      raise_exception=True, sudo=False):
    """Create the specified directory on the specified hosts.
    Args:
        hosts (list): hosts on which to create the directory
        filename (str): the file for which to change ownership
        owner (str): new owner of the file
        group (str): new group owner of the file
        timeout (int, optional): command timeout. Defaults to 15 seconds.
        verbose (bool, optional): whether to log the command run and
            stdout/stderr. Defaults to True.
        raise_exception (bool, optional): whether to raise an exception if the
            command returns a non-zero exit status. Defaults to True.
        sudo (bool, optional): whether to run the command via sudo. Defaults to
            False.
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
    return run_command(
        "{} chown {}:{} {}".format(
            get_clush_command(hosts, "-S -v", sudo), owner, group, filename),
        timeout=timeout, verbose=verbose, raise_exception=raise_exception)


def distribute_files(hosts, source, destination, mkdir=True, timeout=60,
                     verbose=True, raise_exception=True, sudo=False,
                     owner=None):
    """Copy the source to the destination on each of the specified hosts.
    Optionally (by default) ensure the destination directory exists on each of
    the specified hosts prior to copying the source.
    Args:
        hosts (list): hosts on which to copy the source
        source (str): the file to copy to the hosts
        destination (str): the host location in which to copy the source
        mkdir (bool, optional): whether or not to ensure the destination
            directory exists on hosts prior to copying the source. Defaults to
            True.
        timeout (int, optional): command timeout. Defaults to 60 seconds.
        verbose (bool, optional): whether to log the command run and
            stdout/stderr. Defaults to True.
        raise_exception (bool, optional): whether to raise an exception if the
            command returns a non-zero exit status. Defaults to True.
        sudo (bool, optional): whether to run the command via sudo. Defaults to
            False.
        owner (str, optional): if specified the owner to assign as the owner of
            the copied file. Defaults to None.
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
    result = None
    if mkdir:
        result = create_directory(
            hosts, os.path.dirname(destination), verbose=verbose,
            raise_exception=raise_exception)
    if result is None or result.exit_status == 0:
        if sudo:
            # In order to copy a protected file to a remote host in CI the
            # source will first be copied as is to the remote host
            localhost = gethostname().split(".")[0]
            other_hosts = [host for host in hosts if host != localhost]
            if other_hosts:
                # Existing files with strict file permissions can cause the
                # subsequent non-sudo copy to fail, so remove the file first
                rm_command = "{} rm -f {}".format(
                    get_clush_command(other_hosts, "-S -v", True), source)
                run_command(rm_command, verbose=verbose, raise_exception=False)
                result = distribute_files(
                    other_hosts, source, source, mkdir=True,
                    timeout=timeout, verbose=verbose,
                    raise_exception=raise_exception, sudo=False, owner=None)
            if result is None or result.exit_status == 0:
                # Then a local sudo copy will be executed on the remote node to
                # copy the source to the destination
                command = "{} cp {} {}".format(
                    get_clush_command(hosts, "-S -v", True), source,
                    destination)
                result = run_command(command, timeout, verbose, raise_exception)
        else:
            # Without the sudo requirement copy the source to the destination
            # directly with clush
            command = "{} --copy {} --dest {}".format(
                get_clush_command(hosts, "-S -v", False), source, destination)
            result = run_command(command, timeout, verbose, raise_exception)

        # If requested update the ownership of the destination file
        if owner is not None and result.exit_status == 0:
            change_file_owner(
                hosts, destination, owner, get_primary_group(owner), timeout=timeout,
                verbose=verbose, raise_exception=raise_exception, sudo=sudo)
    return result


def get_clush_command(hosts, args=None, sudo=False):
    """Get the clush command with optional sudo arguments.
    Args:
        hosts (object): hosts with which to use the clush command
        args (str, optional): additional clush command line arguments. Defaults
            to None.
        sudo (bool, optional): if set the clush command will be configured to
            run a command with sudo privileges. Defaults to False.
    Returns:
        str: the clush command
    """
    command = ["clush", "-w", convert_string(hosts)]
    if args:
        command.insert(1, args)
    if sudo:
        # If ever needed, this is how to disable host key checking:
        # command.extend(["-o", "-oStrictHostKeyChecking=no", "sudo"])
        command.append("sudo")
    return " ".join(command)


def get_default_config_file(name):
    """Get the default config file.
    Args:
        name (str): daos component name, e.g. server, agent, control
    Returns:
        str: the default config file
    """
    file_name = "".join(["daos_", name, ".yml"])
    return os.path.join(os.sep, "etc", "daos", file_name)


def get_file_listing(hosts, files):
    """Get the file listing from multiple hosts.
    Args:
        hosts (object): hosts with which to use the clush command
        files (object): list of multiple files to list or a single file as a str
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
    result = run_command(
        "{} /usr/bin/ls -la {}".format(
            get_clush_command(hosts, "-S -v", True),
            convert_string(files, " ")),
        verbose=False, raise_exception=False)
    return result


def get_subprocess_stdout(subprocess):
    """Get the stdout from the specified subprocess.
    Args:
        subprocess (process.SubProcess): subprocess from which to get stdout
    Returns:
        str: the std out of the subprocess
    """
    output = subprocess.get_stdout()
    if isinstance(output, bytes):
        output = output.decode("utf-8")
    return output


def create_string_buffer(value, size=None):
    """Create a ctypes string buffer.
    Converts any string to a bytes object before calling
    ctypes.create_string_buffer().
    Args:
    value (object): value to pass to ctypes.create_string_buffer()
    size (int, optional): size to pass to ctypes.create_string_buffer()
    Returns:
    array: return value from ctypes.create_string_buffer()
    """
    if isinstance(value, str):
        value = value.encode("utf-8")
    return ctypes.create_string_buffer(value, size)


def get_display_size(size):
    """Get a string of the provided size in bytes and human-readable sizes.
    Args:
        size (int): size in bytes
    Returns:
        str: the size represented in bytes and human-readable sizes
    """
    return "{} ({}) ({})".format(
        size, bytes_to_human(size, binary=True),
        bytes_to_human(size, binary=False))


def report_errors(test, errors):
    """Print errors and fail the test if there's any errors.
    Args:
        test (Test): Test class.
        errors (list): List of errors.
    """
    if errors:
        test.log.error("Errors detected:")
        for error in errors:
            test.log.error("  %s", error)
        error_msg = ("{} error{} detected".format(
            len(errors), "" if len(errors) == 0 else "s"))
        test.fail(error_msg)

    test.log.info("No errors detected.")


def percent_change(val1, val2):
    """Calculate percent change between two values as a decimal.
    Args:
        val1 (float): first value.
        val2 (float): second value.
    Returns:
        float: decimal percent change.
    """
    if val1 and val2:
        return (float(val2) - float(val1)) / float(val1)
    return 0.0


def get_primary_group(user=None):
    """Get the name of the user's primary group.
    Args:
        user (str, optional): the user account name. Defaults to None, which uses the current user.
    Returns:
        str: the primary group name
    """
    if user is None:
        user = getuser()
    try:
        gid = pwd.getpwnam(user).pw_gid
        return grp.getgrgid(gid).gr_name
    except KeyError:
        # User may not exist on this host, e.g. daos_server, so just return the user name
        return user


def get_journalctl(hosts, since, until, journalctl_type):
    """Run the journalctl on the hosts.
    Args:
        hosts (list): List of hosts to run journalctl.
        since (str): Start time to search the log.
        until (str): End time to search the log.
        journalctl_type (str): String to search in the log. -t param for journalctl.
    Returns:
        list: a list of dictionaries containing the following key/value pairs:
            "hosts": NodeSet containing the hosts with this data
            "data":  data requested for the group of hosts
    """
    command = ("sudo /usr/bin/journalctl --system -t {} --since=\"{}\" "
               "--until=\"{}\"".format(journalctl_type, since, until))
    err = "Error gathering system log events"
    results = get_host_data(hosts=hosts, command=command, text="journalctl", error=err)

    return results


def get_avocado_config_value(section, key):
    """Get an avocado configuration value.
    Args:
        section (str): the configuration section, e.g. 'runner.timeout'
        key (str): the configuration key, e.g. 'process_died'
    Returns:
        object: the value of the requested config key in the specified section
    """
    if int(MAJOR) >= 82:
        config = settings.as_dict()
        return config.get(".".join([section, key]))
    return settings.get_value(section, key)     # pylint: disable=no-member


def set_avocado_config_value(section, key, value):
    """Set an avocado configuration value.
    Args:
        section (str): the configuration section, e.g. 'runner.timeout'
        key (str): the configuration key, e.g. 'process_died'
        value (object): the value to set
    """
    if int(MAJOR) >= 82:
        settings.update_option(".".join([section, key]), value)
    else:
        settings.config.set(section, key, str(value))
