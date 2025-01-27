"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os

# pylint: disable=import-error,no-name-in-module
from util.host_utils import get_local_host
from util.run_utils import command_as_user, get_clush_command, run_local, run_remote
from util.user_utils import get_chown_command, get_primary_group


def __run_command(logger, hosts, command, verbose=True, timeout=15):
    """Run the command locally if there are no remote hosts or remotely.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        command (str): command to run
        verbose (bool, optional): log the command output. Defaults to True.
        timeout (int, optional): command timeout. Defaults to 15 seconds.

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    if not hosts.difference(get_local_host()):
        return run_local(logger, command, verbose, timeout)
    return run_remote(logger, hosts, command, verbose, timeout)


def create_directory(logger, hosts, directory, timeout=15, verbose=True, user=None):
    """Create the specified directory on the specified hosts.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to create the directory
        directory (str): the directory to create
        timeout (int, optional): command timeout. Defaults to 15 seconds.
        verbose (bool, optional): log the command output. Defaults to True.
        user (str, optional): user with which to run the command. Defaults to None.

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    command = command_as_user(f"mkdir -p {directory}", user)
    return __run_command(logger, hosts, command, verbose, timeout)


def change_file_owner(logger, hosts, filename, owner, group, timeout=15, verbose=True, user=None):
    """Create the specified directory on the specified hosts.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to create the directory
        filename (str): the file for which to change ownership
        owner (str): new owner of the file
        group (str): new group owner of the file
        timeout (int, optional): command timeout. Defaults to 15 seconds.
        verbose (bool, optional): log the command output. Defaults to True.
        user (str, optional): user with which to run the command. Defaults to None.

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    command = command_as_user(get_chown_command(owner, group, file=filename), user)
    return __run_command(logger, hosts, command, verbose, timeout)


def get_file_size(logger, host, file_name):
    """Obtain the file size on the specified host.

    Args:
        logger (Logger): logger for the messages produced by this method
        host (NodeSet): host from which to get the file size
        file_name (str): name of remote file

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    return __run_command(logger, host, f"stat -c%s {file_name}")


def distribute_files(logger, hosts, source, destination, mkdir=True, timeout=60,
                     verbose=True, sudo=False, owner=None):
    """Copy the source to the destination on each of the specified hosts.

    Optionally (by default) ensure the destination directory exists on each of
    the specified hosts prior to copying the source.

    Args:
        logger (Logger): logger for the messages produced by this method
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

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    result = None
    if mkdir:
        result = create_directory(logger, hosts, os.path.dirname(destination), timeout, verbose)

    if result is None or result.passed:
        if sudo:
            # In order to copy a protected file to a remote host in CI the source will first be
            # copied as is to the remote host
            other_hosts = hosts.difference(get_local_host())
            if other_hosts:
                # Existing files with strict file permissions can cause the subsequent non-sudo
                # copy to fail, so remove the file first
                _rm_command = command_as_user(f"rm -f {source}", "root")
                run_remote(logger, other_hosts, _rm_command, verbose, timeout)
                result = distribute_files(
                    logger, other_hosts, source, source, mkdir=True,
                    timeout=timeout, verbose=verbose, sudo=False, owner=None)

            if result is None or result.passed:
                # Then a local sudo copy will be executed on the remote node to copy the source
                # to the destination
                _cp_command = command_as_user(f"cp {source} {destination}", "root")
                result = run_remote(logger, hosts, _cp_command, verbose, timeout)
        else:
            # Without the sudo requirement copy the source to the destination directly with clush
            _command = get_clush_command(hosts, args=f"-S -v --copy {source} --dest {destination}")
            result = run_local(logger, _command, verbose, timeout)

        # If requested update the ownership of the destination file
        if owner is not None and result.passed:
            result = change_file_owner(
                logger, hosts, destination, owner, get_primary_group(owner), timeout, verbose,
                "root" if sudo else None)

    return result
