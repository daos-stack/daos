"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import getpass
import os
import tempfile

from general_utils import DaosTestError, distribute_files
from run_utils import command_as_user, run_remote


class SystemctlFailure(Exception):
    """Base exception for this module."""


def get_systemctl_command(unit_command, service, user="root"):
    """Get the systemctl command for the specified inputs.

    Args:
        unit_command (str): command to issue for the service
        service (str): service for which to issue the command
        user (str, optional): user to use to issue the command. Defaults to "root".

    Returns:
        str: the systemctl command for the specified service and user
    """
    command = ["systemctl"]
    if user != "root":
        command.append(f"--user {user}")
    if unit_command:
        command.append(unit_command)
    if service:
        command.append(service)
    return " ".join(command)


def get_service_file(logger, hosts, service, user, verbose=True, timeout=120):
    """Get the service file.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        service (str): service for which to issue the command
        user (str, optional): user to use to issue the command. Defaults to "root".
        verbose (bool, optional): log the command output. Defaults to True.
        timeout (int, optional): number of seconds to wait for the command to complete.
            Defaults to 120 seconds.

    Raises:
        SystemctlFailure: if there is a problem obtaining the service file

    Returns:
        str: the service file
    """
    command = ' | '.join([
        get_systemctl_command("status", service, user),
        "grep 'Loaded:'",
        "grep -oE '/.*service'",
        "xargs -r sh -c '[ -e \"$0\" ] && echo \"$0\"'"
    ])
    result = run_remote(logger, hosts, command, verbose, timeout)
    if not result.passed:
        raise SystemctlFailure("Error obtaining the service file path")
    if not result.homogeneous:
        raise SystemctlFailure("Error obtaining a homogeneous service file path")
    return list(result.all_stdout.values())[0].strip()


def create_override_config(logger, hosts, service, user, service_command, service_config, test_env,
                           verbose=True, timeout=120):
    """Create a systemctl override config file.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        service (str): service for which to issue the command
        user (str): user to use to issue the command
        service_command (str): full path to the service command
        service_config (str): full path to the service config
        test_env (TestEnvironment):
        verbose (bool, optional): log the command output. Defaults to True.
        timeout (int, optional): number of seconds to wait for the command to complete.
            Defaults to 120 seconds.

    Raises:
        SystemctlFailure: if there are problems detecting, creating, or distributing the systemctl
            override config file

    Returns:
        str: the systemctl override config file path
    """
    # Get the existing service file
    service_file = get_service_file(logger, hosts, service, user, verbose, timeout)

    # Create the override directory
    override_directory = f"{service_file}.d"
    command = command_as_user(f"mkdir -p {override_directory}", user)
    result = run_remote(logger, hosts, command, verbose, timeout)
    if not result.passed:
        raise SystemctlFailure("Error creating the systemctl override config directory")

    # Create the override file
    override_file = os.path.join(override_directory, "override.conf")
    override_contents = [
        "[Service]",
        "ExecStart=",
        f"ExecStart={service_command} start -o {service_config}"
    ]
    if test_env.systemd_path:
        override_contents.append(f'Environment="PATH={test_env.systemd_path}"')
    if test_env.systemd_ld_library_path:
        override_contents.append(f'Environment="PATH={test_env.systemd_ld_library_path}"')
    with tempfile.NamedTemporaryFile() as temp:
        temp.write(bytes('\n'.join(override_contents) + '\n', encoding='utf-8'))
        temp.flush()
        os.chmod(temp.name, 0o644)
        _sudo = user != getpass.getuser()
        _owner = user if _sudo else None
        try:
            distribute_files(
                hosts, temp.name, override_file, mkdir=False,
                verbose=verbose, raise_exception=True, sudo=_sudo, owner=_owner)
        except DaosTestError as error:
            raise SystemctlFailure(
                "Error distributing the systemctl override config directory") from error

    # Reload on all hosts to pick up changes
    if not daemon_reload(logger, hosts, user, verbose, timeout).passed:
        raise SystemctlFailure("Error reloading systemctl daemon with override config directory")

    return override_file


def daemon_reload(logger, hosts, user, verbose=True, timeout=120):
    """Run systemctl daemon-reload.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        user (str, optional): user to use to issue the command. Defaults to "root".
        verbose (bool, optional): log the command output. Defaults to True.
        timeout (int, optional): number of seconds to wait for the command to complete.
            Defaults to 120 seconds.

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    command = get_systemctl_command("daemon-reload", None, user)
    return run_remote(logger, hosts, command, verbose, timeout)
