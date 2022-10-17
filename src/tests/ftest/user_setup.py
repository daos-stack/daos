#!/usr/bin/env python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import argparse
import logging
import sys

from ClusterShell.NodeSet import NodeSet

# pylint: disable=import-error,no-name-in-module
from util.logger_utils import get_console_handler
from util.run_utils import get_local_host, RunException
from util.user_utils import create_group, create_user, delete_user


# Set up a logger for the console messages
LOGGER = logging.getLogger(__name__)
LOGGER.setLevel(logging.DEBUG)
LOGGER.addHandler(get_console_handler("%(message)s", logging.INFO))


def create_users(nodes, users, group=None, parent_dir=None, sudo=False):
    '''Create some users.

    Args:
        nodes (NodeSet): nodes on which to create the users
        users (list): users to create
        group (str, optional): user group. Default is None
        parent_dir (str, optional): parent home directory
        sudo (bool): whether to execute commands with sudo

    Raises:
        RunException: if creation fails

    '''
    for user in users:
        # Delete if existing
        try:
            delete_user(LOGGER, nodes, user, sudo)
        except RunException:
            pass

        create_user(LOGGER, nodes, user, group, parent_dir, sudo)


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

    if args.group and not create_group(LOGGER, nodes, args.group, args.sudo):
        return 1

    if args.users and not create_users(nodes, args.users, args.group, args.dir, args.sudo):
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
