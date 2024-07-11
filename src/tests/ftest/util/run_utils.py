"""
  (C) Copyright 2022-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import shlex
import subprocess  # nosec
import time
from getpass import getuser
from socket import gethostname

from ClusterShell.NodeSet import NodeSet
from ClusterShell.Task import task_self


class RunException(Exception):
    """Base exception for this module."""


class ResultData():
    # pylint: disable=too-few-public-methods
    """Command result data for the set of hosts."""

    def __init__(self, command, returncode, hosts, stdout, stderr, timeout):
        """Initialize a ResultData object.

        Args:
            command (str): the executed command
            returncode (int): the return code of the executed command
            hosts (NodeSet): the host(s) on which the executed command yielded this result
            stdout (list): the result of the executed command split by newlines
            timeout (bool): indicator for a command timeout
        """
        self.command = command
        self.returncode = returncode
        self.hosts = hosts
        self.stdout = stdout
        self.stderr = stderr
        self.timeout = timeout

    def __lt__(self, other):
        """Determine if another ResultData object is less than this one.

        Args:
            other (NodeSet): the other NodSet to compare

        Returns:
            bool: True if this object is less than the other ResultData object; False otherwise
        """
        if not isinstance(other, ResultData):
            raise NotImplementedError
        return str(self.hosts) < str(other.hosts)

    def __gt__(self, other):
        """Determine if another ResultData object is greater than this one.

        Args:
            other (NodeSet): the other NodSet to compare

        Returns:
            bool: True if this object is greater than the other ResultData object; False otherwise
        """
        return not self.__lt__(other)

    @property
    def passed(self):
        """Did the command pass.

        Returns:
            bool: if the command was successful
        """
        return self.returncode == 0


class CommandResult():
    """Groups of command results from the same hosts with the same return status."""

    def __init__(self):
        """Create a CommandResult object.

        Args:
            command (str): command executed
            task (Task): object containing the results from an executed clush command
        """
        self.output = []

    @property
    def homogeneous(self):
        """Did all the hosts produce the same output.

        Returns:
            bool: if all the hosts produced the same output
        """
        return len(self.output) == 1

    @property
    def passed(self):
        """Did the command pass on all the hosts.

        Returns:
            bool: if the command was successful on each host
        """
        all_zero = all(data.passed for data in self.output)
        return all_zero and not self.timeout

    @property
    def timeout(self):
        """Did the command timeout on any hosts.

        Returns:
            bool: True if the command timed out on at least one set of hosts; False otherwise
        """
        return any(data.timeout for data in self.output)

    @property
    def passed_hosts(self):
        """Get all passed hosts.

        Returns:
            NodeSet: all nodes where the command passed
        """
        return NodeSet.fromlist(data.hosts for data in self.output if data.returncode == 0)

    @property
    def failed_hosts(self):
        """Get all failed hosts.

        Returns:
            NodeSet: all nodes where the command failed
        """
        return NodeSet.fromlist(data.hosts for data in self.output if data.returncode != 0)

    @property
    def all_stdout(self):
        """Get all of the stdout from the issued command from each host.

        Returns:
            dict: the stdout (the values) from each set of hosts (the keys, as a str of the NodeSet)
        """
        stdout = {}
        for data in self.output:
            stdout[str(data.hosts)] = '\n'.join(data.stdout)
        return stdout

    @property
    def all_stderr(self):
        """Get all of the stderr from the issued command from each host.

        Returns:
            dict: the stderr (the values) from each set of hosts (the keys, as a str of the NodeSet)
        """
        stderr = {}
        for data in self.output:
            stderr[str(data.hosts)] = '\n'.join(data.stderr)
        return stderr

    @property
    def joined_stdout(self):
        """Get all of the stdout from the issued command from each host joined by newlines.

        Returns:
            str: all of the stdout from each host joined by newlines
        """
        return '\n'.join(self.all_stdout.values())

    @property
    def joined_stderr(self):
        """Get all of the stderr from the issued command from each host joined by newlines.

        Returns:
            str: all of the stderr from each host joined by newlines
        """
        return '\n'.join(self.all_stderr.values())

    def log_output(self, log):
        """Log the command result.

        Args:
            log (logger): logger for the messages produced by this method
        """
        for data in self.output:
            log_result_data(log, data)


def __get_local_result(command, return_code, stdout, stderr, timeout):
    """Get a CommandResult object for a command issued on the local host.

    Args:
        command (str): command executed
        return_code (int): executed command's return code
        stdout (str): executed command's stdout
        stderr (str): executed command's stderr
        timed_out (bool) did the executed command time out

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    result = CommandResult()
    local_host = gethostname().split(".")[0]
    stdout = stdout.splitlines() if stdout is not None else []
    stderr = stderr.splitlines() if stderr is not None else []
    result.output.append(
        ResultData(command, return_code, NodeSet(local_host), stdout, stderr, timeout))
    return result


def __get_remote_result(command, task):
    """Get a CommandResult object for a command issued on remote hosts.

    Args:
        command (str): command executed
        task (Task): object containing the results from an executed clush command

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    result = CommandResult()

    # Get a dictionary of host list values for each unique return code key
    return_codes = dict(task.iter_retcodes())

    # Get a list of any hosts that timed out
    timed_out = [str(hosts) for hosts in task.iter_keys_timeout()]

    # Populate the a list of unique output for each NodeSet
    for code in sorted(return_codes):
        stdout_data = __sanitize_iter_data(
            return_codes[code], list(task.iter_buffers(return_codes[code])), '')

        for stdout_raw, stdout_hosts in stdout_data:
            # In run_remote(), task.run() is executed with the stderr=False default.
            # As a result task.iter_buffers() will return combined stdout and stderr.
            stdout = __msg_tree_elem_to_list(stdout_raw)
            stderr_data = __sanitize_iter_data(
                stdout_hosts, list(task.iter_errors(stdout_hosts)), '')
            for stderr_raw, stderr_hosts in stderr_data:
                stderr = __msg_tree_elem_to_list(stderr_raw)
                result.output.append(
                    ResultData(
                        command, code, NodeSet.fromlist(stderr_hosts), stdout, stderr, False))
    if timed_out:
        result.output.append(
            ResultData(command, 124, NodeSet.fromlist(timed_out), None, None, True))

    return result


def __sanitize_iter_data(hosts, data, default_entry):
    """Ensure the data generated from an iter function has entries for each host.

    Args:
        hosts (list): lists of host which generated data
        data (list): data from an iter function as a list
        default_entry (object): entry to add to data for missing hosts in data

    Returns:
        list: a list of tuples of entries and list of hosts
    """
    if not data:
        return [(default_entry, hosts)]

    source_keys = NodeSet.fromlist(hosts)
    data_keys = NodeSet()
    for _, keys in data:
        data_keys.add(NodeSet.fromlist(keys))

    sanitized_data = data.copy()
    missing_keys = source_keys - data_keys
    if missing_keys:
        sanitized_data.append((default_entry, list(missing_keys)))
    return sanitized_data


def __msg_tree_elem_to_list(msg_tree_elem):
    """Convert a ClusterShell.MsgTree.MsgTreeElem to a list of strings.

    Args:
        msg_tree_elem (MsgTreeElem): output from Task.iter_* method.

    Returns:
        list: list of strings
    """
    msg_tree_elem_list = []
    for line in msg_tree_elem.splitlines():
        if isinstance(line, bytes):
            msg_tree_elem_list.append(line.decode("utf-8"))
        else:
            msg_tree_elem_list.append(line)
    return msg_tree_elem_list


def log_result_data(log, data):
    """Log a single command result data entry.

    Args:
        log (logger): logger for the messages produced by this method
        data (ResultData): command result common to a set of hosts
    """
    info = " timed out" if data.timeout else ""
    if not data.stdout and not data.stderr:
        log.debug("  %s (rc=%s)%s: <no output>", str(data.hosts), data.returncode, info)
    elif data.stdout and len(data.stdout) == 1 and not data.stderr:
        log.debug("  %s (rc=%s)%s: %s", str(data.hosts), data.returncode, info, data.stdout[0])
    else:
        log.debug("  %s (rc=%s)%s:", str(data.hosts), data.returncode, info)
        indent = 6 if data.stderr else 4
        if data.stdout and data.stderr:
            log.debug("    <stdout>:")
        for line in data.stdout:
            log.debug("%s%s", " " * indent, line)
        if data.stderr:
            log.debug("    <stderr>:")
        for line in data.stderr:
            log.debug("%s%s", " " * indent, line)


def get_clush_command(hosts, args=None, command="", command_env=None, command_sudo=False):
    """Get the clush command with optional sudo arguments.

    Args:
        hosts (NodeSet): hosts with which to use the clush command.
        args (str, optional): additional clush command line arguments. Defaults to None.
        command (str, optional): command to execute with clush. Defaults to empty string.
        command_env (EnvironmentVariables, optional): environment variables to export with the
            command. Defaults to None.
        sudo (bool, optional): whether to run the command with sudo privileges. Defaults to False.

    Returns:
        str: the clush command
    """
    cmd_list = ["clush"]
    if args:
        cmd_list.append(args)
    cmd_list.extend(["-w", str(hosts)])
    # If ever needed, this is how to disable host key checking:
    # cmd_list.extend(["-o", "-oStrictHostKeyChecking=no"])
    cmd_list.append(command_as_user(command, "root" if command_sudo else "", command_env))
    return " ".join(cmd_list)


def run_local(log, command, verbose=True, timeout=None, stderr=False):
    """Run the command locally.

    Args:
        log (logger): logger for the messages produced by this method
        command (str): command from which to obtain the output
        verbose (bool, optional): if set log the output of the command (capture_output must also be
            set). Defaults to True.
        timeout (int, optional): number of seconds to wait for the command to complete.
            Defaults to None.
        stderr (bool, optional): whether to enable stdout/stderr separation. Defaults to False.

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    local_host = NodeSet(gethostname().split(".")[0])
    kwargs = {
        "encoding": "utf-8",
        "shell": False,
        "check": False,
        "timeout": timeout,
        "stdout": subprocess.PIPE,
        "stderr": subprocess.PIPE if stderr else subprocess.STDOUT,
    }
    if timeout and verbose:
        log.debug("Running on %s with a %s timeout: %s", local_host, timeout, command)
    elif verbose:
        log.debug("Running on %s: %s", local_host, command)

    try:
        # pylint: disable=subprocess-run-check
        task = subprocess.run(shlex.split(command), **kwargs)     # nosec
        results = __get_local_result(command, task.returncode, task.stdout, task.stderr, False)

    except subprocess.TimeoutExpired as error:
        # Raised if command times out
        results = __get_local_result(command, 124, error.stdout, error.stderr, True)

    except Exception as error:  # pylint: disable=broad-except
        # Catch all
        results = __get_local_result(command, 255, None, str(error), False)

    if verbose:
        results.log_output(log)
    else:
        # Always log any failed commands
        for data in results.output:
            if not data.passed:
                log_result_data(log, data)
    return results


def run_remote(log, hosts, command, verbose=True, timeout=120, task_debug=False, stderr=False,
               fanout=None):
    """Run the command on the remote hosts.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        command (str): command from which to obtain the output
        verbose (bool, optional): log the command output. Defaults to True.
        timeout (int, optional): number of seconds to wait for the command to complete.
            Defaults to 120 seconds.
        task_debug (bool, optional): whether to enable debug for the task object. Defaults to False.
        stderr (bool, optional): whether to enable stdout/stderr separation. Defaults to False.
        fanout (int, optional): fanout to use. Default uses the max of the
            clush default (64) or available cores

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    task = task_self()
    task.set_info('debug', task_debug)
    task.set_default("stderr", stderr)
    # Set fan out to the max of the default or number of logical cores
    if fanout is None:
        fanout = max(task.info('fanout'), len(os.sched_getaffinity(0)))
    task.set_info('fanout', fanout)
    # Enable forwarding of the ssh authentication agent connection
    task.set_info("ssh_options", "-oForwardAgent=yes")
    if verbose:
        if timeout is None:
            log.debug("Running on %s without a timeout: %s", hosts, command)
        else:
            log.debug("Running on %s with a %s second timeout: %s", hosts, timeout, command)
    task.run(command=command, nodes=hosts, timeout=timeout)
    results = __get_remote_result(command, task)
    if verbose:
        results.log_output(log)
    else:
        # Always log any failed commands
        for data in results.output:
            if not data.passed:
                log_result_data(log, data)
    return results


def command_as_user(command, user, env=None):
    """Adjust a command to be ran as another user.

    Args:
        command (str): the original command
        user (str): user to run as
        env (EnvironmentVariables, optional): environment variables to export with the command.
            Defaults to None.

    Returns:
        str: command adjusted to run as another user
    """
    if not user or user == getuser():
        if not env:
            return command
        return " ".join([env.to_export_str(), command]).strip()

    cmd_list = ["sudo"]
    if env:
        cmd_list.extend(env.to_list())
    cmd_list.append("-n")
    if user != "root":
        # Use runuser to avoid using a password
        cmd_list.extend(["runuser", "-u", user, "--"])
    cmd_list.append(command)
    return " ".join(cmd_list)


def find_command(source, pattern, depth, other=None):
    """Get the find command.

    Args:
        source (str): where the files are currently located
        pattern (str): pattern used to limit which files are processed
        depth (int): max depth for find command
        other (object, optional): other commands, as a list or str, to include at the end of the
            base find command. Defaults to None.

    Returns:
        str: the find command
    """
    command = ["find", source, "-maxdepth", str(depth), "-type", "f", "-name", f"'{pattern}'"]
    if isinstance(other, list):
        command.extend(other)
    elif isinstance(other, str):
        command.append(other)
    return " ".join(command)


def stop_processes(log, hosts, pattern, verbose=True, timeout=60, exclude=None, force=False):
    """Stop the processes on each hosts that match the pattern.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to stop any processes matching the pattern
        pattern (str): regular expression used to find process names to stop
        verbose (bool, optional): display command output. Defaults to True.
        timeout (int, optional): command timeout in seconds. Defaults to 60 seconds.
        exclude (str, optional): negative filter to better identify processes. Defaults to None.
        force (bool, optional): if set use the KILL signal to immediately stop any running
            processes. Defaults to False which will attempt to kill w/o a signal, then with the ABRT
            signal, and finally with the KILL signal.

    Returns:
        tuple: (NodeSet, NodeSet) where the first NodeSet indicates on which hosts processes
            matching the pattern were initially detected and the second NodeSet indicates on which
            hosts the processes matching the pattern are still running (will be empty if every
            process was killed or no process matching the pattern were found).
    """
    processes_detected = NodeSet()
    processes_running = NodeSet()
    command = f"/usr/bin/pgrep --list-full {pattern}"
    pattern_match = str(pattern)
    if exclude:
        command = f"/usr/bin/ps xa | grep -E {pattern} | grep -vE {exclude}"
        pattern_match += " and doesn't match " + str(exclude)

    # Search for any active processes
    log.debug("Searching for any processes on %s that match %s", hosts, pattern_match)
    result = run_remote(log, hosts, command, verbose, timeout)
    if not result.passed_hosts:
        log.debug("No processes found on %s that match %s", result.failed_hosts, pattern_match)
        return processes_detected, processes_running

    # Indicate on which hosts processes matching the pattern were found running in the return status
    processes_detected.add(result.passed_hosts)

    # Initialize on which hosts the processes matching the pattern are still running
    processes_running.add(result.passed_hosts)

    # Attempt to kill any processes found on any of the hosts with increasing force
    steps = [("", 5), (" --signal ABRT", 1), (" --signal KILL", 0)]
    if force:
        steps = [(" --signal KILL", 5)]
    while steps and result.passed_hosts:
        step = steps.pop(0)
        log.debug(
            "Killing%s any processes on %s that match %s and then waiting %s seconds",
            step[0], result.passed_hosts, pattern_match, step[1])
        kill_command = f"sudo /usr/bin/pkill{step[0]} {pattern}"
        run_remote(log, result.passed_hosts, kill_command, verbose, timeout)
        time.sleep(step[1])
        result = run_remote(log, result.passed_hosts, command, verbose, timeout)
        if not result.passed_hosts:
            # Indicate all running processes matching the pattern were stopped in the return status
            log.debug(
                "All processes running on %s that match %s have been stopped.",
                result.failed_hosts, pattern_match)
        # Update the set of hosts on which the processes matching the pattern are still running
        processes_running.difference_update(result.failed_hosts)
    if processes_running:
        log.debug("Processes still running on %s that match: %s", processes_running, pattern_match)
    return processes_detected, processes_running
