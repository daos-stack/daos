"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from logging import getLogger
from socket import gethostname

from ClusterShell.NodeSet import NodeSet
from exception_utils import CommandFailure
from run_utils import command_as_user, get_clush_command, run_command, run_remote
from user_utils import get_chown_command, get_primary_group


def _get_result(hosts, command, timeout=15, verbose=True, sudo=False):
    """Get the result of executing the command on the specified hosts.

    Args:
        hosts (NodeSet): hosts on which to execute the command
        command (str): command to execute to get the result
        timeout (int, optional): command timeout. Defaults to 15.
        verbose (bool, optional): whether to log the command run and its output. Defaults to True.
        sudo (bool, optional): whether to run the command via sudo. Defaults to False.

    Returns:
        RemoteCommandResult: a grouping of the command results from the same hosts with the same
            return status
    """
    log = getLogger()
    if sudo:
        command = command_as_user(command, 'root')
    result = run_remote(log, hosts, command, verbose, timeout)
    return result


def _debug_no_space(result, directory):
    """Add some debug to log if a remote command failed due to 'No space'.

    Args:
        result (RemoteCommandResult): command result
        directory (str): directory of which to list the contents
    """
    log = getLogger()
    if not result.passed:
        debug_hosts = NodeSet()
        for hosts, stdout in result.all_stdout():
            if 'no space' in stdout.lower():
                debug_hosts.add(hosts)
        if debug_hosts:
            log.debug('### Checking space on %s ###', debug_hosts)
            run_remote(log, debug_hosts, 'df')
            run_remote(log, debug_hosts, f'ls -al {directory}')


def create_directory(hosts, directory, timeout=15, verbose=True, raise_exception=True, sudo=False):
    """Create the specified directory on the specified hosts.

    Args:
        hosts (NodeSet): hosts on which to create the directory
        directory (str): the directory to create
        timeout (int, optional): command timeout. Defaults to 15 seconds.
        verbose (bool, optional): whether to log the command run and stdout/stderr.
            Defaults to True.
        raise_exception (bool, optional): whether to raise an exception if the command returns a
            non-zero exit status. Defaults to True.
        sudo (bool, optional): whether to run the command via sudo. Defaults to False.

    Raises:
        CommandFailure: if there is an error running the command and an exception was requested

    Returns:
        RemoteCommandResult: a grouping of the command results from the same hosts with the same
            return status
    """
    mkdir_command = f'/usr/bin/mkdir -p {directory}'
    result = _get_result(hosts, mkdir_command, timeout, verbose, sudo)
    if not result.passed:
        _debug_no_space(result, directory)
        if raise_exception:
            raise CommandFailure(
                f"Error creating directory '{directory}' on {str(result.failed_hosts)} failed.")
    return result


def change_file_owner(hosts, filename, owner, group, timeout=15, verbose=True,
                      raise_exception=True, sudo=False):
    """Create the specified directory on the specified hosts.

    Args:
        hosts (NodeSet): hosts on which to create the directory
        filename (str): the file for which to change ownership
        owner (str): new owner of the file
        group (str): new group owner of the file
        timeout (int, optional): command timeout. Defaults to 15 seconds.
        verbose (bool, optional): whether to log the command run and its output. Defaults to True.
        raise_exception (bool, optional): whether to raise an exception if the command returns a
            non-zero exit status. Defaults to True.
        sudo (bool, optional): whether to run the command via sudo. Defaults to False.

    Raises:
        CommandFailure: if there is an error running the command and an exception was requested

    Returns:
        RemoteCommandResult: a grouping of the command results from the same hosts with the same
            return status
    """
    chown_command = get_chown_command(owner, group, file=filename)
    result = _get_result(hosts, chown_command, timeout, verbose, sudo)
    if not result.passed and raise_exception:
        raise CommandFailure(
            f"Error changing file owner of '{filename}' on {str(result.failed_hosts)} failed.")
    return result


def check_file_exists(hosts, filename, user=None, directory=False, sudo=False, timeout=15):
    """Check if a specified file exist on each specified hosts.

    If specified, verify that the file exists and is owned by the user.

    Args:
        hosts (NodeSet): hosts on which to run the command
        filename (str): file to check for the existence of on each host
        user (str, optional): owner of the file. Defaults to None.
        sudo (bool, optional): whether to run the command via sudo. Defaults to False.

    Returns:
        (bool, NodeSet): A tuple of:
            - True if the file exists on each of the hosts; False otherwise
            - A NodeSet of hosts on which the file does not exist
    """
    command = "test -e {0}".format(filename)
    if user is not None and not directory:
        command = "test -O {0}".format(filename)
    elif user is not None and directory:
        command = "test -O {0} && test -d {0}".format(filename)
    elif directory:
        command = "test -d '{0}'".format(filename)
    result = _get_result(hosts, command, timeout, True, sudo)
    return result.passed, result.failed_hosts


def distribute_files(hosts, source, destination, mkdir=True, timeout=60, verbose=True,
                     raise_exception=True, sudo=False, owner=None):
    """Copy the source to the destination on each of the specified hosts.

    Optionally (by default) ensure the destination directory exists on each of
    the specified hosts prior to copying the source.

    Args:
        hosts (NodeSet): hosts on which to copy the source
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
        CommandFailure: if there is an error running the command and an exception was requested

    Returns:
        RemoteCommandResult: a grouping of the command results from the same hosts with the same
            return status
    """
    result = None
    if mkdir:
        result = create_directory(
            hosts, os.path.dirname(destination), verbose=verbose, raise_exception=raise_exception)
    if result.passed:
        if sudo:
            # In order to copy a protected file to a remote host in CI the source will first be
            # copied as is to the remote host
            localhost = gethostname().split(".")[0]
            other_hosts = NodeSet.fromlist([host for host in hosts if host != localhost])
            if other_hosts:
                # Existing files with strict file permissions can cause the subsequent non-sudo
                # copy to fail, so remove the file first
                _get_result(other_hosts, f'rm -f {source}', timeout, verbose, True)
                result = distribute_files(
                    other_hosts, source, source, mkdir=True,
                    timeout=timeout, verbose=verbose,
                    raise_exception=raise_exception, sudo=False, owner=None)
            if result.passed:
                # Then a local sudo copy will be executed on the remote node to copy the source
                # to the destination
                log = getLogger()
                command = get_clush_command(
                    hosts, args="-S -v", command="cp {} {}".format(source, destination),
                    command_sudo=True)
                result = run_command(log, command, verbose, timeout)
                if not result.passed:
                    _debug_no_space(result, destination)
        else:
            # Without the sudo requirement copy the source to the destination
            # directly with clush
            log = getLogger()
            result = remote_file_copy(log, hosts, source, destination, verbose, timeout)
            if not result.passed:
                _debug_no_space(result, destination)

        # If requested update the ownership of the destination file
        if owner is not None and result.exit_status == 0:
            change_file_owner(
                hosts, destination, owner, get_primary_group(owner), timeout=timeout,
                verbose=verbose, raise_exception=raise_exception, sudo=sudo)
    return result


def remote_file_copy(log, hosts, source, destination, verbose=True, timeout=60):
    """Copy local files from this hosts to the destination on the remote hosts.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts to which the files will be copied
        source (str): the file to copy
        destination (str): the location to which the files will be copied
        timeout (int, optional): copy command timeout. Defaults to 60.

    Returns:
        RemoteCommandResult: a grouping of the command results from the same hosts with the same
            return status
    """
    command = f"clush -w {str(hosts)} -B -S -p -v --copy f'{source}' --dest '{destination}'"
    return run_command(log, command, verbose, timeout)


def reverse_remote_file_copy(log, hosts, source, destination, timeout=60):
    """Copy remote files from the specified hosts to the destination on this host.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts from which the files will be copied
        source (str): the remote files to copy
        destination (str): the local location to which the files will be copied
        timeout (int, optional): copy command timeout. Defaults to 60.

    Returns:
        RemoteCommandResult: a grouping of the command results from the same hosts with the same
            return status
    """
    command = f"clush -w {str(hosts)} -B -S -p -v --rcopy f'{source}' --dest '{destination}'"
    return run_command(log, command, True, timeout)
