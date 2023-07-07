#!/usr/bin/env python3
"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

# pylint: disable=import-error,no-name-in-module

import argparse
import getpass
import logging
import os
import re
import socket
import sys

from ClusterShell.NodeSet import NodeSet

# Update the path to support utils files that import other utils files
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "util"))
# pylint: disable=import-outside-toplevel
from logger_utils import get_console_handler                            # noqa: E402
from package_utils import install_packages, remove_packages             # noqa: E402
from run_utils import get_clush_command, run_remote, command_as_user    # noqa: E402

# Set up a logger for the console messages
logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)
logger.addHandler(get_console_handler("%(message)s", logging.DEBUG))


class SlurmSetupException(Exception):
    """Exception for SlurmSetup class."""


class SlurmSetup():
    """Slurm setup class."""

    EPILOG_FILE = '/var/tmp/epilog_soak.sh'
    EXAMPLE_FILES = [
        '/etc/slurm/slurm.conf.example',
        '/etc/slurm/cgroup.conf.example',
        '/etc/slurm/slurmdbd.conf.example']
    MUNGE_DIR = '/etc/munge'
    MUNGE_KEY = '/etc/munge/munge.key'
    PACKAGE_LIST = ['slurm', 'slurm-example-configs', 'slurm-slurmctld', 'slurm-slurmd']
    SLURM_CONF = '/etc/slurm/slurm.conf'
    SLURM_LOG_DIR = '/var/log/slurm'

    def __init__(self, log, nodes, control_node, sudo=False):
        """Initialize a SlurmSetup object.

        Args:
            log (logger): object configured to log messages
            nodes (NodeSet): slurm nodes
            control_node (NodeSet): slurm control node
            sudo (bool, optional): whether or not to use sudo with commands. Defaults to False.
        """
        self.log = log
        self.nodes = NodeSet(nodes)
        self.control = NodeSet(control_node)
        self.root = 'root' if sudo else None

    @property
    def all_nodes(self):
        """Get all the nodes specified in this class.

        Returns:
            NodeSet: all the nodes specified in this class
        """
        return self.nodes.union(self.control)

    def remove(self):
        """Remove slurm packages from the nodes.

        Raises:
            SlurmSetupException: if there is a problem removing the packages
        """
        self.log.info("Removing slurm packages")
        result = remove_packages(self.log, self.all_nodes, self.PACKAGE_LIST, self.root)
        if not result.passed:
            raise SlurmSetupException(f"Error removing slurm packages on {result.failed_hosts}")

    def install(self):
        """Install slurm packages on the nodes.

        Raises:
            SlurmSetupException: if there is a problem installing the packages
        """
        self.log.info("Installing slurm packages")
        result = install_packages(self.log, self.all_nodes, self.PACKAGE_LIST, self.root)
        if not result.passed:
            raise SlurmSetupException(f"Error installing slurm packages on {result.failed_hosts}")

    def update_config(self, slurm_user, partition):
        """Update the slurm config.

        Args:
            slurm_user (str): user to define in the slurm config file
            partition (str): name of the slurm partition to include in the configuration

        Raises:
            SlurmSetupException: if there is a problem
        """
        self.log.info("Updating slurm config files")

        # Create the slurm epilog script on the control node
        self._create_epilog_script(self.EPILOG_FILE)

        # Copy the slurm example.conf files to all nodes
        for source in self.EXAMPLE_FILES:
            self._copy_file(self.all_nodes, source, os.path.splitext(source)[0])

        # Update the config file on all hosts
        self._update_slurm_config(slurm_user, partition)

    def start_munge(self, user):
        """Start munge.

        Args:
            user (str): user account to use with munge

        Raises:
            SlurmSetupException: if there is a problem starting munge
        """
        self.log.info("Starting munge")

        # Create munge key only if it does not exist.
        result = run_remote(
            self.log, self.control, command_as_user(f'test -f {self.MUNGE_KEY}', self.root))
        if not result.passed:
            # Create a munge key on the control host
            self.log.debug('Creating a new munge key on %s', self.control)
            result = run_remote(
                self.log, self.control, command_as_user('create-munge-key', self.root))
            if not result.passed:
                # Try the other possible munge key creation command:
                result = run_remote(
                    self.log, self.control, command_as_user('mungekey -c', self.root))
                if not result.passed:
                    raise SlurmSetupException(f'Error creating munge key on {result.failed_hosts}')

        # Setup the munge dir file permissions on all hosts
        self._update_file(self.all_nodes, self.MUNGE_DIR, '777', user)

        # Setup the munge key file permissions on the control host
        self._update_file(self.control, self.MUNGE_KEY, '777', user)

        # Copy the munge key from the control node to the non-control nodes
        non_control = self.nodes.difference(self.control)
        self.log.debug('Copying the munge key to %s', non_control)
        command = get_clush_command(
            non_control, args=f"-B -S -v --copy {self.MUNGE_KEY} --dest {self.MUNGE_KEY}")
        result = run_remote(self.log, self.control, command)
        if not result.passed:
            raise SlurmSetupException(f'Error creating munge key on {result.failed_hosts}')

        # Resetting munge dir and key permissions
        self._update_file(self.all_nodes, self.MUNGE_KEY, '400', 'munge')
        self._update_file(self.all_nodes, self.MUNGE_DIR, '700', 'munge')

        # Restart munge on all nodes
        self._restart_systemctl(self.all_nodes, 'munge')

    def start_slurm(self, user, debug):
        """Start slurm.

        Args:
            user (str): user account to use with slurm
            debug (bool): whether or not to display slurm debug

        Raises:
            SlurmSetupException: if there is a problem starting slurm
        """
        self.log.info("Starting slurm")

        self._mkdir(self.all_nodes, self.SLURM_LOG_DIR)
        self._update_file_ownership(self.all_nodes, self.SLURM_LOG_DIR, user)
        self._mkdir(self.all_nodes, '/var/spool/slurmd')
        self._mkdir(self.all_nodes, '/var/spool/slurmctld')
        self._mkdir(self.all_nodes, '/var/spool/slurm/d')
        self._mkdir(self.all_nodes, '/var/spool/slurm/ctld')
        self._update_file_ownership(self.all_nodes, '/var/spool/slurm/ctld', user)
        self._update_file(self.all_nodes, '/var/spool/slurmctld', '775', user)
        self._remove_file(self.all_nodes, '/var/spool/slurmctld/clustername')

        # Restart slurmctld on the control node
        self._restart_systemctl(
            self.control, 'slurmctld', '/var/log/slurmctld.log', self.SLURM_CONF)

        # Restart slurmd on all nodes
        self._restart_systemctl(self.all_nodes, 'slurmd', '/var/log/slurmd.log', self.SLURM_CONF)

        # Update nodes to the idle state
        command = command_as_user(
            f'scontrol update nodename={str(self.nodes)} state=idle', self.root)
        result = run_remote(self.log, self.nodes, command)
        if not result.passed or debug:
            self._display_debug(self.control, '/var/log/slurmctld.log', self.SLURM_CONF)
            self._display_debug(self.all_nodes, '/var/log/slurmd.log', self.SLURM_CONF)
        if not result.passed:
            raise SlurmSetupException(f'Error setting nodes to idle on {self.nodes}')

    def _create_epilog_script(self, script):
        """Create epilog script to run after each job.

        Args:
            script (str): epilog script name.

        Raises:
            SlurmSetupException: if there is a problem creating the epilog script
        """
        self.log.debug('Creating the slurm epilog script to run after each job.')
        try:
            with open(script, 'w') as script_file:
                script_file.write('#!/bin/bash\n#\n')
                script_file.write('/usr/bin/bash -c \'pkill --signal 9 dfuse\'\n')
                script_file.write(
                    '/usr/bin/bash -c \'for dir in $(find /tmp/daos_dfuse);'
                    'do fusermount3 -uz $dir;rm -rf $dir; done\'\n')
                script_file.write('exit 0\n')
        except IOError as error:
            self.log.debug('Error writing %s - verifying file existence:', script)
            run_remote(self.log, self.control, f'ls -al {script}')
            raise SlurmSetupException(f'Error writing slurm epilog script {script}') from error

        command = command_as_user(f'chmod 755 {script}', self.root)
        if not run_remote(self.log, self.control, command).passed:
            raise SlurmSetupException(f'Error setting slurm epilog script {script} permissions')

    def _copy_file(self, nodes, source, destination):
        """Copy the source file to the destination on all the nodes.

        Args:
            nodes (NodeSet): nodes on which to copy the files
            source (str): file to copy
            destination (str): where to copy the file

        Raises:
            SlurmSetupException: if there is an error copying the file on any host
        """
        self.log.debug(f'Copying the {source} file to {destination} on {str(nodes)}')
        command = command_as_user(f'cp {source} {destination}', self.root)
        result = run_remote(self.log, nodes, command)
        if not result.passed:
            raise SlurmSetupException(
                f'Error copying {source} to {destination} on {str(result.failed_hosts)}')

    def _update_slurm_config(self, slurm_user, partition):
        """Update the slurm config file.

        Args:
            slurm_user (str): user to define in the slurm config file
            partition (str): name of the slurm partition to include in the configuration

        Raises:
            SlurmSetupException: if there is a problem modifying slurm config file
        """
        # Update the config file with the slurm cluster name
        self._modify_slurm_config_file(
            'slurm cluster name', self.all_nodes, 's/ClusterName=cluster/ClusterName=ci_cluster/g',
            self.root)

        # Update the config file with the slurm user
        self._modify_slurm_config_file(
            'slurm user', self.all_nodes, f's/SlurmUser=slurm/SlurmUser={slurm_user}/g',
            self.root)

        # Update the config file with the removal of the NodeName entry
        self._modify_slurm_config_file(
            'node name', self.all_nodes, 's/NodeName/#NodeName/g', self.root)

        # Update the config file with the slurm epilog file
        self._modify_slurm_config_file(
            'epilog file', self.all_nodes, 's#EpilogSlurmctld=#EpilogSlurmctld={EPILOG_FILE}#g',
            self.root)

        # Update the config file with the slurm control node
        not_updated = self.all_nodes.copy()
        for control_keyword in ['SlurmctldHost', 'ControlMachine']:
            command = f'grep {control_keyword} {self.SLURM_CONF}'
            results = run_remote(self.log, self.all_nodes, command)
            if results.passed_hosts:
                not_updated.remove(
                    self._modify_slurm_config_file(
                        'slurm control node', results.passed_hosts,
                        f's/{control_keyword}=linux0/{control_keyword}={str(self.control)}/g',
                        self.root))
        if not_updated:
            raise SlurmSetupException(f'Slurm control node not updated on {not_updated}')

        # Update the config file with each node's socket/core/thread information
        self._update_slurm_config_sys_info()

        # Update the config file with the partition information
        self._update_slurm_config_partitions(partition)

    def _modify_slurm_config_file(self, description, hosts, replacement, user=None):
        """Replace text in the slurm configuration file.

        Args:
            description (str): what is being modified in the slurm config file
            hosts (NodeSet): hosts on which to modify the slurm config file
            replacement (str): what text to replace
            user (str, optional): user to use when running the sed command. Defaults to None.

        Raises:
            SlurmSetupException: if there is a problem modifying slurm config file

        Returns:
            NodeSet: hosts on which the command succeeded
        """
        self.log.debug(
            'Updating the %s in the %s config file on %s', description, self.SLURM_CONF, hosts)
        command = command_as_user(f'sed -i -e \'{replacement}\' {self.SLURM_CONF}', user)
        result = run_remote(self.log, hosts, command)
        if result.failed_hosts:
            raise SlurmSetupException(
                f'Error updating {description} in the {self.SLURM_CONF} config '
                f'file on {result.failed_hosts}')
        return result.passed_hosts

    def _update_slurm_config_sys_info(self):
        """Update the slurm config files with hosts socket/core/thread information.

        Raises:
            SlurmSetupException: if there is a problem updating the slurm config file
        """
        self.log.debug('Updating slurm config socket/core/thread information on %s', self.all_nodes)
        command = r"lscpu | grep -E '(Socket|Core|Thread)\(s\)'"
        result = run_remote(self.log, self.all_nodes, command)
        for data in result.output:
            info = {
                match[0]: match[1]
                for match in re.findall(r"(Socket|Core|Thread).*:\s+(\d+)", "\n".join(data.stdout))
                if len(match) > 1}

            if "Socket" in info and "Core" in info and "Thread" in info:
                echo_command = (f'echo \"Nodename={data.hosts} Sockets={info["Socket"]} '
                                f'CoresPerSocket={info["Core"]} ThreadsPerCore={info["Thread"]}\"')
                mod_result = self._append_config_file(echo_command)
                if mod_result.failed_hosts:
                    raise SlurmSetupException(
                        'Error updating socket/core/thread information on '
                        f'{mod_result.failed_hosts}')

    def _update_slurm_config_partitions(self, partition):
        """Update the slurm config files with hosts partition information.

        Args:
            partition (str): name of the slurm partition to include in the configuration

        Raises:
            SlurmSetupException: if there is a problem updating the slurm config file
        """
        self.log.debug('Updating slurm config partition information on %s', self.all_nodes)
        echo_command = (
            f'echo \"PartitionName={partition} Nodes={self.nodes} Default=YES MaxTime=INFINITE '
            'State=UP\"')
        mod_result = self._append_config_file(echo_command)
        if mod_result.failed_hosts:
            raise SlurmSetupException(
                f'Error updating partition information on {mod_result.failed_hosts}')

    def _append_config_file(self, echo_command):
        """Append data to the config file.

        Args:
            echo_command (str): command adding contents to the config file

        Returns:
            RemoteCommandResult: the result from the echo | tee command
        """
        tee_command = command_as_user(f'tee -a {self.SLURM_CONF}', self.root)
        return run_remote(self.log, self.all_nodes, f'{echo_command} | {tee_command}')

    def _update_file(self, nodes, file, permission, user):
        """Update file permissions and ownership.

        Args:
            nodes (NodeSet): nodes on which to update the file permissions/ownership
            file (str): file whose permissions/ownership will be updated
            permission (str): file permission to set
            user (str): user to have ownership of the file

        Raises:
            SlurmSetupException: if there was an error updating the file permissions/ownership
        """
        self._update_file_permissions(nodes, file, permission)
        self._update_file_ownership(nodes, file, user)

    def _update_file_permissions(self, nodes, file, permission):
        """Update the file permissions.

        Args:
            nodes (NodeSet): nodes on which to update the file permissions
            file (str): file whose permissions will be updated
            permission (str): file permission to set
            user (str): user to use with chown command

        Raises:
            SlurmSetupException: if there was an error updating the file permissions
        """
        self.log.debug('Updating file permissions for %s on %s', self.MUNGE_DIR, nodes)
        result = run_remote(
            self.log, nodes, command_as_user(f'chmod -R {permission} {file}', self.root))
        if not result.passed:
            raise SlurmSetupException(
                f'Error updating permissions to {permission} for {file} on {result.failed_hosts}')

    def _update_file_ownership(self, nodes, file, user):
        """Update the file ownership.

        Args:
            nodes (NodeSet): nodes on which to update the file ownership
            file (str): file whose ownership will be updated
            user (str): user to have ownership of the file

        Raises:
            SlurmSetupException: if there was an error updating the file ownership
        """
        result = run_remote(self.log, nodes, command_as_user(f'chown {user}. {file}', self.root))
        if not result.passed:
            raise SlurmSetupException(
                f'Error updating ownership to {user} for {file} on {result.failed_hosts}')

    def _remove_file(self, nodes, file):
        """Remove a file.

        Args:
            nodes (NodeSet): nodes on which to remove the file
            file (str): file to remove

        Raises:
            SlurmSetupException: if there was an error removing the file
        """
        self.log.debug('Removing %s on %s', file, nodes)
        result = run_remote(self.log, nodes, command_as_user(f'rm -fr {file}', self.root))
        if not result.passed:
            raise SlurmSetupException(f'Error removing {file} on {result.failed_hosts}')

    def _restart_systemctl(self, nodes, service, debug_log=None, debug_config=None):
        """Restart the systemctl service.

        Args:
            nodes (NodeSet): nodes on which to restart the systemctl service
            service (str): systemctl service to restart/enable
            debug_log (str, optional): log file to display if there is a problem restarting
            debug_config (str, optional): config file to display if there is a problem restarting

        Raises:
            SlurmSetupException: if there is a problem restarting the systemctl service
        """
        self.log.debug('Restarting %s on %s', service, nodes)
        for action in ('restart', 'enable'):
            command = command_as_user(f'systemctl {action} {service}', self.root)
            result = run_remote(self.log, self.all_nodes, command)
            if not result.passed:
                self._display_debug(result.failed_hosts, debug_log, debug_config)
                raise SlurmSetupException(f'Error restarting {service} on {result.failed_hosts}')

    def _display_debug(self, nodes, debug_log=None, debug_config=None):
        """Display debug information.

        Args:
            nodes (NodeSet): nodes on which to display the debug information
            debug_log (str, optional): log file to display. Defaults to None.
            debug_config (str, optional): config file to display. Defaults to None.
        """
        if debug_log:
            self.log.debug('DEBUG: %s contents:', debug_log)
            command = command_as_user(f'cat {debug_log}', self.root)
            run_remote(self.log, nodes, command)
        if debug_config:
            self.log.debug('DEBUG: %s contents:', debug_config)
            command = command_as_user(f'grep -v \"^#\\w\" {debug_config}', self.root)
            run_remote(self.log, nodes, command)

    def _mkdir(self, nodes, directory):
        """Create a directory.

        Args:
            nodes (NodeSet): nodes on which to create the directory
            directory (str): directory to create

        Raises:
            SlurmSetupException: if there was an error creating the directory
        """
        self.log.debug('Creating %s on %s', directory, nodes)
        result = run_remote(self.log, nodes, command_as_user(f'mkdir -p {directory}', self.root))
        if not result.passed:
            raise SlurmSetupException(f'Error creating {directory} on {result.failed_hosts}')


def main():
    """Set up test env with slurm."""
    parser = argparse.ArgumentParser(prog="slurm_setup.py")

    parser.add_argument(
        "-n", "--nodes",
        default=None,
        help="Comma separated list of nodes to install slurm")
    parser.add_argument(
        "-c", "--control",
        default=socket.gethostname().split('.', 1)[0],
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
    logger.info("Arguments: %s", args)

    # Check params
    if args.nodes is None:
        logger.error("slurm_nodes: Specify at least one slurm node")
        sys.exit(1)

    slurm_setup = SlurmSetup(logger, args.nodes, args.control, args.sudo)

    # Remove packages if specified with --remove and then exit
    if args.remove:
        try:
            slurm_setup.remove()
            sys.exit(0)
        except SlurmSetupException as error:
            logger.error(str(error))
            sys.exit(1)

    # Install packages if specified with --install and continue with setup
    if args.install:
        try:
            slurm_setup.install()
        except SlurmSetupException as error:
            logger.error(str(error))
            sys.exit(1)

    # Edit the slurm conf files
    try:
        slurm_setup.update_config(args.user, args.partition)
    except SlurmSetupException as error:
        logger.error(str(error))
        sys.exit(1)

    # Munge Setup
    try:
        slurm_setup.start_munge(args.user)
    except SlurmSetupException as error:
        logger.error(str(error))
        sys.exit(1)

    # Slurm Startup
    try:
        slurm_setup.start_slurm(args.user, args.debug)
    except SlurmSetupException as error:
        logger.error(str(error))
        sys.exit(1)

    sys.exit(0)


if __name__ == "__main__":
    main()
