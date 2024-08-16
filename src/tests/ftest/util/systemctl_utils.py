"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import getpass
import os
import tempfile

from host_utils import get_local_host
from run_utils import command_as_user, get_clush_command, run_local, run_remote
from user_utils import get_chown_command, get_primary_group


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
    return run_remote(logger, hosts, command, verbose, timeout)


def distribute_files(logger, hosts, source, destination, mkdir=True, timeout=60,
                     verbose=True, sudo=False, owner=None):
    """Copy the source to the destination on each of the specified hosts.

    Optionally (by default) ensure the destination directory exists on each of
    the specified hosts prior to copying the source.

    Args:
        hosts (NodeSet): hosts on which to copy the source
        source (str): the file to copy to the hosts
        destination (str): the host location in which to copy the source
        mkdir (bool, optional): whether or not to ensure the destination
            directory exists on hosts prior to copying the source. Defaults to
            True.
        timeout (int, optional): command timeout. Defaults to 60 seconds.
        verbose (bool, optional): whether to log the command run and
            stdout/stderr. Defaults to True.
        raise_exception (bool, optional): whether to raise an exception if the
            command returns a non-zero exit status. Defaults to True.
        sudo (bool, optional): whether to run the command via sudo. Defaults to
            False.
        owner (str, optional): if specified the owner to assign as the owner of
            the copied file. Defaults to None.

    Returns:
        CommandResult: groups of command results from the same hosts with the same return status
    """
    result = None
    if mkdir:
        _mkdir_command = f"/usr/bin/mkdir -p {os.path.dirname(destination)}"
        result = run_remote(logger, hosts, _mkdir_command, verbose, timeout)

    if result is None or result.passed:
        if sudo:
            # In order to copy a protected file to a remote host in CI the source will first be
            # copied as is to the remote host
            other_hosts = hosts.difference(get_local_host())
            if other_hosts:
                # Existing files with strict file permissions can cause the subsequent non-sudo
                # copy to fail, so remove the file first
                _rm_command = command_as_user(f"rm -f {source}", "root")
                run_remote(logger, other_hosts, _rm_command, verbose, timeout)
                result = distribute_files(
                    logger, other_hosts, source, source, mkdir=True,
                    timeout=timeout, verbose=verbose, sudo=False, owner=None)

            if result is None or result.passed:
                # Then a local sudo copy will be executed on the remote node to copy the source
                # to the destination
                _cp_command = command_as_user(f"cp {source} {destination}", "root")
                result = run_remote(logger, hosts, _cp_command, verbose, timeout)
        else:
            # Without the sudo requirement copy the source to the destination directly with clush
            _command = get_clush_command(hosts, args=f"-S -v --copy {source} --dest {destination}")
            result = run_local(logger, _command, verbose, timeout)

        # If requested update the ownership of the destination file
        if owner is not None and result.passed:
            _chown_command = get_chown_command(owner, get_primary_group(owner), file=destination)
            _command = command_as_user(_chown_command, "root" if sudo else None)
            result = run_remote(logger, hosts, _command, verbose, timeout)

    return result
