#!/usr/bin/python2
"""
  (C) Copyright 2018-2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""

import socket
import argparse
import logging
import os
import pwd
import re
from general_utils import pcmd, run_task
from ClusterShell.NodeSet import NodeSet

slurm_conf = "/etc/slurm/slurm.conf"
slurmdbd_conf = "/etc/slurm/slurmdbd.conf"

package_list = ["munge-devel", "munge-libs",
                "readline-devel", "perl-ExtUtils-MakeMaker",
                "openssl-devel", "pam-devel", "rpm-build",
                "perl-DBI", "perl-Switch", "munge", "mariadb-devel",
                "slurm", "slurm-contribs", "slurm-devel",
                "slurm-example-configs", "slurm-libpmi", "slurm-openlava",
                "slurm-pam_slurm", "slurm-perlapi", " slurm-slurmctld",
                "slurm-slurmd", "slurm-slurmdbd", "slurm-torque",
                "mariadb-server"]

copy_list = ["cp /etc/slurm/slurm.conf.example /etc/slurm/slurm.conf",
             "cp /etc/slurm/cgroup.conf.example /etc/slurm/cgroup.conf",
             "cp /etc/slurm/slurmdbd.conf.example /etc/slurm/slurmdbd.conf"]

munge_startup = [
    "chown munge. {0}".format("/etc/munge/munge.key"),
    "systemctl restart munge",
    "systemctl enable munge"]

slurmctl_startup = [
    "systemctl restart slurmctld",
    "systemctl enable slurmctld"]

slurm_srv_startup = [
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

    # Copy the slurm*example.conf files to /etc/slurm/
    if execute_cluster_cmds(all_nodes, copy_list, args.sudo) > 0:
        exit(1)

    cmd_list = [
        "sed -i -e 's/ControlMachine=linux0/ControlMachine={}/g' {}".format(
            args.control, slurm_conf),
        "sed -i -e 's/ClusterName=linux/ClusterName=ci_cluster/g' {}".format(
            slurm_conf),
        "sed -i -e 's/SlurmUser=slurm/SlurmUser={}/g' {}".format(
            args.user, slurm_conf),
        "sed -i -e 's/NodeName/#NodeName/g' {}".format(
            slurm_conf),
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
                        "ThreadsPerCore={3}\" >> {4}".format(
                            NodeSet.fromlist(nodes), info["Socket"],
                            info["Core"], info["Thread"], slurm_conf))

    #
    cmd_list.append("echo \"PartitionName=daos_client Nodes={} Default=YES "
                    "MaxTime=INFINITE State=UP\" >> {}".format(
                        args.nodes, slurm_conf))

    if execute_cluster_cmds(all_nodes, cmd_list, args.sudo) > 0:
        exit(1)


def execute_cluster_cmds(nodes, cmdlist, sudo=False, timeout=None):
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
    for package in package_list:
        logging.info("{} {} on {}".format(action, package, all_nodes))
        cmd_list.append("yum {} -y ".format(action) + package)
    if execute_cluster_cmds(all_nodes, cmd_list, args.sudo) > 0:
        exit(1)


def start_munge(args):
    """Start munge service on all nodes.

    Args:
        args (Namespace): Commandline arguments

    """
    # Check if file exists on slurm control node
    if execute_cluster_cmds(args.control, ["ls /etc/munge/munge.key"]) > 0:
        # Create one key on control node and then copy it to all slurm nodes
        if execute_cluster_cmds(
                    args.control, ["create-munge-key"], args.sudo) > 0:
            exit(1)

    # copy key to all nodes FROM slurmctl node;
    cmd_list = [
        "clush --copy -p -w {} /etc/munge/munge.key".format(args.nodes)]
    if execute_cluster_cmds(args.control, cmd_list, args.sudo) > 0:
        exit(1)

    # Start Munge service on all nodes
    all_nodes = NodeSet("{},{}".format(str(args.control), str(args.nodes)))
    if execute_cluster_cmds(all_nodes, munge_startup, args.sudo) > 0:
        exit(1)


def start_slurm(args):
    """Start the slurm services on all nodes.

    Args:
        args (Namespace): Commandline arguments

    """
    # Setting up slurm on all nodes
    all_nodes = NodeSet("{},{}".format(str(args.control), str(args.nodes)))
    cmd_list = [
        "mkdir -p {0};chown {1}. {0}".format("/var/log/slurm", args.user),
        "mkdir -p {0}d;mkdir -p {0}ctld;chown {1}. {0}ctld".format(
            "/var/spool/slurm/", args.user)
        ]
    if execute_cluster_cmds(all_nodes, cmd_list, args.sudo) > 0:
        exit(1)

    # Startup the slurm control service
    if execute_cluster_cmds(args.control, slurmctl_startup, args.sudo) > 0:
        exit(1)
    else:
        # Startup the slurm service
        if execute_cluster_cmds(all_nodes, slurm_srv_startup, args.sudo) > 0:
            exit(1)


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
        "-u", "--user",
        default=pwd.getpwuid(os.geteuid()).pw_name,
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
    logging.info("Arguments: {}".format(args))

    # Check params
    if args.nodes is None:
        logging.error("slurm_nodes: Specify at least one slurm node")
        exit(1)

    # Convert control node and slurm node list into NodeSets
    args.control = NodeSet(args.control)
    args.nodes = NodeSet(args.nodes)

    # Remove packages if specified with --remove and then exit
    if args.remove:
        configuring_packages(args, "remove")
        exit(0)

    # Install packages if specified with --install
    if args.install:
        configuring_packages(args, "install")

    # Edit the slurm conf files
    update_config_cmdlist(args)

    # Munge Setup
    start_munge(args)

    # Slurm Startup
    start_slurm(args)

    exit(0)


if __name__ == "__main__":
    main()
