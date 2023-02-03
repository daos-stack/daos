"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from socket import gethostname
import subprocess   # nosec
import shlex
from ClusterShell.NodeSet import NodeSet
from ClusterShell.Task import task_self


class RunException(Exception):
    """Base exception for this module."""


class RemoteCommandResult():
    """Stores the command result from a Task object."""

    class ResultData():
        # pylint: disable=too-few-public-methods
        """Command result data for the set of hosts."""

        def __init__(self, command, returncode, hosts, stdout, timeout):
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
            self.timeout = timeout

    def __init__(self, command, task):
        """Create a RemoteCommandResult object.

        Args:
            command (str): command executed
            task (Task): object containing the results from an executed clush command
        """
        self.output = []
        self._process_task(task, command)

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
        all_zero = all(data.returncode == 0 for data in self.output)
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

    def _process_task(self, task, command):
        """Populate the output list and determine the passed result for the specified task.

        Args:
            task (Task): a ClusterShell.Task.Task object for the executed command
            command (str): the executed command
        """
        # Get a dictionary of host list values for each unique return code key
        results = dict(task.iter_retcodes())

        # Get a list of any hosts that timed out
        timed_out = [str(hosts) for hosts in task.iter_keys_timeout()]

        # Populate the a list of unique output for each NodeSet
        for code in sorted(results):
            output_data = list(task.iter_buffers(results[code]))
            if not output_data:
                output_data = [["<NONE>", results[code]]]
            for output, output_hosts in output_data:
                # In run_remote(), task.run() is executed with the stderr=False default.
                # As a result task.iter_buffers() will return combined stdout and stderr.
                stdout = []
                for line in output.splitlines():
                    if isinstance(line, bytes):
                        stdout.append(line.decode("utf-8"))
                    else:
                        stdout.append(line)
                self.output.append(
                    self.ResultData(command, code, NodeSet.fromlist(output_hosts), stdout, False))
        if timed_out:
            self.output.append(
                self.ResultData(command, 124, NodeSet.fromlist(timed_out), None, True))

    def log_output(self, log):
        """Log the command result.

        Args:
            log (logger): logger for the messages produced by this method

        """
        for data in self.output:
            if data.timeout:
                log.debug("  %s (rc=%s): timed out", str(data.hosts), data.returncode)
            elif len(data.stdout) == 1:
                log.debug("  %s (rc=%s): %s", str(data.hosts), data.returncode, data.stdout[0])
            else:
                log.debug("  %s (rc=%s):", str(data.hosts), data.returncode)
                for line in data.stdout:
                    log.debug("    %s", line)


def get_switch_user(user="root"):
    """Get the switch user command for the requested user.

    Args:
        user (str): user account. Defaults to "root".

    Returns:
        list: the sudo command as a list

    """
    command = ["sudo", "-n"]
    if user != "root":
        # Use runuser to avoid using a password
        command.extend(["runuser", "-u", user, "--"])
    return command


def get_clush_command_list(hosts, args=None, sudo=False):
    """Get the clush command with optional sudo arguments.

    Args:
        hosts (NodeSet): hosts with which to use the clush command
        args (str, optional): additional clush command line arguments. Defaults
            to None.
        sudo (bool, optional): if set the clush command will be configured to
            run a command with sudo privileges. Defaults to False.

    Returns:
        list: list of the clush command

    """
    command = ["clush", "-w", str(hosts)]
    if args:
        command.insert(1, args)
    if sudo:
        # If ever needed, this is how to disable host key checking:
        # command.extend(["-o", "-oStrictHostKeyChecking=no", get_switch_user()])
        command.extend(get_switch_user())
    return command


def get_clush_command(hosts, args=None, sudo=False):
    """Get the clush command with optional sudo arguments.

    Args:
        hosts (NodeSet): hosts with which to use the clush command
        args (str, optional): additional clush command line arguments. Defaults
            to None.
        sudo (bool, optional): if set the clush command will be configured to
            run a command with sudo privileges. Defaults to False.

    Returns:
        str: the clush command

    """
    return " ".join(get_clush_command_list(hosts, args, sudo))


def run_local(log, command, capture_output=True, timeout=None, check=False, verbose=True):
    """Run the command locally.

    Args:
        log (logger): logger for the messages produced by this method
        command (str): command from which to obtain the output
        capture_output(bool, optional): whether or not to include the command output in the
            subprocess.CompletedProcess.stdout returned by this method. Defaults to True.
        timeout (int, optional): number of seconds to wait for the command to complete.
            Defaults to None.
        check (bool, optional): if set the method will raise an exception if the command does not
            yield a return code equal to zero. Defaults to False.
        verbose (bool, optional): if set log the output of the command (capture_output must also be
            set). Defaults to True.

    Raises:
        RunException: if the command fails: times out (timeout must be specified),
            yields a non-zero exit status (check must be True), is interrupted by the user, or
            encounters some other exception.

    Returns:
        subprocess.CompletedProcess: an object representing the result of the command execution with
            the following properties:
                - args (the command argument)
                - returncode
                - stdout (only set if capture_output=True)
                - stderr (not used; included in stdout)

    """
    local_host = gethostname().split(".")[0]
    kwargs = {"encoding": "utf-8", "shell": False, "check": check, "timeout": timeout}
    if capture_output:
        kwargs["stdout"] = subprocess.PIPE
        kwargs["stderr"] = subprocess.STDOUT
    if timeout and verbose:
        log.debug("Running on %s with a %s timeout: %s", local_host, timeout, command)
    elif verbose:
        log.debug("Running on %s: %s", local_host, command)

    try:
        # pylint: disable=subprocess-run-check
        result = subprocess.run(shlex.split(command), **kwargs)     # nosec

    except subprocess.TimeoutExpired as error:
        # Raised if command times out
        log.debug(str(error))
        log.debug("  output: %s", error.output)
        log.debug("  stderr: %s", error.stderr)
        raise RunException(f"Command '{command}' exceed {timeout}s timeout") from error

    except subprocess.CalledProcessError as error:
        # Raised if command yields a non-zero return status with check=True
        log.debug(str(error))
        log.debug("  output: %s", error.output)
        log.debug("  stderr: %s", error.stderr)
        raise RunException(f"Command '{command}' returned non-zero status") from error

    except KeyboardInterrupt as error:
        # User Ctrl-C
        message = f"Command '{command}' interrupted by user"
        log.debug(message)
        raise RunException(message) from error

    except Exception as error:
        # Catch all
        message = f"Command '{command}' encountered unknown error"
        log.debug(message)
        log.debug(str(error))
        raise RunException(message) from error

    if capture_output and verbose:
        # Log the output of the command
        log.debug("  %s (rc=%s):", local_host, result.returncode)
        if result.stdout:
            for line in result.stdout.splitlines():
                log.debug("    %s", line)

    return result


def run_remote(log, hosts, command, verbose=True, timeout=120, task_debug=False):
    """Run the command on the remote hosts.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        command (str): command from which to obtain the output
        verbose (bool, optional): log the command output. Defaults to True.
        timeout (int, optional): number of seconds to wait for the command to complete.
            Defaults to 120 seconds.
        task_debug (bool, optional): whether to enable debug for the task object. Defaults to False.

    Returns:
        RemoteCommandResult: a grouping of the command results from the same hosts with the same
            return status

    """
    task = task_self()
    if task_debug:
        task.set_info('debug', True)
    # Enable forwarding of the ssh authentication agent connection
    task.set_info("ssh_options", "-oForwardAgent=yes")
    if verbose:
        log.debug("Running on %s with a %s second timeout: %s", hosts, timeout, command)
    task.run(command=command, nodes=hosts, timeout=timeout)
    results = RemoteCommandResult(command, task)
    if verbose:
        results.log_output(log)
    return results


def command_as_user(command, user):
    """Adjust a command to be ran as another user.

    Args:
        command (str): the original command
        user (str): user to run as

    Returns:
        str: command adjusted to run as another user

    """
    if not user:
        return command
    switch_command = " ".join(get_switch_user(user))
    return f"{switch_command} {command}"


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
