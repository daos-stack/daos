"""
(C) Copyright 2018-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from run_utils import run_remote, command_as_user


def install_packages(logger, nodes, packages, user=None, timeout=600):
    """Install the packages on the nodes.

    Args:
        nodes (NodeSet): nodes on which to install the packages
        packages (list): a list of packages to install
        user (str, optional): user to user when installing the packages. Defaults to None.
        timeout (int, optional): timeout for the dnf install command. Defaults to 600.

    Returns:
        RemoteCommandResult: the 'dnf install' command results
    """
    logger.info('Installing packages on %s: %s', nodes, ', '.join(packages))
    command = command_as_user(user, ' '.join(['dnf', 'install', '-y'] + packages))
    return run_remote(logger, nodes, command, timeout=timeout)


def remove_packages(logger, nodes, packages, user=None, timeout=600):
    """Remove the packages on the nodes.

    Args:
        nodes (NodeSet): nodes on which to remove the packages
        packages (list): a list of packages to remove
        user (str, optional): user to user when removing the packages. Defaults to None.
        timeout (int, optional): timeout for the dnf remove command. Defaults to 600.

    Returns:
        RemoteCommandResult: the 'dnf remove' command results
    """
    logger.info('Removing packages on %s: %s', nodes, ', '.join(packages))
    command = command_as_user(user, ' '.join(['dnf', 'remove', '-y'] + packages))
    return run_remote(logger, nodes, command, timeout=timeout)
