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
    command = _sudo(sudo, 'groupadd {} -f'.format(group))
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
        check_cmd = 'id -u {} &> /dev/null'.format(user)
        delete_cmd = _sudo(sudo, 'userdel -f -r {}'.format(user))
        command = 'if $({}); then {}; fi'.format(check_cmd, delete_cmd)
        if not run_remote(LOGGER, nodes, command).passed:
            return False

        # Create new user
        command = ' '.join(filter(None, [
            _sudo(sudo, 'useradd -m'),
            '-g {}'.format(group) if group else None,
            '-d {}'.format(os.path.join(parent_dir, user)) if parent_dir else None,
            user
        ]))
        if not run_remote(LOGGER, nodes, command).passed:
            return False
    return True


def main():
    '''Set up test env users.

    Returns:
        int: 0 on success. positive int on failure
    '''
    logging.basicConfig(
        format="%(asctime)s %(levelname)-5s %(message)s",
        datefmt=r"%Y/%m/%d %I:%M:%S", level=logging.DEBUG)

    parser = argparse.ArgumentParser(prog="user_setup.py")

    parser.add_argument(
        '-n', '--nodes',
        type=str,
        help="Comma-separated list of nodes to create users on")
    parser.add_argument(
        '-u', '--users',
        type=str,
        required=True,
        help="Comma-separated list of user accounts to create")
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

    nodes = NodeSet(args.nodes or get_local_host())
    users = args.users.split(',') if args.users else None

    if args.group and not create_group(nodes, args.group, args.sudo):
        return 1

    if users and not create_users(nodes, users, args.group, args.dir, args.sudo):
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
