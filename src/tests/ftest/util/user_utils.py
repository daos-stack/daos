"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from getpass import getuser
from grp import getgrgid
from pwd import getpwnam
import os


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


def get_getent_command(database, key, sudo=False):
    """Get a getent command string.

    Args:
        database (str): the administrative database
        key (str): the key/entry to check for
        sudo (bool): whether to execute commands with sudo

    Returns:
        str: the getent command

    """
    _sudo = 'sudo -n ' if sudo else ''
    return f'{_sudo}getent {database} {key}'


def get_groupadd_command(group, force=False, sudo=False):
    """Get a groupadd command string.

    Args:
        group (str): the group to create
        force (bool, optional): whether to use the force option. Default is False
        sudo (bool, optional): whether to execute commands with sudo. Default is False

    Returns:
        str: the groupadd command

    """
    _sudo = 'sudo -n ' if sudo else ''
    _force = '-f ' if force else ''
    return f'{_sudo}groupadd -r {_force}{group}'


def get_useradd_command(user, group=None, parent_dir=None, sudo=False):
    """Get a useradd command string.

    Args:
        user (str): user to create
        group (str, optional): user group. Default is None
        parent_dir (str, optional): parent home directory. Default is None
        sudo (bool): whether to execute commands with sudo. Default is False

    Returns:
        str: the useradd command

    """
    _sudo = 'sudo -n ' if sudo else ''
    return ' '.join(filter(None, [
        f'{_sudo}useradd -m',
        f'-g {group}' if group else None,
        f'-d {os.path.join(parent_dir, user)}' if parent_dir else None,
        user]))


def get_userdel_command(user, sudo=False):
    """Get a userdel command string.

    Args:
        user (str): user to create
        sudo (bool): whether to execute commands with sudo. Default is False

    Returns:
        str: the userdel command

    """
    _sudo = 'sudo -n ' if sudo else ''
    return f'{_sudo}userdel -f -r {user}'
