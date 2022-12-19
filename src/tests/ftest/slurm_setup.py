#!/usr/bin/python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

# pylint: disable=import-error,no-name-in-module

import argparse
import getpass
import logging
import re
import socket
import sys

from ClusterShell.NodeSet import NodeSet

from util.logger_utils import get_console_handler
from util.run_utils import get_clush_command_list, run_remote

# Set up a logger for the console messages
logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)
logger.addHandler(get_console_handler("%(message)s", logging.DEBUG))

SLURM_CONF = "/etc/slurm/slurm.conf"

PACKAGE_LIST = ["slurm", "slurm-example-configs",
                "slurm-slurmctld", "slurm-slurmd"]

COPY_LIST = ["cp /etc/slurm/slurm.conf.example /etc/slurm/slurm.conf",
             "cp /etc/slurm/cgroup.conf.example /etc/slurm/cgroup.conf",
             "cp /etc/slurm/slurmdbd.conf.example /etc/slurm/slurmdbd.conf"]

MUNGE_STARTUP = [
    "chown munge. {0}".format("/etc/munge/munge.key"),
    "systemctl restart munge",
    "systemctl enable munge"]

SLURMCTLD_STARTUP = [
    "systemctl restart slurmctld",
    "systemctl enable slurmctld"]

SLURMCTLD_STARTUP_DEBUG = [
    "cat /var/log/slurmctld.log",
    "grep -v \"^#\\w\" /etc/slurm/slurm.conf"]

SLURMD_STARTUP = [
    "systemctl restart slurmd",
    "systemctl enable slurmd"]

SLURMD_STARTUP_DEBUG = [
    "cat /var/log/slurmd.log",
    "grep -v \"^#\\w\" /etc/slurm/slurm.conf"]


def update_config_cmdlist(args):
    """Create the command lines to update slurmd.conf file.

    Args:
        args (Namespace): command line arguments

    Returns:
        cmd_list: list of cmdlines to update config file

    """
    all_nodes = NodeSet("{},{}".format(str(args.control), str(args.nodes)))
    cmd_list = ["sed -i -e 's/ClusterName=cluster/ClusterName=ci_cluster/g' {}".format(SLURM_CONF),
                "sed -i -e 's/SlurmUser=slurm/SlurmUser={}/g' {}".format(args.user, SLURM_CONF),
                "sed -i -e 's/NodeName/#NodeName/g' {}".format(SLURM_CONF)]
    sudo = "sudo" if args.sudo else ""
    # Copy the slurm*example.conf files to /etc/slurm/
    if execute_cluster_cmds(all_nodes, COPY_LIST, args.sudo) > 0:
        sys.exit(1)
    match = False
    # grep SLURM_CONF to determine format of the the file
    for ctl_host in ["SlurmctldHost", "ControlMachine"]:
        command = r"grep {} {}".format(ctl_host, SLURM_CONF)
        if run_remote(logger, all_nodes, command).passed:
            ctl_str = "sed -i -e 's/{0}=linux0/{0}={1}/g' {2}".format(
                ctl_host, args.control, SLURM_CONF)
            cmd_list.insert(0, ctl_str)
            match = True
            break
    if not match:
        logger.error("% could not be updated. Check conf file format", SLURM_CONF)
        sys.exit(1)

    # This info needs to be gathered from every node that can run a slurm job
    command = r"lscpu | grep -E '(Socket|Core|Thread)\(s\)'"
    result = run_remote(logger, all_nodes, command)
    for data in result.output:
        info = {
            match[0]: match[1]
            for match in re.findall(r"(Socket|Core|Thread).*:\s+(\d+)", "\n".join(data.stdout))
            if len(match) > 1}

        if "Socket" not in info or "Core" not in info or "Thread" not in info:
            # Did not find value for socket|core|thread so do not
            # include in config file
            pass
        cmd_list.append("echo \"NodeName={0} Sockets={1} CoresPerSocket={2} "
                        "ThreadsPerCore={3}\" |{4} tee -a {5}".format(
                            data.hosts, info["Socket"], info["Core"], info["Thread"], sudo,
                            SLURM_CONF))

    #
    cmd_list.append("echo \"PartitionName={} Nodes={} Default=YES "
                    "MaxTime=INFINITE State=UP\" |{} tee -a {}".format(
                        args.partition, args.nodes, sudo, SLURM_CONF))

    return execute_cluster_cmds(all_nodes, cmd_list, args.sudo)


def execute_cluster_cmds(nodes, cmdlist, sudo=False):
    """Execute the list of cmds on hostlist nodes.

    Args:
        nodes (NodeSet): nodes on which to execute the commands
        cmdlist ([type]): list of cmdlines to execute
        sudo (str, optional): Execute cmd with sudo privileges. Defaults to false.

     Returns:
        ret_code: returns 0 if all commands passed on all hosts; 1 otherwise

    """
    for cmd in cmdlist:
        if sudo:
            cmd = "sudo {}".format(cmd)
        if not run_remote(logger, nodes, cmd, timeout=600).passed:
            # Do not bother executing any remaining commands if this one failed
            return 1
    return 0


def configuring_packages(args, action):
    """Install required slurm and munge packages.

    Args:
        args (Namespace): command line arguments
        action (str): 'install' or 'remove'

    """
    # Install packages on control and compute nodes
    all_nodes = NodeSet("{},{}".format(str(args.control), str(args.nodes)))
    logger.info("%s slurm packages on %s: %s", action, all_nodes, ", ".join(PACKAGE_LIST))
    command = ["dnf", action, "-y"] + PACKAGE_LIST
    return execute_cluster_cmds(all_nodes, [" ".join(command)], args.sudo)


def start_munge(args):
    """Start munge service on all nodes.

    Args:
        args (Namespace): command line arguments

    """
    sudo = "sudo" if args.sudo else ""
    all_nodes = NodeSet("{},{}".format(str(args.control), str(args.nodes)))
    # exclude the control node
    nodes = NodeSet(str(args.nodes))
    nodes.difference_update(str(args.control))

    # copy key to all nodes FROM slurmctl node;
    # change the protections/ownership on the munge dir on all nodes
    cmd_list = [
        "{0} chmod -R 777 /etc/munge; {0} chown {1}. /etc/munge".format(
            sudo, args.user)]
    if execute_cluster_cmds(all_nodes, cmd_list) > 0:
        return 1

    # Check if file exists on slurm control node
    # change the protections/ownership on the munge key before copying
    cmd_list = ["set -Eeu",
                "rc=0",
                "if [ ! -f /etc/munge/munge.key ]",
                "then {} create-munge-key".format(sudo),
                "fi",
                "{} chmod 777 /etc/munge/munge.key".format(sudo),
                "{} chown {}. /etc/munge/munge.key".format(sudo, args.user)]

    if execute_cluster_cmds(args.control, ["; ".join(cmd_list)]) > 0:
        return 1
    # remove any existing key from other nodes
    cmd_list = ["{} rm -f /etc/munge/munge.key".format(sudo)]
    if execute_cluster_cmds(nodes, ["; ".join(cmd_list)]) > 0:
        return 1

    # copy munge.key to all hosts
    command = get_clush_command_list(nodes)
    command.extend(["--copy", "/etc/munge/munge.key", "--dest", "/etc/munge/munge.key"])
    if execute_cluster_cmds(args.control, [" ".join(command)]) > 0:
        return 1

    # set the protection back to defaults
    cmd_list = [
        "{} chmod 400 /etc/munge/munge.key".format(sudo),
        "{} chown munge. /etc/munge/munge.key".format(sudo),
        "{} chmod 700 /etc/munge".format(sudo),
        "{} chown munge. /etc/munge".format(sudo)]
    if execute_cluster_cmds(all_nodes, ["; ".join(cmd_list)]) > 0:
        return 1

    # Start Munge service on all nodes
    all_nodes = NodeSet("{},{}".format(str(args.control), str(args.nodes)))
    return execute_cluster_cmds(all_nodes, MUNGE_STARTUP, args.sudo)


def start_slurm(args):
    """Start the slurm services on all nodes.

    Args:
        args (Namespace): command line arguments

    """
    # Setting up slurm on all nodes
    all_nodes = NodeSet("{},{}".format(str(args.control), str(args.nodes)))
    cmd_list = [
        "mkdir -p /var/log/slurm",
        "chown {}. {}".format(args.user, "/var/log/slurm"),
        "mkdir -p /var/spool/slurmd",
        "mkdir -p /var/spool/slurmctld",
        "mkdir -p /var/spool/slurm/d",
        "mkdir -p /var/spool/slurm/ctld",
        "chown {}. {}/ctld".format(args.user, "/var/spool/slurm"),
        "chown {}. {}".format(args.user, "/var/spool/slurmctld"),
        "chmod 775 {}".format("/var/spool/slurmctld"),
        "rm -f /var/spool/slurmctld/clustername"]

    if execute_cluster_cmds(all_nodes, cmd_list, args.sudo) > 0:
        return 1

    # Startup the slurm control service
    status = execute_cluster_cmds(args.control, SLURMCTLD_STARTUP, args.sudo)
    if status > 0 or args.debug:
        execute_cluster_cmds(args.control, SLURMCTLD_STARTUP_DEBUG, args.sudo)
    if status > 0:
        return 1

    # Startup the slurm service
    status = execute_cluster_cmds(all_nodes, SLURMD_STARTUP, args.sudo)
    if status > 0 or args.debug:
        execute_cluster_cmds(all_nodes, SLURMD_STARTUP_DEBUG, args.sudo)
    if status > 0:
        return 1

    # ensure that the nodes are in the idle state
    cmd_list = ["scontrol update nodename={} state=idle".format(args.nodes)]
    status = execute_cluster_cmds(args.nodes, cmd_list, args.sudo)
    if status > 0 or args.debug:
        cmd_list = (SLURMCTLD_STARTUP_DEBUG)
        execute_cluster_cmds(args.control, cmd_list, args.sudo)
        cmd_list = (SLURMD_STARTUP_DEBUG)
        execute_cluster_cmds(all_nodes, cmd_list, args.sudo)
    if status > 0:
        return 1
    return 0


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

    # Convert control node and slurm node list into NodeSets
    args.control = NodeSet(args.control)
    args.nodes = NodeSet(args.nodes)

    # Remove packages if specified with --remove and then exit
    if args.remove:
        ret_code = configuring_packages(args, "remove")
        if ret_code > 0:
            sys.exit(1)
        sys.exit(0)

    # Install packages if specified with --install and continue with setup
    if args.install:
        ret_code = configuring_packages(args, "install")
        if ret_code > 0:
            sys.exit(1)

    # Edit the slurm conf files
    ret_code = update_config_cmdlist(args)
    if ret_code > 0:
        sys.exit(1)

    # Munge Setup
    ret_code = start_munge(args)
    if ret_code > 0:
        sys.exit(1)

    # Slurm Startup
    ret_code = start_slurm(args)
    if ret_code > 0:
        sys.exit(1)

    sys.exit(0)


if __name__ == "__main__":
    main()
