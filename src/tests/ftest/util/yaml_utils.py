#!/usr/bin/python3
"""
(C) Copyright 2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from argparse import ArgumentParser
from collections import OrderedDict
import os
import re
import yaml

from ClusterShell.NodeSet import NodeSet

from exception_utils import CommandFailure
from general_utils import run_task, display_task
from logger_utils import get_console_logger, getLogger

YAML_KEYS = OrderedDict(
    [
        ("test_servers", "test_servers"),
        ("test_clients", "test_clients"),
        ("bdev_list", "nvme"),
        ("timeout", "timeout_multiplier"),
        ("timeouts", "timeout_multiplier"),
        ("clush_timeout", "timeout_multiplier"),
        ("ior_timeout", "timeout_multiplier"),
        ("job_manager_timeout", "timeout_multiplier"),
        ("pattern_timeout", "timeout_multiplier"),
        ("pool_query_timeout", "timeout_multiplier"),
        ("rebuild_timeout", "timeout_multiplier"),
        ("srv_timeout", "timeout_multiplier"),
        ("storage_prepare_timeout", "timeout_multiplier"),
        ("storage_format_timeout", "timeout_multiplier"),
    ]
)


def get_device_replacement(args):
    """Determine the value to use for the '--nvme' command line argument.

    Determine if the specified hosts have homogeneous NVMe drives (either standalone or VMD
    controlled) and use these values to replace placeholder devices in avocado test yaml files.

    Supported auto '--nvme' arguments:
        auto[:filter]       = select any PCI domain number of a NVMe device or VMD controller
                              (connected to a VMD enabled NVMe device) in the homogeneous 'lspci -D'
                              output from each server.  Optionally grep the list of NVMe or VMD
                              enabled NVMe devices for 'filter'.
        auto_nvme[:filter]  = select any PCI domain number of a non-VMD controlled NVMe device in
                              the homogeneous 'lspci -D' output from each server.  Optionally grep
                              this output for 'filter'.
        auto_vmd[:filter]   = select any PCI domain number of a VMD controller connected to a VMD
                              enabled NVMe device in the homogeneous 'lspci -D' output from each
                              server.  Optionally grep the list of VMD enabled NVMe devices for
                              'filter'.

    Args:
        args (argparse.Namespace): command line arguments for this program

    Raises:
        CommandFailure: if no VMD or NVMe devices were found

    Returns:
        str: a comma-separated list of nvme device pci addresses available on all of the specified
            test servers

    """
    log = getLogger()
    devices = []
    device_types = []
    host_list = args.test_servers.copy()

    # Separate any optional filter from the key
    dev_filter = None
    nvme_args = args.nvme.split(":")
    if len(nvme_args) > 1:
        dev_filter = nvme_args[1]

    # First check for any VMD disks, if requested
    if nvme_args[0] in ["auto", "auto_vmd"]:
        vmd_devices = auto_detect_devices(host_list, "NVMe", "5", dev_filter)
        if vmd_devices:
            # Find the VMD controller for the matching VMD disks
            vmd_controllers = auto_detect_devices(host_list, "VMD", "4", None)
            devices.extend(get_vmd_address_backed_nvme(host_list, vmd_devices, vmd_controllers))
        elif not dev_filter:
            # Use any VMD controller if no VMD disks found w/o a filter
            devices = auto_detect_devices(host_list, "VMD", "4", None)
        if devices:
            device_types.append("VMD")

    # Second check for any non-VMD NVMe disks, if requested
    if nvme_args[0] in ["auto", "auto_nvme"]:
        dev_list = auto_detect_devices(host_list, "NVMe", "4", dev_filter)
        if dev_list:
            devices.extend(dev_list)
            device_types.append("NVMe")

    # If no VMD or NVMe devices were found raise an exception
    if not devices:
        raise CommandFailure(
            "Unable to auto-detect devices for the '--nvme {}' argument".format(args.nvme))

    log.info(
        "Auto-detected %s devices on %s: %s", " & ".join(device_types), args.test_servers, devices)
    return ",".join(devices)


def auto_detect_devices(host_list, device_type, length, device_filter=None):
    """Get a list of NVMe/VMD devices found on each specified host.

    Args:
        host_list (NodeSet): hosts on which to find the NVMe/VMD devices
        device_type (str): device type to find, e.g. 'NVMe' or 'VMD'
        length (str): number of digits to match in the first PCI domain number
        device_filter (str, optional): optional filter to apply to device searching. Defaults to
            None.

    Raises:
        CommandFailure: if an invalid device_type is specified

    Returns:
        list: A list of detected devices - empty if none found

    """
    log = getLogger()
    devices = []

    # Find the devices on each host
    if device_type == "VMD":
        # Exclude the controller revision as this causes heterogeneous clush output
        command_list = [
            "/sbin/lspci -D",
            "grep -E '^[0-9a-f]{{{0}}}:[0-9a-f]{{2}}:[0-9a-f]{{2}}.[0-9a-f] '".format(length),
            "grep -E 'Volume Management Device NVMe RAID Controller'",
            r"sed -E 's/\(rev\s+([a-f0-9])+\)//I'"]
    elif device_type == "NVMe":
        command_list = [
            "/sbin/lspci -D",
            "grep -E '^[0-9a-f]{{{0}}}:[0-9a-f]{{2}}:[0-9a-f]{{2}}.[0-9a-f] '".format(length),
            "grep -E 'Non-Volatile memory controller:'"]
        if device_filter and device_filter.startswith("-"):
            command_list.append("grep -v '{}'".format(device_filter[1:]))
        elif device_filter:
            command_list.append("grep '{}'".format(device_filter))
    else:
        raise CommandFailure(
            "Invalid 'device_type' for NVMe/VMD auto-detection: {}".format(device_type))

    command = " | ".join(command_list) + " || :"
    task = run_task(host_list, command, verbose=True)

    # Verify the command was successful on each server host
    if display_task(task):
        # Verify each server host has the same VMD PCI addresses
        output_data = list(task.iter_buffers())
        if len(output_data) > 1:
            log.error("ERROR: Non-homogeneous %s PCI addresses.", device_type)
        elif len(output_data) == 1:
            # Get the devices from the successful, homogeneous command output
            output_str = "\n".join([line.decode("utf-8") for line in output_data[0][0]])
            devices = find_pci_address(output_str)
    else:
        log.error("ERROR: Issuing command '%s'", command)

    return devices


def get_vmd_address_backed_nvme(host_list, vmd_disks, vmd_controllers):
    """Find valid VMD address which has backing NVMe.

    Args:
        host_list (NodeSet): hosts on which to find the VMD addresses
        vmd_disks (list): list of PCI domain numbers for each VMD controlled disk
        vmd_controllers (list): list of PCI domain numbers for each VMD controller

    Returns:
        list: a list of the VMD controller PCI domain numbers which are connected to the VMD disks

    """
    log = getLogger()
    disk_controllers = []
    command_list = ["ls -l /sys/block/", "grep nvme"]
    if vmd_disks:
        command_list.append("grep -E '({0})'".format("|".join(vmd_disks)))
    command_list.extend(["cut -d'>' -f2", "cut -d'/' -f4"])
    command = " | ".join(command_list) + " || :"
    task = run_task(host_list, command, verbose=True)

    # Verify the command was successful on each server host
    if not display_task(task):
        log.error("ERROR: Issuing command '%s'", command)
    else:
        # Verify each server host has the same NVMe devices behind the same VMD addresses.
        output_data = list(task.iter_buffers())
        if len(output_data) > 1:
            log.error("ERROR: Non-homogeneous NVMe device behind VMD addresses.")
        elif len(output_data) == 1:
            # Add any VMD controller addresses found in the /sys/block output that are also
            # included in the provided list of VMD controllers.
            output_str = "\n".join([line.decode("utf-8") for line in output_data[0][0]])
            for device in vmd_controllers:
                if device in output_str:
                    disk_controllers.append(device)

    return disk_controllers


def find_pci_address(value):
    """Find PCI addresses in the specified string.

    Args:
        value (str): string to search for PCI addresses

    Returns:
        list: a list of all the PCI addresses found in the string

    """
    pattern = r"[{0}]{{4,5}}:[{0}]{{2}}:[{0}]{{2}}\.[{0}]".format("0-9a-fA-F")
    return re.findall(pattern, str(value))


def update_nvme_argument(args):
    """Replace any keywords in the nvme argument with detected values.

    Args:
        args (argparse.Namespace): command line arguments for this program
    """
    # Auto-detect nvme test yaml replacement values if requested
    if args.nvme and args.nvme.startswith("auto") and (not hasattr(args, "list") or not args.list):
        args.nvme = get_device_replacement(args)
    # elif args.nvme and args.nvme.startswith("vmd:"):
    #     args.nvme = args.nvme.replace("vmd:", "")


def get_yaml_data(yaml_file):
    """Get the contents of a yaml file as a dictionary.

    This will ignoring any other tags, like !mux.

    Args:
        yaml_file (str): yaml file to read

    Raises:
        CommandFailure: if an error is encountered reading the yaml file

    Returns:
        dict: the contents of the yaml file

    """
    class DaosLoader(yaml.SafeLoader):  # pylint: disable=too-many-ancestors
        """Helper class for parsing avocado yaml files."""

        def forward_mux(self, node):
            """Pass on mux tags unedited."""
            return self.construct_mapping(node)

        def ignore_unknown(self, node):  # pylint: disable=no-self-use,unused-argument
            """Drop any other tag."""
            return None

    DaosLoader.add_constructor('!mux', DaosLoader.forward_mux)
    DaosLoader.add_constructor(None, DaosLoader.ignore_unknown)

    yaml_data = {}
    if os.path.isfile(yaml_file):
        with open(yaml_file, "r") as open_file:
            try:
                yaml_data = yaml.load(open_file.read(), Loader=DaosLoader)
            except yaml.YAMLError as error:
                raise CommandFailure("Error reading {}".format(yaml_file)) from error
    return yaml_data


def find_values(obj, keys, key=None, val_type=list):
    """Find dictionary values of a certain type specified with certain keys.

    Args:
        obj (obj): a python object; initially the dictionary to search
        keys (list): list of keys to find their matching list values
        key (str, optional): key to check for a match. Defaults to None.

    Returns:
        dict: a dictionary of each matching key and its value

    """
    def add_matches(found):
        """Add found matches to the match dictionary entry of the same key.

        If a match does not already exist for this key add all the found values.
        When a match already exists for a key, append the existing match with
        any new found values.

        For example:
            Match       Found           Updated Match
            ---------   ------------    -------------
            None        [A, B]          [A, B]
            [A, B]      [C]             [A, B, C]
            [A, B, C]   [A, B, C, D]    [A, B, C, D]

        Args:
            found (dict): dictionary of matches found for each key
        """
        for found_key in found:
            if found_key not in matches:
                # Simply add the new value found for this key
                matches[found_key] = found[found_key]

            else:
                is_list = isinstance(matches[found_key], list)
                if not is_list:
                    matches[found_key] = [matches[found_key]]
                if isinstance(found[found_key], list):
                    for found_item in found[found_key]:
                        if found_item not in matches[found_key]:
                            matches[found_key].append(found_item)
                elif found[found_key] not in matches[found_key]:
                    matches[found_key].append(found[found_key])

                if not is_list and len(matches[found_key]) == 1:
                    matches[found_key] = matches[found_key][0]

    matches = {}
    if isinstance(obj, val_type) and isinstance(key, str) and key in keys:
        # Match found
        matches[key] = obj
    elif isinstance(obj, dict):
        # Recursively look for matches in each dictionary entry
        for obj_key, obj_val in list(obj.items()):
            add_matches(find_values(obj_val, keys, obj_key, val_type))
    elif isinstance(obj, list):
        # Recursively look for matches in each list entry
        for item in obj:
            add_matches(find_values(item, keys, None, val_type))

    return matches


def replace_yaml_entries(yaml_file, args):
    """_summary_.

    Args:
        yaml_file (str): _description_
        args (argparse.Namespace): command line arguments for this program

    Raises:
        CommandFailure: _description_

    Returns:
        str: modified yaml file

    """
    log = getLogger()
    if not os.path.isfile(yaml_file):
        raise CommandFailure(
            "Error replacing yaml entries in {}: file does not exist".format(yaml_file))

    yaml_data = get_yaml_data(yaml_file)
    yaml_keys = list(YAML_KEYS.keys())
    yaml_find = find_values(yaml_data, yaml_keys, val_type=(str, list, int, dict))

    # Generate a list of values that can be used as replacements
    user_values = OrderedDict()
    for key, value in list(YAML_KEYS.items()):
        args_value = getattr(args, value)
        if isinstance(args_value, NodeSet):
            user_values[key] = args_value.copy()
        elif isinstance(args_value, str):
            user_values[key] = args_value.split(",")
        elif args_value:
            user_values[key] = [args_value]
        else:
            user_values[key] = None

    # Assign replacement values for the test yaml entries to be replaced
    log.info("Detecting replacements for %s in %s", yaml_keys, yaml_file)
    log.info("  Found values: %s", yaml_find)
    log.info("  User values:  %s", dict(user_values))

    return ""


def add_arguments(parser):
    """Add yaml modification parameters to the provided command line parser.

    Args:
        parser (ArgumentParser): command line argument parser
    """
    parser.add_argument(
        "-e", "--extra_yaml",
        action="store",
        default=None,
        type=str,
        help="additional yaml file to include with the test yaml file. Any "
             "entries in the extra yaml file can be used to replace an "
             "existing entry in the test yaml file.")
    parser.add_argument(
        "-n", "--nvme",
        action="store",
        help="comma-separated list of NVMe device PCI addresses to use as "
             "replacement values for the bdev_list in each test's yaml file.  "
             "Using the 'auto[:<filter>]' keyword will auto-detect any VMD "
             "controller or NVMe PCI address list on each of the '--test_servers' "
             "hosts - the optional '<filter>' can be used to limit auto-detected "
             "NVMe addresses, e.g. 'auto:Optane' for Intel Optane NVMe devices.  "
             "To limit the device detection to either VMD controller or NVMe "
             "devices the 'auto_vmd[:filter]' or 'auto_nvme[:<filter>]' keywords "
             "can be used, respectively.  When using 'filter' with VMD controllers, "
             "the filter is applied to devices managed by the controller, therefore "
             "only selecting controllers that manage the matching devices.")
    parser.add_argument(
        "-o",
        "--override",
        action="store_true",
        help="allow test yaml lists and NodeSets to be reduced or increased based upon "
             "the user provided replacement values.  A separate --test_servers and "
             "--test_clients entry must be specified when using this option.")
    parser.add_argument(
        "-tc", "--test_clients",
        action="store",
        help="comma-separated list of hosts to use as replacement values for "
             "client placeholders in each test's yaml file")
    parser.add_argument(
        "-tm", "--timeout_multiplier",
        action="store",
        default=None,
        type=float,
        help="a multiplier to apply to each timeout value found in the test yaml")
    parser.add_argument(
        "-ts", "--test_servers",
        action="store",
        help="comma-separated list of hosts to use as replacement values for "
             "server placeholders in each test's yaml file.  If the "
             "'--test_clients' argument is not specified, this list of hosts "
             "will also be used to replace client placeholders.")
    parser.add_argument(
        "-y", "--yaml_directory",
        action="store",
        default=None,
        help="directory in which to write the modified yaml files. A temporary "
             "directory - which only exists for the duration of the launch.py "
             "command - is used by default.")


def add_arguments_verbose(parser):
    """Add yaml modification parameters to the provided command line parser.

    Args:
        parser (ArgumentParser): command line argument parser
    """
    parser.add_argument(
        "-v", "--verbose",
        action="count",
        default=0,
        help="verbosity output level. Specify multiple times (e.g. -vv) for "
             "additional output")


def main():
    """Modify yaml files."""
    log = get_console_logger()
    log.info("--- YAML UTILS ---")

    parser = ArgumentParser(prog="yaml_utils.py", description="Utilities used to modify yaml files")
    add_arguments(parser)
    add_arguments_verbose(parser)
    parser.add_argument("files", nargs="*", type=str, help="yaml files to modify")
    args = parser.parse_args()
    log.debug("Arguments: %s", args)

    # Convert host specifications into NodeSets
    args.test_servers = NodeSet(args.test_servers)
    args.test_clients = NodeSet(args.test_clients)

    # Auto-detect nvme test yaml replacement values if requested
    update_nvme_argument(args)

    for item in args.files:
        if os.path.isfile(item):
            replace_yaml_entries(item, args)


if __name__ == "__main__":
    main()
