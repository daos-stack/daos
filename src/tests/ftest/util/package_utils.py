"""
(C) Copyright 2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from run_utils import run_remote, command_as_user


def find_packages(log, hosts, pattern, user=None):
    """Get the installed packages on each specified host.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to search for installed packages
        pattern (str): grep pattern to use to search for installed packages
        user (str, optional): user account to use to run the search command. Defaults to None.

    Returns:
        dict: a dictionary of host keys with a list of installed RPM values
    """
    installed = {}
    command = command_as_user(f"rpm -qa | grep -E {pattern} | sort -n", user)
    result = run_remote(log, hosts, command)
    for data in result.output:
        if data.passed:
            installed[str(data.hosts)] = data.stdout or []
    return installed


def install_packages(log, hosts, packages, user=None, timeout=600):
    """Install the packages on the hosts.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to install the packages
        packages (list): a list of packages to install
        user (str, optional): user to use when installing the packages. Defaults to None.
        timeout (int, optional): timeout for the dnf install command. Defaults to 600.

    Returns:
        RemoteCommandResult: the 'dnf install' command results
    """
    log.info('Installing packages on %s: %s', hosts, ', '.join(packages))
    command = command_as_user(' '.join(['dnf', 'install', '-y'] + packages), user)
    return run_remote(log, hosts, command, timeout=timeout)


def remove_packages(log, hosts, packages, user=None, timeout=600):
    """Remove the packages on the hosts.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to remove the packages
        packages (list): a list of packages to remove
        user (str, optional): user to use when removing the packages. Defaults to None.
        timeout (int, optional): timeout for the dnf remove command. Defaults to 600.

    Returns:
        RemoteCommandResult: the 'dnf remove' command results
    """
    log.info('Removing packages on %s: %s', hosts, ', '.join(packages))
    command = command_as_user(' '.join(['dnf', 'remove', '-y'] + packages), user)
    return run_remote(log, hosts, command, timeout=timeout)
