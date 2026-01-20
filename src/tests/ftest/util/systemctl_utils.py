"""
  (C) Copyright 2018-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import getpass
import os
import re
import tempfile

from ClusterShell.NodeSet import NodeSet
# pylint: disable=import-error,no-name-in-module
from util.file_utils import create_directory, distribute_files
from util.run_utils import command_as_user, run_remote


class SystemctlFailure(Exception):
    """Base exception for this module."""


def get_service_status(logger, hosts, service, user="root"):
    """Get the status of the daos_server.service.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to get the service state
        service (str): name of the service
        user (str, optional): user to use to issue the command. Defaults to "root".

    Returns:
        dict: a dictionary with the following keys:
            - "status":       boolean set to True if status was obtained; False otherwise
            - "stop":         NodeSet where to stop the daos_server.service
            - "disable":      NodeSet where to disable the daos_server.service
            - "reset-failed": NodeSet where to reset the daos_server.service

    """
    status = {
        "status": True,
        "stop": NodeSet(),
        "disable": NodeSet(),
        "reset-failed": NodeSet()}
    status_states = {
        "stop": ["active", "activating", "deactivating"],
        "disable": ["active", "activating", "deactivating"],
        "reset-failed": ["failed"]}
    command = get_systemctl_command("is-active", service, user)
    result = run_remote(logger, hosts, command, False)
    for data in result.output:
        if data.timeout:
            status["status"] = False
            status["stop"].add(data.hosts)
            status["disable"].add(data.hosts)
            status["reset-failed"].add(data.hosts)
            logger.debug("  %s: TIMEOUT", data.hosts)
            break
        logger.debug("  %s: %s", data.hosts, "\n".join(data.stdout))
        for key, state_list in status_states.items():
            for line in data.stdout:
                if line in state_list:
                    status[key].add(data.hosts)
                    break
    return status


def stop_service(logger, hosts, service, user="root", retries=2):
    """Stop any daos_server.service running on the hosts running servers.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): list of hosts on which to stop the service.
        service (str): name of the service
        user (str, optional): user to use to issue the command. Defaults to "root".
        retries (int, optional): number of times to retry stopping the service. Defaults to 2.

    Returns:
        bool: True if the service was successfully stopped; False otherwise

    """
    if not hosts:
        logger.debug("  Skipping stopping %s service - no hosts", service)
        return True

    result = {"status": True}
    status_keys = ["reset-failed", "stop", "disable"]
    mapping = {"stop": "active", "disable": "enabled", "reset-failed": "failed"}
    check_hosts = NodeSet(hosts)
    loop = 1
    while check_hosts:
        # Check the status of the service on each host
        result = get_service_status(logger, check_hosts, service)
        check_hosts = NodeSet()
        for key in status_keys:
            if result[key]:
                if loop == retries:
                    # Exit the while loop if the service is still running
                    logger.error(
                        " - Error %s still %s on %s", service, mapping[key], result[key])
                    result["status"] = False
                else:
                    # Issue the appropriate systemctl command to remedy the
                    # detected state, e.g. 'stop' for 'active'.
                    command = command_as_user(get_systemctl_command(key, service, user), user)
                    run_remote(logger, result[key], command)

                    # Run the status check again on this group of hosts
                    check_hosts.add(result[key])
        loop += 1

    return result["status"]


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
        get_systemctl_command("show", service, user),
        "grep 'FragmentPath='",
    ])
    result = run_remote(logger, hosts, command, verbose, timeout)
    if not result.passed:
        raise SystemctlFailure("Error obtaining the service file path")
    if not result.homogeneous:
        raise SystemctlFailure("Error obtaining a homogeneous service file path")
    try:
        return re.findall(r'FragmentPath=(.+)', result.joined_stdout)[0]
    except IndexError as error:
        raise SystemctlFailure("Error parsing the service file path") from error


def create_override_config(logger, hosts, service, user, service_command, service_config, path,
                           ld_library_path, verbose=True, timeout=120):
    """Create a systemctl override config file.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        service (str): service for which to issue the command
        user (str): user to use to issue the command
        service_command (str): full path to the service command
        service_config (str): full path to the service config
        path (str): the PATH variable to set in the systemd config.
        ld_library_path (str): the LD_LIBRARY_PATH variable to set in the systemd config.
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
    override_file = os.path.join(f"{service_file}.d", "override.conf")
    result = create_directory(logger, hosts, os.path.dirname(override_file), timeout, verbose, user)
    if not result.passed:
        raise SystemctlFailure("Error creating the systemctl override config directory")

    # Create the override file - empty ExecStart clears the existing setting
    override_contents = [
        "[Service]",
        "ExecStart=",
        f"ExecStart={service_command} start -o {service_config}"
    ]
    if path:
        override_contents.append(f'Environment="PATH={path}"')
    if ld_library_path:
        override_contents.append(f'Environment="LD_LIBRARY_PATH={ld_library_path}"')
    override_contents = "\n".join(override_contents) + "\n"

    with tempfile.NamedTemporaryFile() as temp:
        temp.write(bytes(override_contents, encoding='utf-8'))
        temp.flush()
        os.chmod(temp.name, 0o644)

        _sudo = user != getpass.getuser()
        _owner = user if _sudo else None

        result = distribute_files(
            logger, hosts, temp.name, override_file, mkdir=False, verbose=verbose, sudo=_sudo,
            owner=_owner)
        if not result.passed:
            raise SystemctlFailure(
                "Error distributing the systemctl override config directory")

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
    return run_remote(logger, hosts, command_as_user(command, user), verbose, timeout)
