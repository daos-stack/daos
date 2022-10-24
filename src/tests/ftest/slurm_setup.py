#!/usr/bin/env python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import argparse
from datetime import datetime
import getpass
import logging
import os
import re
import sys

sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "util"))
# pylint: disable=import-outside-toplevel
from host_utils import get_node_set                                                 # noqa: E402
from logger_utils import get_console_handler                                        # noqa: E402
from run_utils import get_clush_command_list, run_remote, get_local_host            # noqa: E402


# Set up a logger for the console messages
logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)
logger.addHandler(get_console_handler("%(message)s", logging.DEBUG))


class SlurmConfig():
    """Configure slurm on the specified hosts."""

    PACKAGES = ['slurm', 'slurm-example-configs', 'slurm-slurmctld', 'slurm-slurmd']
    CONFIG_FILES = [
        os.path.join(os.sep, 'etc', 'slurm', 'slurm.conf'),
        os.path.join(os.sep, 'etc', 'slurm', 'cgroup.conf'),
        os.path.join(os.sep, 'etc', 'slurm', 'slurmdbd.conf')]

    def __init__(self, log, nodes, control, sudo):
        """Initialize a SlurmConfig object.

        Args:
            log (logger): object configured to log messages
            nodes (object): nodes to utilize as slurm nodes
            control (object): node to utilize as the slurm control node
            sudo (bool): whether or not to run commands with sudo
        """
        self.log = log
        self.nodes = get_node_set(nodes)
        self.control = get_node_set(control)
        self.sudo = ['sudo', '-n'] if sudo else []
        self._timestamp = {}

    @property
    def all_nodes(self):
        """Get all the nodes.

        Returns:
            NodeSet: all of the slurm nodes
        """
        return self.nodes | self.control

    def install(self):
        """Install the slurm RPM packages on all the nodes.

        Returns:
            bool: if all the slurm packages were installed successfully

        """
        self.log.info('Installing packages on %s: %s', self.all_nodes, ', '.join(self.PACKAGES))
        command = self.sudo + ['dnf', 'install', '-y'] + self.PACKAGES
        return run_remote(self.log, self.all_nodes, ' '.join(command), timeout=600).passed

    def remove(self):
        """Remove the slurm RPM packages on all the nodes.

        Returns:
            bool: if all the slurm packages were removed successfully

        """
        self.log.info("Removing packages from %s: %s", self.all_nodes, ', '.join(self.PACKAGES))
        command = self.sudo + ['dnf', 'remove', '-y'] + self.PACKAGES
        return run_remote(self.log, self.all_nodes, ' '.join(command), timeout=600).passed

    def start(self, partition, user=None, debug=False):
        """Start slurm.

        Args:
            partition (str): slurm partition name
            user (str, optional): slurm user name. Defaults to None
            debug (bool, optional): whether or not to display debug information if commands fail.
                Defaults to False.

        Returns:
            bool: True if successful; False otherwise

        """
        if not user:
            user = getpass.getuser()

        if not self._copy_example_config_files():
            self.log.error("Error copying example config files")
            return False

        if not self._update_slurm_config(user, partition):
            self.log.error("Error updating the slurm config file")
            return False

        if not self.start_munge(user):
            self.log.error("Error starting munge")
            return False

        if not self.start_slurm(user, debug):
            self.log.error("Error starting slurm")
            return False

        return True

    def _copy_example_config_files(self):
        """Copy the example config files to all the nodes.

        Returns:
            bool: True if successful; False otherwise

        """
        self.log.info("Copying example config files to %s", self.all_nodes)
        for config_file in self.CONFIG_FILES:
            command = self.sudo + ["cp", config_file + ".example", config_file]
            if not run_remote(self.log, self.all_nodes, ' '.join(command)).passed:
                return False
        return True

    def _update_slurm_config(self, user, partition):
        """Update the slurm config file.

        Args:
            user (str): slurm user name
            partition (str): slurm partition name

        Returns:
            bool: True if successful; False otherwise

        """
        slurm_config = self.CONFIG_FILES[0]
        self.log.info("Updating the %s file", slurm_config)

        # Modify the slurm config file
        control = None
        with open(self.CONFIG_FILES[0], "r", encoding="utf-8") as config:
            match = re.findall(r"(SlurmctldHost|ControlMachine)", "\n".join(config.readlines()))
            if match:
                control = match[0]
        if not control:
            self.log.error(
                "Error unable to find 'SlurmctldHost' or 'ControlMachine' in %s", slurm_config)
            return False
        commands = [
            f"sed -i -e 's/{control}=linux0/{control}={self.control}/g' {slurm_config}",
            f"sed -i -e 's/ClusterName=cluster/ClusterName=ci_cluster/g' {slurm_config}",
            f"sed -i -e 's/SlurmUser=slurm/SlurmUser={user}/g' {slurm_config}",
            f"sed -i -e 's/NodeName/#NodeName/g' {slurm_config}"]

        # Gathered socket, core, and thread information from every node that can run a slurm job
        command = r"lscpu | grep -E '(Socket|Core|Thread)\(s\)'"
        result = run_remote(logger, self.all_nodes, command)
        for data in result.output:
            info = {
                match[0]: match[1]
                for match in re.findall(r"(Socket|Core|Thread).*:\s+(\d+)", "\n".join(data.stdout))
                if len(match) > 1}

            if "Socket" in info and "Core" in info and "Thread" in info:
                entries = [
                    f'NodeName={data.hosts}',
                    f'Sockets={info["Socket"]}',
                    f'CoresPerSocket={info["Core"]}',
                    f'ThreadsPerCore={info["Thread"]}']
                commands.append(self._get_add_config_command(slurm_config, ' '.join(entries)))

        # Add the partition information to the config file
        entries = [
            f"PartitionName={partition}",
            f"Nodes={self.nodes}",
            "Default=YES",
            "MaxTime=INFINITE",
            "State=UP"]
        commands.append(self._get_add_config_command(slurm_config, ' '.join(entries)))

        # Update all the config files
        for command in commands:
            if not run_remote(self.log, self.all_nodes, command).passed:
                return False

        return True

    def _get_add_config_command(self, config, entry):
        """Get the command to add an entry to the config file.

        Args:
            config (str): _description_
            entry (str): entry to add to the config file

        Returns:
            str: the command to add an entry to the config file

        """
        return " ".join(["echo", f"\"{entry}\"", "|"] + self.sudo + ["tee", "-a", config])

    def start_munge(self, user):
        """Start munge service on all nodes.

        Args:
            user (str): slurm user name

        Returns:
            bool: True if successful; False otherwise

        """
        munge_dir = os.path.join(os.sep, 'etc', 'munge')
        munge_key = os.path.join(munge_dir, 'munge.key')
        self.log.info('Setting up munge on %s', self.all_nodes)

        # Get a list of non-control nodes
        non_control = self.nodes.copy()
        non_control.difference_update(self.control)

        # copy key to all nodes FROM slurmctl node;
        # change the protections/ownership on the munge dir on all nodes
        self.log.debug('Temporarily changing munge config file permissions on %s', non_control)
        commands = [
            self.sudo + ['chmod', '-R', '777', munge_dir],
            self.sudo + ['chown', f'{user}.', munge_dir],
            self.sudo + ['rm', '-f', munge_key]
        ]
        for command in commands:
            if not run_remote(self.log, non_control, ' '.join(command)).passed:
                return False

        # Ensure a munge key exists on the control node with the correct permissions
        self.log.debug('Preparing munge config file on %s', self.control)
        sudo_str = ' '.join(self.sudo)
        commands = [
            'set -Eeu',
            'rc=0',
            f'if [ ! -f {munge_key} ]',
            f'then {sudo_str} create-munge-key',
            'fi',
            f'{sudo_str} chmod 777 {munge_key}',
            f'{sudo_str} chown {user}. {munge_key}']
        if not run_remote(self.log, self.control, '; '.join(commands)).passed:
            return False

        # Copy the munge.key to all hosts
        self.log.debug('Copying munge config from %s to %s', self.control, non_control)
        command = get_clush_command_list(non_control)
        command.extend(['--copy', munge_key, '--dest', munge_key])
        if not run_remote(self.log, self.control, ' '.join(command)).passed:
            return False

        # Set the protection back to defaults
        self.log.debug('Resetting munge file permissions on %s', self.all_nodes)
        commands = [
            self.sudo + ['chmod', '400', munge_key],
            self.sudo + ['chown', 'munge.', munge_key],
            self.sudo + ['chmod', '700', munge_dir],
            self.sudo + ['chown', 'munge.', munge_dir],
        ]
        for command in commands:
            if not run_remote(self.log, self.all_nodes, ' '.join(command)).passed:
                return False

        # Start munge service on all nodes
        self.log.info('Starting munge on %s', self.all_nodes)
        commands = [
            self.sudo + ['systemctl', 'restart', 'munge'],
            self.sudo + ['systemctl', 'enable', 'munge'],
        ]
        for command in commands:
            if not run_remote(self.log, self.all_nodes, ' '.join(command)).passed:
                return False
        return True

    def start_slurm(self, user, debug):
        """Start the slurm services on all nodes.

        Args:
            user (str): slurm user name
            debug (bool): whether or not to display debug information if commands fail

        Returns:
            bool: True if successful; False otherwise

        """
        # Setting up slurm on all nodes
        self.log.info('Setting up slurm on %s', self.all_nodes)
        slurm_log_dir = os.path.join(os.sep, 'var', 'log', 'slurm')
        slurmd_dir = os.path.join(os.sep, 'var', 'spool', 'slurmd')
        slurmctld_dir = os.path.join(os.sep, 'var', 'spool', 'slurmctld')
        slurm_d_dir = os.path.join(os.sep, 'var', 'spool', 'slurm', 'd')
        slurm_ctld_dir = os.path.join(os.sep, 'var', 'spool', 'slurm', 'ctld')
        slurmd_log = os.path.join(os.sep, 'var', 'log', 'slurmd.log')
        slurmctld_log = os.path.join(os.sep, 'var', 'log', 'slurmctld.log')

        commands = [
            self.sudo + ['mkdir', '-p', slurm_log_dir],
            self.sudo + ['chown', f'{user}.', slurm_log_dir],
            self.sudo + ['mkdir', '-p', slurmd_dir],
            self.sudo + ['mkdir', '-p', slurm_d_dir],
            self.sudo + ['mkdir', '-p', slurmctld_dir],
            self.sudo + ['mkdir', '-p', slurm_ctld_dir],
            self.sudo + ['chown', f'{user}.', slurmctld_dir],
            self.sudo + ['chown', f'{user}.', slurm_ctld_dir],
            self.sudo + ['chmod', '775', slurmctld_dir],
            self.sudo + ['rm', '-f', os.path.join(slurmctld_dir, "clustername")],
        ]
        for command in commands:
            if not run_remote(self.log, self.all_nodes, ' '.join(command)).passed:
                return False

        # Start the slurm control service
        if not self._start_service('slurmctld', self.control, debug, slurmctld_log):
            return False

        # Startup the slurm service
        if not self._start_service('slurmd', self.all_nodes, debug, slurmd_log):
            return False

        # Ensure that the nodes are in the idle state
        command = self.sudo + ['scontrol', 'update', f'nodename={self.nodes}', 'state=idle']
        if not run_remote(self.log, self.control, ' '.join(command)).passed:
            if debug:
                self._display_debug('slurmctld', self.control, slurmctld_log, False)
                self._display_debug('slurmd', self.all_nodes, slurmd_log, True)
            return False

        return True

    def _start_service(self, service, nodes, debug, log_file):
        """Start the requested service on the specified nodes.

        Args:
            service (str): the service to start
            nodes (NodeSet): nodes on which to start the service
            debug (bool): whether or not to display additional information if commands fail
            log_file (str): log file to display if there is an error and debug is set

        Returns:
            bool: True if successful; False otherwise

        """
        self.log.info('Starting the %s service on %s', service, nodes)
        commands = [
            self.sudo + ['systemctl', 'restart', service],
            self.sudo + ['systemctl', 'enable', service],
        ]
        self._timestamp[service] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        for command in commands:
            if not run_remote(self.log, nodes, ' '.join(command)).passed:
                if debug:
                    self._display_debug(service, nodes, log_file, True)
                return False
        return True

    def _display_debug(self, service, nodes, log_file, include_config):
        """Display debug information about the specified service.

        Args:
            service (str): the service name
            nodes (NodeSet): nodes on which the service is running
            log_file (str): log file to display
            include_config (bool): whether or not to display the config file
        """
        self.log.debug("Debug %s service information:", service)
        debug_commands = [self.sudo + ['systemctl', 'status', service]]
        if service in self._timestamp:
            since = self._timestamp[service]
            debug_commands.append(
                self.sudo + ["journalctl", f"--grep={service}", f"--since=\"{since}\""])
        debug_commands.append(self.sudo + ['cat', log_file])
        if include_config:
            debug_commands.append(self.sudo + ['grep', '-v', '"^#\\w"', self.CONFIG_FILES[0]])
        for debug_command in debug_commands:
            run_remote(self.log, nodes, ' '.join(debug_command))


def main():
    """Set up test env with slurm."""
    parser = argparse.ArgumentParser(prog="slurm_setup.py")

    parser.add_argument(
        "-n", "--nodes",
        default=None,
        help="Comma separated list of nodes to install slurm")
    parser.add_argument(
        "-c", "--control",
        default=get_local_host(),
        help="slurm control node; test control node if None")
    parser.add_argument(
        "-p", "--partition",
        default="daos_client",
        help="Partition name; all nodes will be in this partition")
    parser.add_argument(
        "-u", "--user",
        default=getpass.getuser(),
        help="slurm user for config file; if none the current user is used")
    parser.add_argument(
        "-i", "--install",
        action="store_true",
        help="Install all the slurm/munge packages")
    parser.add_argument(
        "-r", "--remove",
        action="store_true",
        help="Install all the slurm/munge packages")
    parser.add_argument(
        "-s", "--sudo",
        action="store_true",
        help="Run all commands with privileges")
    parser.add_argument(
        "-d", "--debug",
        action="store_true",
        help="Run all debug commands")

    args = parser.parse_args()

    # Setup the Launch object
    slurm_config = SlurmConfig(logger, args.nodes, args.control, args.sudo)

    # If requested remove the packages and exit
    if args.remove:
        if not slurm_config.remove().passed:
            sys.exit(1)
        sys.exit(0)

    # If requested install the packages
    if args.install:
        if not slurm_config.install().passed:
            sys.exit(1)

    # Setup slurm
    if not slurm_config.start(args.partition, args.user, args.debug):
        sys.exit(1)

    logger.info("Slurm setup complete")
    sys.exit(0)


if __name__ == "__main__":
    main()
