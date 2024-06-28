"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from getpass import getuser
from logging import getLogger

from ClusterShell.NodeSet import NodeSet
from host_utils import get_local_host
from run_utils import command_as_user, get_clush_command, run_local_new, run_remote
from user_utils import get_chown_command, get_primary_group


def create_directory(hosts, directory, timeout=15, verbose=True, user=None):
    """Create the specified directory on the specified hosts.

    Args:
        hosts (NodeSet): hosts on which to create the directory
        directory (str): the directory to create
        timeout (int, optional): command timeout. Defaults to 15 seconds.
        verbose (bool, optional): log the command output. Defaults to True.
        user (str, optional): user with which to run the command. Defaults to None.

    Returns:
        RemoteCommandResult: a grouping of the command results from the same hosts with the same
            return status
    """
    log = getLogger()
    command = command_as_user(f"/usr/bin/mkdir -p {directory}", user)
    return run_remote(log, hosts, command, verbose, timeout)


def change_file_owner(hosts, filename, owner, group, timeout=15, verbose=True, user=None):
    """Create the specified directory on the specified hosts.

    Args:
        hosts (NodeSet): hosts on which to create the directory
        filename (str): the file for which to change ownership
        owner (str): new owner of the file
        group (str): new group owner of the file
        timeout (int, optional): command timeout. Defaults to 15 seconds.
        verbose (bool, optional): log the command output. Defaults to True.
        user (str, optional): user with which to run the command. Defaults to None.

    Returns:
        RemoteCommandResult: a grouping of the command results from the same hosts with the same
            return status
    """
    log = getLogger()
    command = command_as_user(get_chown_command(owner, group, file=filename), user)
    return run_remote(log, hosts, command, verbose, timeout)


def distribute_files(hosts, source, destination, mkdir=True, timeout=60, verbose=True, user=None,
                     owner=None):
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
        user (str, optional): user with which to run the command. Defaults to None.
        owner (str, optional): if specified the owner to assign as the owner of
            the copied file. Defaults to None.

    Raises:
        RunException: if there is an error running the command and raise_exception=True

    Returns:
        subprocess.CompletedProcess: an object representing the result of the command execution with
            the following properties:
                - args (the command argument)
                - returncode
                - stdout (only set if capture_output=True)
                - stderr (not used; included in stdout)
    """
    if user != getuser():
        return __distribute_protected_files(
            hosts, source, destination, mkdir, timeout, verbose, user, owner)
    return __distribute_files(hosts, source, destination, mkdir, timeout, verbose, user, owner)


def __distribute_files(hosts, source, destination, mkdir=True, timeout=60, verbose=True, user=None,
                       owner=None):
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
        user (str, optional): user with which to run the command. Defaults to None.
        owner (str, optional): if specified the owner to assign as the owner of
            the copied file. Defaults to None.

    Raises:
        RunException: if there is an error running the command and raise_exception=True

    Returns:
        subprocess.CompletedProcess: an object representing the result of the command execution with
            the following properties:
                - args (the command argument)
                - returncode
                - stdout (only set if capture_output=True)
                - stderr (not used; included in stdout)
    """
    log = getLogger()
    if mkdir:
        result = create_directory(hosts, os.path.dirname(destination), verbose=verbose)
        if not result.passed:
            return result

    # Copy the source to the destination directly with clush
    command = get_clush_command(
        hosts, args=f"-S -v --copy {source} --dest {destination}", user=user)
    result = run_local_new(log, command, verbose, timeout)
    if not result.passed:
        return result

    # If requested update the ownership of the destination file
    if owner is not None:
        change_file_owner(
            hosts, destination, owner, get_primary_group(owner), timeout=timeout,
            verbose=verbose, user=user)
    return result


def __distribute_protected_files(hosts, source, destination, mkdir=True, timeout=60, verbose=True,
                                 user=None, owner=None):
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
        user (str, optional): user with which to run the command. Defaults to None.
        owner (str, optional): if specified the owner to assign as the owner of
            the copied file. Defaults to None.

    Raises:
        RunException: if there is an error running the command and raise_exception=True

    Returns:
        subprocess.CompletedProcess: an object representing the result of the command execution with
            the following properties:
                - args (the command argument)
                - returncode
                - stdout (only set if capture_output=True)
                - stderr (not used; included in stdout)
    """
    log = getLogger()
    if mkdir:
        result = create_directory(hosts, os.path.dirname(destination), verbose=verbose)
        if not result.passed:
            return result

    # In order to copy a protected file to a remote host in CI the source will first be copied as
    # is to the remote host
    localhost = get_local_host()
    other_hosts = NodeSet.fromlist([host for host in hosts if host != localhost])
    if other_hosts:
        # Existing files with strict file permissions can cause the
        # subsequent non-sudo copy to fail, so remove the file first
        rm_command = command_as_user(f"rm -f {source}", "root")
        run_remote(log, other_hosts, rm_command, verbose)
        result = __distribute_files(
            other_hosts, source, source, mkdir=True, timeout=timeout, verbose=verbose,
            user=user, owner=None)
        if not result.passed:
            return result

    # Then a local sudo copy will be executed on the remote node to
    # copy the source to the destination
    command = command_as_user(f"cp {source} {destination}", 'root')
    result = run_remote(log, hosts, command, verbose, timeout)
    if not result.passed:
        return result

    # If requested update the ownership of the destination file
    if owner is not None:
        change_file_owner(
            hosts, destination, owner, get_primary_group(owner), timeout=timeout,
            verbose=verbose, user=user)
    return result
