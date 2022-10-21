"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from getpass import getuser
from grp import getgrgid
from pwd import getpwnam


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
