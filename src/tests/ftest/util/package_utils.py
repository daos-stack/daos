"""
(C) Copyright 2023-2024 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import re

# pylint: disable=import-error,no-name-in-module
from util.run_utils import command_as_user, run_remote


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
        CommandResult: the 'dnf install' command results
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
        CommandResult: the 'dnf remove' command results
    """
    log.info('Removing packages on %s: %s', hosts, ', '.join(packages))
    command = command_as_user(' '.join(['dnf', 'remove', '-y'] + packages), user)
    return run_remote(log, hosts, command, timeout=timeout)


class DaosVersion():
    """Class representing a DAOS version."""

    def __init__(self, version):
        """Initialize DaosVersion.

        Args:
            version (str/list): the major.minor.patch version

        Raises:
            ValueError: if the version is malformatted
        """
        if isinstance(version, DaosVersion):
            self.__version = repr(version)
        elif isinstance(version, (list, tuple)):
            self.__version = '.'.join(map(str, version))
        else:
            self.__version = str(version)

        try:
            version_match = re.match(r'^v?([0-9]+)\.([0-9]+)\.([0-9]+)', self.__version)
            self.major = version_match.group(1)
            self.minor = version_match.group(2)
            self.patch = version_match.group(3)
        except (AttributeError, IndexError) as error:
            raise ValueError(f'Invalid version string {self.__version}') from error

    def __str__(self):
        """Version as 'major.minor.patch'"""
        return f'{self.major}.{self.minor}.{self.patch}'

    def __repr__(self):
        """Full version used in init."""
        return self.__version

    def __iter__(self):
        """Version as [major, minor, patch]"""
        yield self.major
        yield self.minor
        yield self.patch

    def __eq__(self, other):
        """self == other"""
        return list(self) == list(DaosVersion(other))

    def __gt__(self, other):
        """self > other"""
        return list(self) > list(DaosVersion(other))

    def __ge__(self, other):
        """self >= other"""
        return list(self) >= list(DaosVersion(other))

    def __lt__(self, other):
        """self < other"""
        return list(self) < list(DaosVersion(other))

    def __le__(self, other):
        """self <= other"""
        return list(self) <= list(DaosVersion(other))
