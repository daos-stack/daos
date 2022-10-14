#!/usr/bin/env python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import argparse
import logging
import os
import sys

from ClusterShell.NodeSet import NodeSet

# pylint: disable=import-error,no-name-in-module
from util.logger_utils import get_console_handler
from util.run_utils import run_remote, get_local_host


# Set up a logger for the console messages
LOGGER = logging.getLogger(__name__)
LOGGER.setLevel(logging.DEBUG)
LOGGER.addHandler(get_console_handler("%(message)s", logging.INFO))


def _sudo(sudo, command):
    '''Optionally prepend sudo to a command string.

    Args:
        sudo (bool): whether to prepend sudo
        command (str): the original command

    Returns:
        str: original command with sudo optionally prepended

    '''
    return ('sudo ' if sudo else '') + command


def create_group(nodes, group, sudo=False):
    '''Create a group on remote nodes.

    Args:
        nodes (NodeSet): nodes on which to create the group
        group (str): the group to create
        sudo (bool): whether to execute commands with sudo

    Returns:
        bool: whether the command was successful on all nodes
    '''
    check_cmd = f'getent group {group} >/dev/null'
    create_cmd = _sudo(sudo, f'groupadd -r {group}')
    command = f'{check_cmd} || {create_cmd}'
    return run_remote(LOGGER, nodes, command).passed


def create_users(nodes, users, group=None, parent_dir=None, sudo=False):
    '''Create some users.

    Args:
        nodes (NodeSet): nodes on which to create the users
        users (list): users to create
        group (str, optional): user group. Default is None
        parent_dir (str, optional): parent home directory
        sudo (bool): whether to execute commands with sudo

    Returns:
        bool: whether the command was successful on all nodes
    '''
    for user in users:
        # Delete if existing
        check_cmd = f'id -u {user} &> /dev/null'
        delete_cmd = _sudo(sudo, f'userdel -f -r {user}')
        command = f'if $({check_cmd}); then {delete_cmd}; fi'
        if not run_remote(LOGGER, nodes, command).passed:
            return False

        # Create new user
        command = ' '.join(filter(None, [
            _sudo(sudo, 'useradd -m'),
            f'-g {group}' if group else None,
            f'-d {os.path.join(parent_dir, user)}' if parent_dir else None,
            user
        ]))
        if not run_remote(LOGGER, nodes, command).passed:
            return False
    return True


def main():
    '''Set up test env users.

    Returns:
        int: 0 on success. 1 on failure
    '''
    logging.basicConfig(
        format="%(asctime)s %(levelname)-5s %(message)s",
        datefmt=r"%Y/%m/%d %I:%M:%S", level=logging.DEBUG)

    parser = argparse.ArgumentParser(prog="user_setup.py")

    parser.add_argument(
        'users',
        type=str,
        nargs='+',
        help="User accounts to create")
    parser.add_argument(
        '-n', '--nodes',
        type=str,
        default=get_local_host(),
        help="Comma-separated list of nodes to create users on. Defaults to localhost")
    parser.add_argument(
        "-g", "--group",
        type=str,
        help="Group to add users to. Will be created if non-existent")
    parser.add_argument(
        "-d", "--dir",
        type=str,
        help="Parent home directory for users")
    parser.add_argument(
        "-s", "--sudo",
        action="store_true",
        help="Run all commands with privileges")

    args = parser.parse_args()
    logging.info("Arguments: %s", args)

    nodes = NodeSet(args.nodes)

    if args.group and not create_group(nodes, args.group, args.sudo):
        return 1

    if args.users and not create_users(nodes, args.users, args.group, args.dir, args.sudo):
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
