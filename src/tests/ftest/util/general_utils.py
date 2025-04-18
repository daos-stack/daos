"""
  (C) Copyright 2018-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines

import ctypes
import math
import os
import random
import re
import string
import time
from datetime import datetime
from getpass import getuser
from importlib import import_module
from logging import getLogger

from avocado.core.settings import settings
from avocado.core.version import MAJOR
from avocado.utils import process
from ClusterShell.NodeSet import NodeSet
from ClusterShell.Task import task_self
from run_utils import command_as_user, run_local, run_remote


class DaosTestError(Exception):
    """DAOS API exception class."""


def human_to_bytes(size):
    """Convert a human readable size value to respective byte value.

    Args:
        size (str): human readable size value to be converted.

    Raises:
        DaosTestError: when an invalid human readable size value is provided

    Returns:
        int|float: value translated to bytes.

    """
    conversion = {}
    for index, unit in enumerate(('', 'k', 'm', 'g', 't', 'p', 'e')):
        conversion[unit] = 1000 ** index
        conversion[f'{unit}b'] = 1000 ** index
        conversion[f'{unit}ib'] = 1024 ** index
    try:
        match = re.findall(r'([0-9.]+)\s*([a-zA-Z]+|)', str(size))
        number = match[0][0]
        unit = match[0][1].lower()
    except (TypeError, IndexError) as error:
        raise DaosTestError(f'Invalid human readable size format: {size}') from error
    try:
        value = float(number) * conversion[unit]
    except KeyError as error:
        raise DaosTestError(f'Invalid unit detected, not in {conversion.keys()}: {unit}') from error
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
    it will use shlex.split() to break up the command into a list before it is
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

    return None


def run_task(hosts, command, timeout=None, verbose=False):
    """Create a task to run a command on each host in parallel.

    Args:
        hosts (NodeSet): hosts on which to run the command
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
        hosts (NodeSet): hosts on which to run the command
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
    host_exit_status = {str(host): None for host in hosts}
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
        for exit_status, _hosts in output_exit_status.items():
            output_interrupted = {}
            for host in list(_hosts):
                is_interrupted = host in host_interrupted
                if is_interrupted not in output_interrupted:
                    output_interrupted[is_interrupted] = NodeSet()
                output_interrupted[is_interrupted].add(host)

            # Add a result entry for each group of hosts with the same output,
            # exit status, and interrupted status
            for interrupted, _hosts in output_interrupted.items():
                results.append({
                    "command": command,
                    "hosts": _hosts,
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
        log.info(collate_results(command, results))

    return results


def collate_results(command, results):
    """Collate the output of run_pcmd.

    Args:
        command (str): command used to obtain the data on each server
        results (list): list: a list of dictionaries with each entry
                        containing output, exit status, and interrupted
                        status common to each group of hosts (see run_pcmd()'s
                        return for details)

    Returns:
        str: a string collating run_pcmd()'s results

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
        hosts (NodeSet): hosts on which to run the command
        command (str): command used to obtain the data on each server
        text (str): data identification string
        error (str): data error string

    Returns:
        list: a list of dictionaries containing the following key/value pairs:
            "hosts": NodeSet containing the hosts with this data
            "data":  data requested for the group of hosts

    """
    log = getLogger()
    data_error = "[ERROR]"

    # Find the data for each specified servers
    log.info("  Obtaining %s data on %s", text, hosts)
    result = run_remote(log, hosts, command, verbose=False, timeout=timeout)
    if result.failed_hosts:
        log.info(" %s on hosts: %s", error, str(result.failed_hosts))
        return [{"hosts": result.failed_hosts, "data": data_error}]

    return [
        {"hosts": NodeSet(_hosts), "data": stdout}
        for _hosts, stdout in result.all_stdout.items()]


def pcmd(hosts, command, verbose=True, timeout=None, expect_rc=0):
    """Run a command on each host in parallel and get the return codes.

    Args:
        hosts (NodeSet): hosts on which to run the command
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
        hosts (NodeSet): hosts on which to run the command
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


def join(joiner, *args):
    """Join one or more objects together with a specified string.

    Args:
        joiner (str): string to use to join the other objects
        *args: the objects to join. Any non-None object will be passed to str().

    Returns:
        str: a string containing all the objects joined by the joiner string
    """
    return joiner.join(filter(None, map(str, args)))


def list_to_str(value, joiner=","):
    """Convert a list to a string by joining its items.

    Args:
        value (list): list to convert to a string
        joiner (str, optional): string to use to join items. Defaults to ",".

    Returns:
        str: a string of each list entry joined by the joiner string

    """
    return join(joiner, *value)


def dict_to_list(value, joiner="="):
    """Convert a dictionary to a list of joined key and value pairs.

    Args:
        value (dict): dictionary to convert into a list
        joiner (str, optional): string to use to join each key and value. Defaults to "=".

    Returns:
        list: a list of joined dictionary key and value strings

    """
    return [list_to_str(items, joiner) for items in value.items()]


def dict_to_str(value, joiner=", ", items_joiner="="):
    """Convert a dictionary to a string of joined key and value joined pairs.

    Args:
        value (dict): dictionary to convert into a string
        joiner (str, optional): string to use to join dict_to_list() item. Defaults to ", ".
        items_joiner (str, optional): string to use to join each key and value. Defaults to "=".

    Returns:
        str: a string of each dictionary key and value pair joined by the items_joiner string all
            joined by the joiner string

    """
    return list_to_str(dict_to_list(value, items_joiner), joiner)


def dump_engines_stacks(hosts, verbose=True, timeout=60, added_filter=None):
    """Signal the engines on each hosts to generate their ULT stacks dump.

    Args:
        hosts (NodeSet): hosts on which to signal the engines
        verbose (bool, optional): display command output. Defaults to True.
        timeout (int, optional): command timeout in seconds. Defaults to 60
            seconds.
        added_filter (str, optional): negative filter to better identify
            engines.

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
            Return code keys:
                0   No engine matched the criteria / No engine signaled.
                1   One or more engine matched the criteria and a signal was
                    sent.
        None: if hosts is None

    """
    if hosts is None:
        return None

    log = getLogger()
    log.info("Dumping ULT stacks of engines on %s", hosts)

    if added_filter:
        ps_cmd = "/usr/bin/ps xa | grep daos_engine | grep -vE {}".format(
            added_filter)
    else:
        ps_cmd = "/usr/bin/pgrep --list-full daos_engine"

    command = "; ".join([
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
    ])
    return run_remote(log, hosts, command, verbose=verbose, timeout=timeout)


def get_log_file(name):
    """Get the full log file name and path.

    Prepends the DAOS_TEST_LOG_DIR path (or /tmp) to the specified file name.

    Args:
        name (str): log file name

    Returns:
        str: full log file name including path

    """
    return os.path.join(os.environ.get("DAOS_TEST_LOG_DIR", "/tmp"), name)


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


def get_errors_count(log, hostlist, file_glob):
    """Count the number of errors found in log files.

    Args:
        log (logger): logger for the messages produced by this method
        hostlist (list): System list to looks for an error.
        file_glob (str): Glob pattern of the log file to parse.

    Returns:
        dict: A dictionary of the count of errors.

    """
    # Get the Client side Error from client_log file.
    cmd = "cat {} | sed -n -E -e ".format(get_log_file(file_glob))
    cmd += r"'/^.+[[:space:]]ERR[[:space:]].+[[:space:]]DER_[^(]+\([^)]+\).+$/"
    cmd += r"s/^.+[[:space:]]DER_[^(]+\((-[[:digit:]]+)\).+$/\1/p'"
    result = run_remote(log, hostlist, cmd, verbose=False)
    errors_count = {}
    for error_str in sum([data.stdout for data in result.output], []):
        error = int(error_str)
        if error not in errors_count:
            errors_count[error] = 0
        errors_count[error] += 1

    return errors_count


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
    elif name in ["Systemctl", "Clush"]:
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
        item = list_to_str(item, separator)
    elif not isinstance(item, str):
        item = str(item)
    return item


def get_default_config_file(name, path=None):
    """Get the default config file.

    Args:
        name (str): daos component name, e.g. server, agent, control
        path (str, optional): path to use for the config file. Defaults to None which will use the
            /etc/daos default.

    Returns:
        str: the default config file
    """
    if path is None:
        path = os.path.join(os.sep, "etc", "daos")
    return os.path.join(path, f"daos_{name}.yml")


def get_file_listing(hosts, files, user):
    """Get the file listing from multiple hosts.

    Args:
        hosts (NodeSet): hosts with which to use the clush command
        files (object): list of multiple files to list or a single file as a str
        user (str): user used to run the ls command

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    log = getLogger()
    ls_command = command_as_user(f"/usr/bin/ls -la {convert_string(files, ' ')}", user)
    return run_remote(log, hosts, ls_command, False)


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


def append_error(errors, title, details=None):
    """Helper adding an error to the list of errors

    Args:
        errors (list): List of error messages
        title (str): Error message title
        details (list, optional): List of string of the error details
    """
    msg = title
    if details:
        msg += "\n\t" + "\n\t".join(details)
    errors.append(msg)


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

    Raises:
        ValueError: if either val is not a number

    Returns:
        float: decimal percent change.
        math.nan: if val1 is 0

    """
    try:
        return (float(val2) - float(val1)) / float(val1)
    except ZeroDivisionError:
        return math.nan


def get_journalctl_command(since, until=None, system=False, units=None, identifiers=None):
    """Get the journalctl command to capture all unit/identifier activity from since to until.

    Args:
        since (str): show log entries from this date.
        until (str, optional): show log entries up to this date id specified. Defaults to None.
        system (bool, optional): show messages from system services and the kernel. Defaults to
            False which will show all messages that the user can see.
        units (str/list, optional): show messages for the specified systemd unit(s). Defaults to
            None.
        identifiers (str/list, optional): show messages for the specified syslog identifier(s).
            Defaults to None.

    Returns:
        str: journalctl command to capture all unit activity

    """
    command = ["sudo", os.path.join(os.sep, "usr", "bin", "journalctl")]
    if system:
        command.append("--system")
    for key, values in {"unit": units or [], "identifier": identifiers or []}.items():
        for item in values if isinstance(values, (list, tuple)) else [values]:
            command.append("--{}={}".format(key, item))
    command.append("--since=\"{}\"".format(since))
    if until:
        command.append("--until=\"{}\"".format(until))
    return " ".join(command)


def get_journalctl(hosts, since, until, journalctl_type):
    """Run the journalctl on the hosts.

    Args:
        hosts (NodeSet): hosts to run journalctl.
        since (str): Start time to search the log.
        until (str): End time to search the log.
        journalctl_type (str): String to search in the log. -t param for journalctl.

    Returns:
        list: a list of dictionaries containing the following key/value pairs:
            "hosts": NodeSet containing the hosts with this data
            "data":  data requested for the group of hosts

    """
    command = get_journalctl_command(since, until, True, identifiers=journalctl_type)
    err = "Error gathering system log events"
    return get_host_data(hosts=hosts, command=command, text="journalctl", error=err)


def journalctl_time(when=None):
    # pylint: disable=wrong-spelling-in-docstring
    """Get time formatted for journalctl.

    Args:
        when (datetime, optional): time to format. Defaults to datetime.now()

    Returns:
        str: the time in journalctl format
    """
    return (when or datetime.now()).strftime("%Y-%m-%d %H:%M:%S")


def get_avocado_config_value(section, key):
    """Get an avocado configuration value.

    Args:
        section (str): the configuration section, e.g. 'runner.timeout'
        key (str): the configuration key, e.g. 'process_died'

    Returns:
        object: the value of the requested config key in the specified section

    """
    if int(MAJOR) >= 82:
        config = settings.as_dict()  # pylint: disable=no-member
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
        settings.update_option(".".join([section, key]), value)  # pylint: disable=no-member
    else:
        settings.config.set(section, key, str(value))


def nodeset_append_suffix(nodeset, suffix):
    """Append a suffix to each element of a NodeSet/list.

    Only appends if the element does not already end with the suffix.

    Args:
        nodeset (NodeSet/list): the NodeSet or list
        suffix: the suffix to append

    Returns:
        NodeSet: a new NodeSet with the suffix
    """
    return NodeSet.fromlist(
        map(lambda host: host if host.endswith(suffix) else host + suffix, nodeset))


def wait_for_result(log, get_method, timeout, delay=1, add_log=False, **kwargs):
    """Wait for a result with a timeout.

    Args:
        log (logger): logger for the messages produced by this method.
        get_method (object): method that returns a boolean used to determine if the result.
            is found.
        timeout (int): number of seconds to wait for a response to be found.
        delay (int, optional): number of seconds to wait before checking the another result.
            This should be a small number. Defaults to 1.
        add_log (bool, optional): whether or not to include the log argument for get_method.
            Defaults to False.
        kwargs (dict): kwargs for get_method.

    Returns:
        bool: if the result was found.
    """
    if add_log:
        kwargs["log"] = log
    method_str = "{}({})".format(
        get_method.__name__,
        ", ".join([list_to_str(items, "=") for items in kwargs.items() if items[0] != "log"]))
    result_found = False
    timed_out = False
    start = time.time()
    log.debug(
        "wait_for_result: Waiting for a result from %s with a %s second timeout",
        method_str, timeout)
    while not result_found and not timed_out:
        timed_out = (time.time() - start) >= timeout
        result_found = get_method(**kwargs)
        log.debug(
            "wait_for_result: Result from %s: %s (timed out: %s)",
            method_str, result_found, timed_out)
        if not result_found and not timed_out:
            time.sleep(delay)
    log.debug("wait_for_result: Waiting for a result from %s complete", method_str)
    return not timed_out


def check_ping(log, host, expected_ping=True, cmd_timeout=60, verbose=True):
    """Check the host for a ping response.

    Args:
        log (logger): logger for the messages produced by this method.
        host (Node): destination host to ping to.
        expected_ping (bool, optional): whether a ping response is expected. Defaults to True.
        cmd_timeout (int, optional): number of seconds to wait for a response to be found.
            Defaults to 60.
        verbose (bool, optional): display check ping commands. Defaults to True.

    Returns:
        bool: True if the expected number of pings were returned; False otherwise.
    """
    log.debug("Checking for %s to be %s", host, "responsive" if expected_ping else "unresponsive")
    if not run_local(log, f"ping -c 1 {host}", timeout=cmd_timeout, verbose=verbose).passed:
        return not expected_ping
    return expected_ping


def check_ssh(log, hosts, cmd_timeout=60, verbose=True):
    """Check the host for a successful pass-wordless ssh.

    Args:
        log (logger): logger for the messages produced by this method.
        hosts (NodeSet): destination hosts to ssh to.
        cmd_timeout (int, optional): number of seconds to wait for a response to be found.
        verbose (bool, optional): display check ping commands. Defaults to True.

    Returns:
        bool: True if all hosts respond to the remote ssh session; False otherwise.
    """
    result = run_remote(log, hosts, "uname", timeout=cmd_timeout, verbose=verbose)
    return result.passed
