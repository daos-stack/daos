#!/usr/bin/python2
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import socket
import argparse
import logging
import getpass
import re
from ClusterShell.NodeSet import NodeSet
from util.general_utils import pcmd, run_task


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

SLURMD_STARTUP = [
    "systemctl restart slurmd",
    "systemctl enable slurmd"]


def update_config_cmdlist(args):
    """Create the command lines to update slurmd.conf file.

    Args:
        args (Namespace): Commandline arguments

    Returns:
        cmd_list: list of cmdlines to update config file

    """
    all_nodes = NodeSet("{},{}".format(str(args.control), str(args.nodes)))
    if not args.sudo:
        sudo = ""
    else:
        sudo = "sudo"
    # Copy the slurm*example.conf files to /etc/slurm/
    if execute_cluster_cmds(all_nodes, COPY_LIST, args.sudo) > 0:
        exit(1)

    cmd_list = [
        "sed -i -e 's/ControlMachine=linux0/ControlMachine={}/g' {}".format(
            args.control, SLURM_CONF),
        "sed -i -e 's/ClusterName=linux/ClusterName=ci_cluster/g' {}".format(
            SLURM_CONF),
        "sed -i -e 's/SlurmUser=slurm/SlurmUser={}/g' {}".format(
            args.user, SLURM_CONF),
        "sed -i -e 's/NodeName/#NodeName/g' {}".format(
            SLURM_CONF),
        ]

    # This info needs to be gathered from every node that can run a slurm job
    command = r"lscpu | grep -E '(Socket|Core|Thread)\(s\)'"
    task = run_task(all_nodes, command)
    for output, nodes in task.iter_buffers():
        info = {
            data[0]: data[1]
            for data in re.findall(
                r"(Socket|Core|Thread).*:\s+(\d+)", str(output))
            if len(data) > 1}

        if "Socket" not in info or "Core" not in info or "Thread" not in info:
            # Did not find value for socket|core|thread so do not
            # include in config file
            pass
        cmd_list.append("echo \"NodeName={0} Sockets={1} CoresPerSocket={2} "
                        "ThreadsPerCore={3}\" |{4} tee -a {5}".format(
                            NodeSet.fromlist(nodes), info["Socket"],
                            info["Core"], info["Thread"], sudo, SLURM_CONF))

    #
    cmd_list.append("echo \"PartitionName= {} Nodes={} Default=YES "
                    "MaxTime=INFINITE State=UP\" |{} tee -a {}".format(
                        args.partition, args.nodes, sudo, SLURM_CONF))

    return execute_cluster_cmds(all_nodes, cmd_list, args.sudo)


def execute_cluster_cmds(nodes, cmdlist, sudo=False):
    """Execute the list of cmds on hostlist nodes.

    Args:
        nodes (list):  list of nodes
        cmdlist ([type]): list of cmdlines to execute
        sudo (str, optional): Execute cmd with sudo privs. Defaults to false.

     Returns:
        ret_code: returns error code if pcmd fails;

    """
    for cmd in cmdlist:
        if sudo:
            cmd = "sudo {}".format(cmd)
        result = pcmd(nodes, cmd, True, None, 0)
        # if at least one node failed or all nodes failed
        # return on first failure
        if len(result) > 1 or 0 not in result:
            return 1
    return 0


def configuring_packages(args, action):
    """Install required slurm and munge packages.

    Args:
        args (Namespace): Commandline arguments
        action (str):  install or remove

    """
    # Install yum packages on control and compute nodes
    all_nodes = NodeSet("{},{}".format(str(args.control), str(args.nodes)))
    cmd_list = []
    for package in PACKAGE_LIST:
        logging.info("%s %s on %s", action, package, all_nodes)
        cmd_list.append("yum {} -y ".format(action) + package)
    return execute_cluster_cmds(all_nodes, cmd_list, args.sudo)


def start_munge(args):
    """Start munge service on all nodes.

    Args:
        args (Namespace): Commandline arguments

    """
    all_nodes = NodeSet("{},{}".format(str(args.control), str(args.nodes)))
    # exclude the control node
    nodes = NodeSet(str(args.nodes))
    nodes.difference_update(str(args.control))

    # copy key to all nodes FROM slurmctl node;
    # change the protections/ownership on the munge dir on all nodes
    cmd_list = [
        "sudo chmod -R 777 /etc/munge; sudo chown {}. /etc/munge".format(
            args.user)]
    if execute_cluster_cmds(all_nodes, cmd_list) > 0:
        return 1

    # Check if file exists on slurm control node
    # change the protections/ownership on the munge key before copying
    cmd_list = ["set -Eeu",
                "rc=0",
                "if [ ! -f /etc/munge/munge.key ]",
                "then sudo create-munge-key",
                "fi",
                "sudo chmod 777 /etc/munge/munge.key",
                "sudo chown {}. /etc/munge/munge.key".format(args.user)]

    if execute_cluster_cmds(args.control, ["; ".join(cmd_list)]) > 0:
        return 1
    # remove any existing key from other nodes
    cmd_list = ["sudo rm -f /etc/munge/munge.key",
                "scp -p {}:/etc/munge/munge.key /etc/munge/munge.key".format(
                    args.control)]
    if execute_cluster_cmds(nodes, ["; ".join(cmd_list)]) > 0:
        return 1
    # set the protection back to defaults
    cmd_list = [
        "sudo chmod 400 /etc/munge/munge.key",
        "sudo chown munge. /etc/munge/munge.key",
        "sudo chmod 700 /etc/munge",
        "sudo chown munge. /etc/munge"]
    if execute_cluster_cmds(all_nodes, ["; ".join(cmd_list)]) > 0:
        return 1

    # Start Munge service on all nodes
    all_nodes = NodeSet("{},{}".format(str(args.control), str(args.nodes)))
    return execute_cluster_cmds(all_nodes, MUNGE_STARTUP, args.sudo)


def start_slurm(args):
    """Start the slurm services on all nodes.

    Args:
        args (Namespace): Commandline arguments

    """
    # Setting up slurm on all nodes
    all_nodes = NodeSet("{},{}".format(str(args.control), str(args.nodes)))
    cmd_list = [
        "mkdir -p /var/log/slurm",
        "chown {}. {}".format(args.user, "/var/log/slurm"),
        "mkdir -p /var/spool/slurm/d",
        "mkdir -p /var/spool/slurm/ctld",
        "chown {}. {}/ctld".format(args.user, "/var/spool/slurm")
        ]

    if execute_cluster_cmds(all_nodes, cmd_list, args.sudo) > 0:
        return 1

    # Startup the slurm control service
    if execute_cluster_cmds(args.control, SLURMCTLD_STARTUP, args.sudo) > 0:
        return 1

    # Startup the slurm service
    if execute_cluster_cmds(all_nodes, SLURMD_STARTUP, args.sudo) > 0:
        return 1

    # ensure that the nodes are in the idle state
    cmd_list = ["scontrol update nodename={} state=idle".format(
        args.nodes)]
    return execute_cluster_cmds(args.control, cmd_list, args.sudo)


def main():
    """Set up test env with slurm."""
    logging.basicConfig(
        format="%(asctime)s %(levelname)-5s %(message)s",
        datefmt=r"%Y/%m/%d %I:%M:%S",
        name="slurm_setup", level=logging.DEBUG)

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

    args = parser.parse_args()
    logging.info("Arguments: %s", args)

    # Check params
    if args.nodes is None:
        logging.error("slurm_nodes: Specify at least one slurm node")
        exit(1)

    # Convert control node and slurm node list into NodeSets
    args.control = NodeSet(args.control)
    args.nodes = NodeSet(args.nodes)

    # Remove packages if specified with --remove and then exit
    if args.remove:
        ret_code = configuring_packages(args, "remove")
        if ret_code > 0:
            exit(1)
        exit(0)

    # Install packages if specified with --install and continue with setup
    if args.install:
        ret_code = configuring_packages(args, "install")
        if ret_code > 0:
            exit(1)

    # Edit the slurm conf files
    ret_code = update_config_cmdlist(args)
    if ret_code > 0:
        exit(1)

    # Munge Setup
    ret_code = start_munge(args)
    if ret_code > 0:
        exit(1)

    # Slurm Startup
    ret_code = start_slurm(args)
    if ret_code > 0:
        exit(1)

    exit(0)


if __name__ == "__main__":
    main()
