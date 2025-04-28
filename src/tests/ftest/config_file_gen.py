#!/usr/bin/env python3
"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=fixme
# TODO: Find out why this is required and remove it.
# pylint: disable=import-error,no-name-in-module

import logging
import sys
from argparse import ArgumentParser, RawDescriptionHelpFormatter

from util.agent_utils_params import DaosAgentTransportCredentials, DaosAgentYamlParameters
from util.command_utils_base import CommonConfig
from util.dmg_utils_params import DmgTransportCredentials, DmgYamlParameters
from util.exception_utils import CommandFailure
from util.server_utils_params import DaosServerTransportCredentials, DaosServerYamlParameters


def generate_agent_config(args):
    """Generate a daos_agent configuration file.

    Args:
        args (argparse.Namespace): command line arguments for this program

    Returns:
        bool: status of creating the daos_agent configuration file

    """
    common_cfg = CommonConfig(args.group_name, DaosAgentTransportCredentials())
    config = DaosAgentYamlParameters(args.agent_file, common_cfg)
    # Update the configuration file access points
    config.access_points.value = args.node_list.split(",")
    return create_config(args, config)


def generate_server_config(args):
    """Generate a daos_server configuration file.

    Args:
        args (argparse.Namespace): command line arguments for this program

    Returns:
        bool: status of creating the daos_server configuration file

    """
    common_cfg = CommonConfig(args.group_name, DaosServerTransportCredentials())
    config = DaosServerYamlParameters(args.server_file, common_cfg)
    config.engine_params[0].storage.storage_tiers[0].storage_class.value = "ram"
    config.engine_params[0].storage.storage_tiers[0].scm_mount.value = "/mnt/daos"
    config.engine_params[0].storage.storage_tiers[0].scm_size.value = 0
    # Update the configuration file MS replicas
    config.mgmt_svc_replicas.value = args.node_list.split(",")
    return create_config(args, config)


def generate_dmg_config(args):
    """Generate a dmg configuration file.

    Args:
        args (argparse.Namespace): command line arguments for this program

    Returns:
        bool: status of creating the dmg configuration file

    """
    config = DmgYamlParameters(
        args.dmg_file, args.group_name, DmgTransportCredentials())
    # Update the configuration file hostlist
    config.hostlist.value = args.node_list.split(",")
    return create_config(args, config)


def create_config(args, config):
    """Create the configuration file.

    Args:
        args (argparse.Namespace): command line arguments for this program
        config (YamlParameters): object that defines the contents of the
            configuration file

    Returns:
        bool: status of creating the configuration file

    """
    if args.port:
        config.other_params.port.value = args.port

    # Write the configuration file
    try:
        config.create_yaml()
    except CommandFailure as error:
        log = logging.getLogger(__name__)
        log.error("Error: %s", error)
        return False

    return True


def main():
    """Launch DAOS functional tests."""
    # Setup logging
    log_format = "%(asctime)s - %(levelname)-5s - %(funcName)-15s: %(message)s"
    console = logging.StreamHandler()
    console.setLevel(logging.DEBUG)
    console.setFormatter(logging.Formatter(log_format))
    logging.basicConfig(
        format=log_format, datefmt=r"%Y/%m/%d %I:%M:%S", level=logging.DEBUG,
        handlers=[console])
    log = logging.getLogger()

    # Parse the command line arguments
    description = [
        "DAOS Configuration File Generator",
        "",
        "Generates daos_server and daos_agent configuration files",
        "",
        "Examples:",
        "\tconfig_file_gen.py -n host-10 -g daos_server -a "
        "/etc/daos/daos_agent.yml -s /etc/daos/daos_server.yml",
        "",
        "Return codes:",
        "\t0 - all configuration files generated successfully",
        "\t1 - one configuration file generation failed",
        "\t2 - both configuration file generations failed",
    ]
    parser = ArgumentParser(
        prog="config_file_gen.py",
        formatter_class=RawDescriptionHelpFormatter,
        description="\n".join(description))
    parser.add_argument(
        "-a", "--agent_file",
        action="store",
        type=str,
        default=None,
        help="name and path of the daos_agent configuration file to create")
    parser.add_argument(
        "-d", "--dmg_file",
        action="store",
        type=str,
        default=None,
        help="name and path of the dmg configuration file to create")
    parser.add_argument(
        "-g", "--group_name",
        action="store",
        type=str,
        default="daos_server",
        help="server group name")
    parser.add_argument(
        "-n", "--node_list",
        action="store",
        type=str,
        default=None,
        help="comma-separated list of node names to use as the MS replicas")
    parser.add_argument(
        "-p", "--port",
        action="store",
        type=int,
        default=None,
        help="the MS replica port")
    parser.add_argument(
        "-s", "--server_file",
        action="store",
        type=str,
        default=None,
        help="name and path of the daos_server configuration file to create")
    args = parser.parse_args()
    status = 0

    # Create a daos_agent configuration file if a filename was provided
    if args.agent_file:
        log.info(
            "Generating the daos_agent configuration file: %s",
            args.agent_file)
        if not generate_agent_config(args):
            status += 1

    # Create a daos_server configuration file if a filename was provided
    if args.server_file:
        log.info(
            "Generating the daos_server configuration file: %s",
            args.server_file)
        if not generate_server_config(args):
            status += 1

    # Create a dmg configuration file if a filename was provided
    if args.dmg_file:
        log.info(
            "Generating the dmg configuration file: %s",
            args.dmg_file)
        if not generate_dmg_config(args):
            status += 1

    sys.exit(status)


if __name__ == "__main__":
    main()
