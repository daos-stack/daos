"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from getpass import getuser
from grp import getgrgid
from pwd import getpwnam
import os
import re
from collections import defaultdict

from ClusterShell.NodeSet import NodeSet

from run_utils import run_remote


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


def get_user_uid_gid(user):
    """Get a user's uid and gid

    Args:
        user (str, optional): the user account name. Defaults to None, which uses the current user.

    Returns:
        (str, str): the uid and gid

    """
    pwd = getpwnam(user or getuser())
    return pwd.pw_uid, pwd.pw_gid


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


def getent(log, hosts, database, key, sudo=False):
    """Run getent remotely.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        database (str): the administrative database
        key (str): the key/entry to check for
        sudo (bool): whether to execute commands with sudo

    Returns:
        RemoteCommandResult: result of run_remote()

    """
    command = ' '.join(filter(None, [
        'sudo -n' if sudo else None,
        'getent',
        database,
        key]))
    return run_remote(log, hosts, command)


def groupadd(log, hosts, group, force=False, sudo=False):
    """Run groupadd remotely.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        group (str): the group to create
        force (bool, optional): whether to use the force option. Default is False
        sudo (bool, optional): whether to execute commands with sudo. Default is False

    Returns:
        RemoteCommandResult: result of run_remote()

    """
    command = ' '.join(filter(None, [
        'sudo -n' if sudo else None,
        'groupadd',
        '-r',
        '-f' if force else None,
        group]))
    return run_remote(log, hosts, command)


def useradd(log, hosts, user, group=None, parent_dir=None, sudo=False):
    """Run useradd remotely.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        user (str): user to create
        group (str, optional): user group. Default is None
        parent_dir (str, optional): parent home directory. Default is None
        sudo (bool): whether to execute commands with sudo. Default is False

    Returns:
        RemoteCommandResult: result of run_remote()

    """
    command = ' '.join(filter(None, [
        'sudo -n' if sudo else None,
        'useradd',
        '-m',
        f'-g {group}' if group else None,
        f'-d {os.path.join(parent_dir, user)}' if parent_dir else None,
        user]))
    return run_remote(log, hosts, command)


def userdel(log, hosts, user, sudo=False):
    """Run userdel remotely.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        user (str): user to create
        sudo (bool): whether to execute commands with sudo. Default is False

    Returns:
        RemoteCommandResult: result of run_remote()

    """
    command = ' '.join(filter(None, [
        'sudo -n' if sudo else None,
        'userdel',
        '-f',
        '-r',
        user]))
    return run_remote(log, hosts, command)


def get_group_id(log, hosts, group, sudo=False):
    """Get a group's id on remote nodes.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        group (str): group to get id of
        sudo (bool): whether to execute commands with sudo. Default is False

    Returns:
        dict: gid:NodeSet mapping for each gid, where gid is None if non-existent

    """
    gids = defaultdict(NodeSet)
    result = getent(log, hosts, 'group', group, sudo)
    for data in result.output:
        if data.returncode == 0:
            gid = re.findall(r'.*:.*:(.*):.*', '\n'.join(data.stdout))[0]
        else:
            gid = None
        gids[gid].add(data.hosts)
    return dict(gids)


def get_user_groups(log, hosts, user):
    """Get a user's groups on remote nodes.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        user (str): user to get groups of

    Returns:
        dict: groups:NodeSet mapping for each groups value, where groups is None if the user
            does not exist

    """
    groups = defaultdict(NodeSet)
    result = run_remote(log, hosts, f'id {user}')
    for data in result.output:
        if data.returncode == 0:
            group = re.findall(r'groups=([0-9,]*)', '\n'.join(data.stdout))[0]
        else:
            group = None
        groups[group].add(data.hosts)
    return dict(groups)
