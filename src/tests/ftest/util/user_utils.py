"""
  (C) Copyright 2018-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re
from collections import defaultdict
from getpass import getuser
from grp import getgrgid
from pwd import getpwnam

from ClusterShell.NodeSet import NodeSet
# pylint: disable=import-error,no-name-in-module
from util.run_utils import command_as_user, run_remote


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


def getent(log, hosts, database, key, run_user=None):
    """Run getent remotely.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        database (str): the administrative database
        key (str): the key/entry to check for
        run_user (str, optional): user to run the command as.
            Default is None, which runs as the current user

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    command = ' '.join(filter(None, [
        'getent',
        database,
        key]))
    return run_remote(log, hosts, command_as_user(command, run_user))


def groupadd(log, hosts, group, force=False, run_user="root"):
    """Run groupadd remotely.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        group (str): the group to create
        force (bool, optional): whether to use the force option. Default is False
        run_user (str, optional): user to run the command as. Default is root

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    command = ' '.join(filter(None, [
        'groupadd',
        '-r',
        '-f' if force else None,
        group]))
    return run_remote(log, hosts, command_as_user(command, run_user))


def groupdel(log, hosts, group, force=False, run_user="root"):
    """Run groupdel remotely.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        group (str): the group to delete
        force (bool, optional): whether to use the force option. Default is False
        run_user (str, optional): user to run the command as. Default is root

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    command = ' '.join(filter(None, [
        'groupdel',
        '-f' if force else None,
        group]))
    return run_remote(log, hosts, command_as_user(command, run_user))


def useradd(log, hosts, user, group=None, parent_dir=None, run_user="root"):
    """Run useradd remotely.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        user (str): user to create
        group (str, optional): user group. Default is None
        parent_dir (str, optional): parent home directory. Default is None
        run_user (str, optional): user to run the command as. Default is root

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    command = ' '.join(filter(None, [
        'useradd',
        '-m',
        f'-g {group}' if group else None,
        f'-d {os.path.join(parent_dir, user)}' if parent_dir else None,
        user]))
    return run_remote(log, hosts, command_as_user(command, run_user))


def userdel(log, hosts, user, run_user="root"):
    """Run userdel remotely.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        user (str): user to create
        run_user (str, optional): user to run the command as. Default is root

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    command = ' '.join(filter(None, [
        'userdel',
        '-f',
        '-r',
        user]))
    return run_remote(log, hosts, command_as_user(command, run_user))


def usermod(log, hosts, login, groups, run_user="root"):
    """Run usermod remotely.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        login (str): login username
        groups (list): list of new groups
        run_user (str, optional): user to run the command as. Default is root

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    command = ' '.join(filter(None, [
        'usermod',
        f'-G {",".join(groups)}',
        login]))
    return run_remote(log, hosts, command_as_user(command, run_user))


def get_group_id(log, hosts, group, run_user=None):
    """Get a group's id on remote nodes.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        group (str): group to get id of
        run_user (str, optional): user to run the command as.
            Default is None, which runs as the current user

    Returns:
        dict: gid:NodeSet mapping for each gid, where gid is None if non-existent

    """
    gids = defaultdict(NodeSet)
    result = getent(log, hosts, 'group', group, run_user)
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
