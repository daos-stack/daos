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
from util.exception_utils import CommandFailure
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


def get_next_uid_gid(log, hosts):
    """Get the next common UID and GID across some hosts.
    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
    Returns:
        (int, int): next UID, next GID common across hosts
    Raises:
        CommandFailure: if the command fails on one or more hosts
        ValueError: if the command output is unexpected on one or more hosts
    """
    command = '''
UID_MIN=$(grep -E '^UID_MIN' /etc/login.defs | tr -s ' ' | cut -d ' ' -f 2)
UID_MAX=$(grep -E '^UID_MAX' /etc/login.defs | tr -s ' ' | cut -d ' ' -f 2)
GID_MIN=$(grep -E '^GID_MIN' /etc/login.defs | tr -s ' ' | cut -d ' ' -f 2)
GID_MAX=$(grep -E '^GID_MAX' /etc/login.defs | tr -s ' ' | cut -d ' ' -f 2)
NEXT_UID=$(cat /etc/passwd | cut -d ":" -f 3 | xargs -n 1 -I % sh -c \
    "if [[ % -ge $UID_MIN ]] && [[ % -le $UID_MAX ]]; then echo %; fi" \
        | sort -n | tail -n 1 | awk '{ print $1+1 }')
NEXT_GID=$(cat /etc/group | cut -d ":" -f 3 | xargs -n 1 -I % sh -c \
    "if [[ % -ge $GID_MIN ]] && [[ % -le $GID_MAX ]]; then echo %; fi" \
        | sort -n | tail -n 1 | awk '{ print $1+1 }')
echo "NEXT_UID=$NEXT_UID"
echo "NEXT_GID=$NEXT_GID"
'''
    result = run_remote(log, hosts, command)
    if not result.passed:
        raise CommandFailure(f"Failed to get NEXT_UID and NEXT_GID on {result.failed_hosts}")
    all_output = "\n".join(result.all_stdout.values())
    all_uid = re.findall(r'NEXT_UID=([0-9]+)', all_output)
    all_gid = re.findall(r'NEXT_GID=([0-9]+)', all_output)
    if len(all_uid) != len(hosts) or len(all_gid) != len(hosts):
        raise ValueError(f"Failed to get NEXT_UID and NEXT_GID on {hosts}")
    max_uid = max(map(int, all_uid))
    max_gid = max(map(int, all_gid))
    return max_uid, max_gid


def groupadd(log, hosts, group, gid=None, force=False, run_user="root"):
    """Run groupadd remotely.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        group (str): the group to create
        gid (int, optional): GID for the new group. Defaults to None
        force (bool, optional): whether to use the force option. Default is False
        run_user (str, optional): user to run the command as. Default is root

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    command = ' '.join(filter(None, [
        'groupadd',
        '-r',
        f'-g {gid}' if gid else None,
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


def useradd(log, hosts, user, group=None, parent_dir=None, uid=None, run_user="root"):
    """Run useradd remotely.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        user (str): user to create
        group (str, optional): user group. Default is None
        parent_dir (str, optional): parent home directory. Default is None
        uid (int, optional): UID for the new user. Defaults to None
        run_user (str, optional): user to run the command as. Default is root

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    command = ' '.join(filter(None, [
        'useradd',
        '-m',
        f'-g {group}' if group else None,
        f'-d {os.path.join(parent_dir, user)}' if parent_dir else None,
        f'-u {uid}' if uid else None,
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
