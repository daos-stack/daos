"""
  (C) Copyright 2022-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re
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


class LocalTask():
    """A mock ClusterShell.Task object for subprocess command output."""

    def __init__(self, host, return_code, stdout, stderr, timed_out):
        """Create a LocalTask.

        Args:
            host (NodeSet): host from which the command was executed
            return_code (int): executed command's return code
            stdout (str): executed command's stdout
            stderr (str): executed command's stderr
            timed_out (bool) did the executed command time out
        """
        self._return_codes = {return_code: [host]}
        self._stdout = {stdout if stdout is not None else '': [host]}
        self._stderr = {stderr if stderr is not None else '': [host]}
        self._timeout_sources = []
        if timed_out:
            self._timeout_sources.append(host)

    def iter_retcodes(self):
        """Iterate over return codes of the local command result.

        Yields:
            tuple: return code (int), hosts (list)
        """
        yield from self._return_codes.items()

    def iter_keys_timeout(self):
        """Iterate over hosts that timed out.

        Yields:
            str: host where the command timed out
        """
        yield from self._timeout_sources

    def iter_buffers(self, match_keys=None):
        """Iterate over the command stdout for each host.

        Args:
            match_keys (list, optional): only return output matching these hosts. Defaults to None.

        Returns:
            tuple: command stdout (str), hosts (list)
        """
        for output, hosts in self._stdout.items():
            if match_keys is None or hosts[0] in match_keys:
                yield output, hosts

    def iter_errors(self, match_keys=None):
        """Iterate over the command stderr for each host.

        Args:
            match_keys (list, optional): only return output matching these hosts. Defaults to None.

        Returns:
            tuple: command stderr (str), hosts (list)
        """
        for output, hosts in self._stderr.items():
            if match_keys is None or hosts[0] in match_keys:
                yield output, hosts


class CommandResult():
    """Groups of command results from the same hosts with the same return status."""

    def __init__(self, command, task):
        """Create a CommandResult object.

        Args:
            command (str): command executed
            task (Task): object containing the results from an executed clush command
        """
        self.output = []

        # Get a dictionary of host list values for each unique return code key
        return_codes = dict(task.iter_retcodes())

        # Add any hosts that timed out using an 124 return code
        timed_out_hosts = list(task.iter_keys_timeout())
        if timed_out_hosts and 124 in return_codes:
            # To be on the safe side even though we typically wouldn't see 124 from iter_retcodes()
            return_codes[124].extend(timed_out_hosts)
        elif timed_out_hosts:
            return_codes[124] = timed_out_hosts

        # Populate the a list of unique output for each NodeSet
        for code in sorted(return_codes):
            stdout_data = self.__sanitize_iter_data(
                return_codes[code], list(task.iter_buffers(return_codes[code])), '')

            for stdout_raw, stdout_hosts in stdout_data:
                # In run_remote(), task.run() is executed with the stderr=False default.
                # As a result task.iter_buffers() will return combined stdout and stderr.
                stdout = self.__msg_tree_elem_to_list(stdout_raw)
                stderr_data = self.__sanitize_iter_data(
                    stdout_hosts, list(task.iter_errors(stdout_hosts)), '')
                for stderr_raw, stderr_hosts in stderr_data:
                    stderr = self.__msg_tree_elem_to_list(stderr_raw)
                    self.output.append(
                        ResultData(
                            command, code, NodeSet.fromlist(stderr_hosts), stdout, stderr,
                            bool(code == 124)))

    @staticmethod
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

    @staticmethod
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
    def timeout_hosts(self):
        """Get all timeout hosts.

        Returns:
            NodeSet: all nodes where the command timed out
        """
        return NodeSet.fromlist(data.hosts for data in self.output if data.timeout)

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
        all_stdout = self.all_stdout
        return '\n'.join(filter(None, [all_stdout[key] for key in sorted(all_stdout)]))

    @property
    def joined_stderr(self):
        """Get all of the stderr from the issued command from each host joined by newlines.

        Returns:
            str: all of the stderr from each host joined by newlines
        """
        all_stderr = self.all_stderr
        return '\n'.join(filter(None, [all_stderr[key] for key in sorted(all_stderr)]))

    def log_output(self, log):
        """Log the command result.

        Args:
            log (logger): logger for the messages produced by this method
        """
        for data in self.output:
            log_result_data(log, data)


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


def get_clush_command(hosts, args=None, command="", command_env=None, command_sudo=False,
                      timeout=None, fanout=None):
    """Get the clush command with optional sudo arguments.

    Args:
        hosts (NodeSet): hosts with which to use the clush command.
        args (str, optional): additional clush command line arguments. Defaults to None.
        command (str, optional): command to execute with clush. Defaults to empty string.
        command_env (EnvironmentVariables, optional): environment variables to export with the
            command. Defaults to None.
        sudo (bool, optional): whether to run the command with sudo privileges. Defaults to False.
        timeout (int, optional): number of seconds to wait for the command to complete.
            Defaults to None.
        fanout (int, optional): fanout to use. Default uses the max of the
            clush default (64) or available cores

    Returns:
        str: the clush command
    """
    if fanout is None:
        fanout = max(64, len(os.sched_getaffinity(0)))
    cmd_list = ["clush", "-f", str(fanout), "-w", str(hosts)]
    if timeout is not None:
        cmd_list.extend(["-u", str(timeout)])
    if args:
        cmd_list.append(args)
    # If ever needed, this is how to disable host key checking:
    # cmd_list.extend(["-o", "-oStrictHostKeyChecking=no"])
    cmd_list.append(command_as_user(command, "root" if command_sudo else "", command_env))
    return " ".join(cmd_list)


def run_local(log, command, verbose=True, timeout=None, stderr=False, capture_output=True):
    """Run the command on the local host.

    Args:
        log (logger): logger for the messages produced by this method
        command (str): command from which to obtain the output
        verbose (bool, optional): log the command output. Defaults to True.
        timeout (int, optional): number of seconds to wait for the command to complete.
            Defaults to None.
        stderr (bool, optional): whether to enable stdout/stderr separation. Defaults to False.
        capture_output (bool, optional): whether to include stdout/stderr in the CommandResult.
            Defaults to True.

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    local_host = NodeSet(gethostname().split(".")[0])
    kwargs = {
        "encoding": "utf-8",
        "shell": True,
        "check": False,
        "timeout": timeout,
        "env": os.environ.copy()
    }
    if capture_output:
        kwargs["stdout"] = subprocess.PIPE
        kwargs["stderr"] = subprocess.PIPE if stderr else subprocess.STDOUT

    if timeout and verbose:
        log.debug("Running on %s with a %s timeout: %s", local_host, timeout, command)
    elif verbose:
        log.debug("Running on %s: %s", local_host, command)

    try:
        # pylint: disable=subprocess-run-check
        process = subprocess.run(command, **kwargs)     # nosec
        task = LocalTask(local_host, process.returncode, process.stdout, process.stderr, False)

    except subprocess.TimeoutExpired as error:
        # Raised if command times out
        task = LocalTask(local_host, 124, error.stdout, error.stderr, True)

    except Exception as error:  # pylint: disable=broad-except
        # Catch all
        task = LocalTask(local_host, 255, None, str(error), False)

    results = CommandResult(command, task)
    if capture_output and verbose:
        # Log any captured command output when requested
        results.log_output(log)
    elif capture_output:
        # Always log any failed commands whose output was captured
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
    results = CommandResult(command, task)
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


def stop_processes(log, hosts, pattern, verbose=True, timeout=60, exclude=None, force=False,
                   full_command=False):
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
        full_command (bool, optional): if set match the pattern using the full command with
            pgrep/pkill. Defaults to False.

    Raises:
        ValueError: if the pattern ends up matching process 1.

    Returns:
        tuple: (NodeSet, NodeSet) where the first NodeSet indicates on which hosts processes
            matching the pattern were initially detected and the second NodeSet indicates on which
            hosts the processes matching the pattern are still running (will be empty if every
            process was killed or no process matching the pattern were found).
    """
    processes_detected = NodeSet()
    processes_running = NodeSet()
    search_command = f"/usr/bin/pgrep --list-full {pattern}"
    pattern_match = str(pattern)
    if exclude:
        search_command = f"/usr/bin/ps xa | grep -E {pattern} | grep -vE {exclude}"
        pattern_match += " and doesn't match " + str(exclude)
    elif full_command:
        search_command = f"/usr/bin/pgrep --list-full --full -x {pattern}"

    # Search for any active processes
    log.debug("Searching for any processes on %s that match %s", hosts, pattern_match)
    result = run_remote(log, hosts, search_command, verbose, timeout)
    if not result.passed_hosts:
        log.debug("No processes found on %s that match %s", result.failed_hosts, pattern_match)
        return processes_detected, processes_running

    # Catch any attempt to kill process 1.
    if "1" in re.findall(r"^(\d+)\s+", result.joined_stdout, re.MULTILINE):
        raise ValueError(f"Attempting to kill process 1 as a match for {pattern}!")

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
        if full_command:
            kill_command = f"sudo /usr/bin/pkill{step[0]} --full -x {pattern}"
        run_remote(log, result.passed_hosts, kill_command, verbose, timeout)
        time.sleep(step[1])
        result = run_remote(log, result.passed_hosts, search_command, verbose, timeout)
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
