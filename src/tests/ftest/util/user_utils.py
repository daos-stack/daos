"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from getpass import getuser
from grp import getgrgid
from pwd import getpwnam
import os

# pylint: disable=import-error,no-name-in-module
from run_utils import run_remote, RunException


def get_primary_group(user=None):
    """Get the name of the user's primary group.

    Args:
        user (str, optional): the user account name. Defaults to None, which uses the current user.

    Returns:
        str: the primary group name

    """
    try:
        gid = getpwnam(user or getuser()).pw_gid
        return getgrgid(gid).gr_name

    except KeyError:
        # User may not exist on this host, e.g. daos_server, so just return the user name
        return user or getuser()


def get_chown_command(user=None, group=None, options=None, file=None):
    """Get the chown command.

    Args:
        user (str, optional): the user account name. Defaults to None, which uses the current user.
        group (str, optional): the user group name. Defaults to None, which uses the primary group.
        options (str, optional): arguments to include with the chown command. Defaults to None.
        file (str, optional): the file for which to change the ownership. Defaults to None, which
            will end up yielding only the first half of the command.

    Returns:
        str: the chown command

    """
    command = ["chown", f"{user or getuser()}:{group or get_primary_group(user)}"]
    if options:
        command.insert(1, options)
    if file:
        command.append(file)
    return " ".join(command)


def create_group(log, nodes, group, sudo=False):
    """Create a group on remote nodes.

    Args:
        log (logger): logger for the messages produced by this method
        nodes (NodeSet): nodes on which to create the group
        group (str): the group to create
        sudo (bool): whether to execute commands with sudo

    Raises:
        RunException: if creation fails

    """
    _sudo = 'sudo ' if sudo else ''
    command = 'getent group {0} >/dev/null || {1}groupadd -r {0}'.format(group, _sudo)
    if not run_remote(log, nodes, command).passed:
        raise RunException('Failed to create group {} on nodes {}'.format(group, nodes))


def create_user(log, nodes, user, group=None, parent_dir=None, sudo=False):
    """Create a user.

    Args:
        log (logger): logger for the messages produced by this method
        nodes (NodeSet): nodes on which to create the user
        user (str): user to create
        group (str, optional): user group. Default is None
        parent_dir (str, optional): parent home directory
        sudo (bool): whether to execute commands with sudo

    Raises:
        RunException: if creation fails

    """
    _sudo = 'sudo ' if sudo else ''

    command = ' '.join(filter(None, [
        '{}useradd -m'.format(_sudo),
        '-g {}'.format(group) if group else None,
        '-d {}'.format(os.path.join(parent_dir, user)) if parent_dir else None,
        user]))
    if not run_remote(log, nodes, command).passed:
        raise RunException('Failed to create user {} on nodes {}'.format(user, nodes))


def delete_user(log, nodes, user, sudo=False):
    """Delete a user.

    Args:
        log (logger): logger for the messages produced by this method
        nodes (NodeSet): nodes on which to delete the user
        user (str): user to create
        group (str, optional): user group. Default is None
        parent_dir (str, optional): parent home directory
        sudo (bool): whether to execute commands with sudo

    Raises:
        RunException: if creation fails

    """
    _sudo = 'sudo ' if sudo else ''

    command = '{}userdel -f -r {}'.format(_sudo, user)
    if not run_remote(log, nodes, command).passed:
        raise RunException('Failed to delete user {} on nodes {}'.format(user, nodes))
