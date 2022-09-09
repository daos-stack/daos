#!/usr/bin/env python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines

from argparse import ArgumentParser, RawDescriptionHelpFormatter
from collections import OrderedDict, defaultdict
from tempfile import TemporaryDirectory
import errno
import fnmatch
import json
import logging
import os
import re
import socket
import subprocess   # nosec
import site
import sys
import time
import yaml

# When SRE-439 is fixed we should be able to include these import statements here
# from avocado.core.settings import settings
# from avocado.core.version import MAJOR, MINOR
from ClusterShell.NodeSet import NodeSet
from ClusterShell.Task import task_self

# When SRE-439 is fixed we should be able to include these import statements here
# from util.distro_utils import detect
# from util.general_utils import run_remote, RemoteCommandResult
from util.results_utils import create_html, create_xml, Job, Results, TestResult

DEFAULT_DAOS_TEST_LOG_DIR = "/var/tmp/daos_testing"
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
PROVIDER_KEYS = OrderedDict(
    [
        ("cxi", "ofi+cxi"),
        ("verbs", "ofi+verbs"),
        ("ucx", "ucx+dc_x"),
        ("tcp", "ofi+tcp"),
    ]
)


class LaunchException(Exception):
    """Exception for launch.py execution."""


def get_local_host():
    """Get the local host name.

    Returns:
        str: name of the local host
    """
    return socket.gethostname().split(".")[0]


def run_local(log, command, capture_output=True, timeout=None, check=False):
    """Run the command locally.

    Args:
        log (logger): logger for the messages produced by this method
        command (list): command from which to obtain the output
        capture_output(bool, optional): whether or not to include the command output in the
            subprocess.CompletedProcess.stdout returned by this method. Defaults to True.
        timeout (int, optional): number of seconds to wait for the command to complete.
            Defaults to None.
        check (bool, optional): if set the method will raise an exception if the command does not
            yield a return code equal to zero. Defaults to False.

    Raises:
        LaunchException: if the command fails: times out (timeout must be specified),
            yields a non-zero exit status (check must be True), is interrupted by the user, or
            encounters some other exception.

    Returns:
        subprocess.CompletedProcess: an object representing the result of the command execution with
            the following properties:
                - args (the command argument)
                - returncode
                - stdout (only set if capture_output=True)
                - stderr (not used; included in stdout)

    """
    local_host = get_local_host()
    command_str = " ".join(command)
    kwargs = {"encoding": "utf-8", "shell": False, "check": check, "timeout": timeout}
    if capture_output:
        kwargs["stdout"] = subprocess.PIPE
        kwargs["stderr"] = subprocess.STDOUT
    if timeout:
        log.debug("Running on %s with a %s timeout: %s", local_host, timeout, command_str)
    else:
        log.debug("Running on %s: %s", local_host, command_str)

    try:
        # pylint: disable=subprocess-run-check
        result = subprocess.run(command, **kwargs)

    except subprocess.TimeoutExpired as error:
        # Raised if command times out
        log.debug(str(error))
        log.debug("  output: %s", error.output)
        log.debug("  stderr: %s", error.stderr)
        raise LaunchException(f"Command '{command_str}' exceed {timeout}s timeout") from error

    except subprocess.CalledProcessError as error:
        # Raised if command yields a non-zero return status with check=True
        log.debug(str(error))
        log.debug("  output: %s", error.output)
        log.debug("  stderr: %s", error.stderr)
        raise LaunchException(f"Command '{command_str}' returned non-zero status") from error

    except KeyboardInterrupt as error:
        # User Ctrl-C
        message = f"Command '{command_str}' interrupted by user"
        log.debug(message)
        raise LaunchException(message) from error

    except Exception as error:
        # Catch all
        message = f"Command '{command_str}' encountered unknown error"
        log.debug(message)
        log.debug(str(error))
        raise LaunchException(message) from error

    if capture_output:
        # Log the output of the command
        log.debug("  %s (rc=%s):", local_host, result.returncode)
        if result.stdout:
            for line in result.stdout.splitlines():
                log.debug("    %s", line)

    return result


def run_remote(log, hosts, command, timeout=120):
    """Run the command on the remote hosts.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to run the command
        command (str): command from which to obtain the output
        timeout (int, optional): number of seconds to wait for the command to complete.
            Defaults to 120 seconds.

    Returns:
        RemoteCommandResult: a grouping of the command results from the same hosts with the same
            return status

    """
    task = task_self()
    # task.set_info('debug', True)
    # Enable forwarding of the ssh authentication agent connection
    task.set_info("ssh_options", "-oForwardAgent=yes")
    log.debug("Running on %s with a %s second timeout: %s", hosts, timeout, command)
    task.run(command=command, nodes=hosts, timeout=timeout)
    results = RemoteCommandResult(command, task)
    results.log_output(log)
    return results


def get_build_environment(args):
    """Obtain DAOS build environment variables from the .build_vars.json file.

    Returns:
        dict: a dictionary of DAOS build environment variable names and values

    """
    build_vars_file = os.path.join(os.path.dirname(os.path.realpath(__file__)),
                                   "../../.build_vars.json")
    try:
        with open(build_vars_file, encoding="utf-8") as vars_file:
            return json.load(vars_file)
    except ValueError:
        if not args.list:
            raise
        return json.loads(f'{{"PREFIX": "{os.getcwd()}"}}')
    except IOError as error:
        if error.errno == errno.ENOENT:
            if not args.list:
                raise
            return json.loads(f'{{"PREFIX": "{os.getcwd()}"}}')
    # Pylint warns about possible return types if we take this path, so ensure we do not.
    assert False


def set_test_environment(log, args):
    """Set up the test environment.

    Args:
        log (logger): logger for the messages produced by this method
        args (argparse.Namespace): command line arguments for this program

    Raises:
        LaunchException: if there is a problem setting up the test environment

    """
    base_dir = get_build_environment(args)["PREFIX"]
    bin_dir = os.path.join(base_dir, "bin")
    sbin_dir = os.path.join(base_dir, "sbin")
    # /usr/sbin is not setup on non-root user for CI nodes.
    # SCM formatting tool mkfs.ext4 is located under
    # /usr/sbin directory.
    usr_sbin = os.path.sep + os.path.join("usr", "sbin")
    path = os.environ.get("PATH")

    if not args.list:
        # Get the default fabric_iface value (DAOS_TEST_FABRIC_IFACE)
        set_interface_environment(log, args)

        # Get the default provider if CRT_PHY_ADDR_STR is not set
        set_provider_environment(log, os.environ["DAOS_TEST_FABRIC_IFACE"], args)

        # Set the default location for daos log files written during testing
        # if not already defined.
        if "DAOS_TEST_LOG_DIR" not in os.environ:
            os.environ["DAOS_TEST_LOG_DIR"] = DEFAULT_DAOS_TEST_LOG_DIR
        os.environ["D_LOG_FILE"] = os.path.join(
            os.environ["DAOS_TEST_LOG_DIR"], "daos.log")
        os.environ["D_LOG_FILE_APPEND_PID"] = "1"

        # Assign the default value for transport configuration insecure mode
        os.environ["DAOS_INSECURE_MODE"] = str(args.insecure_mode)

    # Update PATH
    os.environ["PATH"] = ":".join([bin_dir, sbin_dir, usr_sbin, path])
    os.environ["COVFILE"] = "/tmp/test.cov"

    # Python paths required for functional testing
    set_python_environment(log)

    log.debug("ENVIRONMENT VARIABLES")
    for key in sorted(os.environ):
        if not key.startswith("BASH_FUNC_"):
            log.debug("  %s: %s", key, os.environ[key])


def set_interface_environment(log, args):
    """Set up the interface environment variables.

    Use the existing OFI_INTERFACE setting if already defined, or select the fastest, active
    interface on this host to define the DAOS_TEST_FABRIC_IFACE environment variable.

    The DAOS_TEST_FABRIC_IFACE defines the default fabric_iface value in the daos_server
    configuration file.

    Args:
        log (logger): logger for the messages produced by this method
        args (argparse.Namespace): command line arguments for this program

    Raises:
        LaunchException: if there is a problem obtaining the default interface

    """
    log.debug("-" * 80)
    # Get the default interface to use if OFI_INTERFACE is not set
    interface = os.environ.get("OFI_INTERFACE")
    if interface is None:
        # Find all the /sys/class/net interfaces on the launch node
        # (excluding lo)
        log.debug("Detecting network devices - OFI_INTERFACE not set")
        available_interfaces = get_available_interfaces(log, args)
        try:
            # Select the fastest active interface available by sorting
            # the speed
            interface = available_interfaces[sorted(available_interfaces)[-1]]
        except IndexError as error:
            raise LaunchException("Error obtaining a default interface!") from error

    # Update env definitions
    os.environ["CRT_CTX_SHARE_ADDR"] = "0"
    os.environ["DAOS_TEST_FABRIC_IFACE"] = interface
    log.info("Testing with interface:   %s", interface)
    for name in ("OFI_INTERFACE", "DAOS_TEST_FABRIC_IFACE", "CRT_CTX_SHARE_ADDR"):
        try:
            log.debug("Testing with %s=%s", name, os.environ[name])
        except KeyError:
            log.debug("Testing with %s unset", name)


def get_available_interfaces(log, args):
    # pylint: disable=too-many-nested-blocks,too-many-branches,too-many-locals
    """Get a dictionary of active available interfaces and their speeds.

    Args:
        log (logger): logger for the messages produced by this method
        args (argparse.Namespace): command line arguments for this program

    Raises:
        LaunchException: if there is a problem finding active network interfaces

    Returns:
        dict: a dictionary of speeds with the first available active interface providing that speed

    """
    available_interfaces = {}
    all_hosts = NodeSet()
    all_hosts.update(args.test_servers)
    all_hosts.update(args.test_clients)

    # Find any active network interfaces on the server or client hosts
    net_path = os.path.join(os.path.sep, "sys", "class", "net")
    operstate = os.path.join(net_path, "*", "operstate")
    command = " | ".join([f"grep -l 'up' {operstate}", "grep -Ev '/(lo|bonding_masters)/'", "sort"])

    result = run_remote(log, all_hosts, command)
    if not result.passed:
        raise LaunchException(
            f"Error obtaining a default interface on {str(all_hosts)} from {net_path}")

    # Populate a dictionary of active interfaces with a NodSet of hosts on which it was found
    active_interfaces = {}
    for data in result.output:
        for line in data.stdout:
            try:
                interface = line.split("/")[-2]
                if interface not in active_interfaces:
                    active_interfaces[interface] = data.hosts
                else:
                    active_interfaces[interface].update(data.hosts)
            except IndexError:
                pass

    # From the active interface dictionary find all the interfaces that are common to all hosts
    log.debug("Active network interfaces detected:")
    common_interfaces = []
    for interface, node_set in active_interfaces.items():
        log.debug("  - %-8s on %s (Common=%s)", interface, node_set, node_set == all_hosts)
        if node_set == all_hosts:
            common_interfaces.append(interface)

    # Find the speed of each common active interface in order to be able to choose the fastest
    interface_speeds = {}
    for interface in common_interfaces:
        command = " ".join(["cat", os.path.join(net_path, interface, "speed")])
        result = run_remote(log, all_hosts, command)
        # Verify each host has the same interface speed
        if result.passed and result.homogeneous:
            for line in result.output[0].stdout:
                try:
                    interface_speeds[interface] = int(line.strip())
                except IOError as io_error:
                    # KVM/Qemu/libvirt returns an EINVAL
                    if io_error.errno == errno.EINVAL:
                        interface_speeds[interface] = 1000
                except ValueError:
                    # Any line not containing a speed (integer)
                    pass
        elif not result.homogeneous:
            log.error(
                "Non-homogeneous interface speed detected for %s on %s.",
                interface, str(all_hosts))
        else:
            log.error("Error detecting speed of %s on %s", interface, str(all_hosts))

    if interface_speeds:
        log.debug("Active network interface speeds on %s:", all_hosts)

    for interface, speed in interface_speeds.items():
        log.debug("  - %-8s (speed: %6s)", interface, speed)
        # Only include the first active interface for each speed - first is
        # determined by an alphabetic sort: ib0 will be checked before ib1
        if speed is not None and speed not in available_interfaces:
            available_interfaces[speed] = interface

    log.debug("Available interfaces on %s: %s", all_hosts, available_interfaces)
    return available_interfaces


def set_provider_environment(log, interface, args):
    """Set up the provider environment variables.

    Use the existing CRT_PHY_ADDR_STR setting if already defined, otherwise
    select the appropriate provider based upon the interface driver.

    Args:
        log (logger): logger for the messages produced by this method
        interface (str): the current interface being used.
        args (argparse.Namespace): command line arguments for this program

    Raises:
        LaunchException: if there is a problem finding a provider for the interface

    """
    log.debug("-" * 80)
    # Use the detected provider if one is not set
    if args.provider:
        provider = args.provider
    else:
        provider = os.environ.get("CRT_PHY_ADDR_STR")
    if provider is None:
        log.debug("Detecting provider for %s - CRT_PHY_ADDR_STR not set", interface)

        # Check for a Omni-Path interface
        log.debug("Checking for Omni-Path devices")
        command = "sudo opainfo"
        result = run_remote(log, args.test_servers, command)
        if result.passed:
            # Omni-Path adapter not found; remove verbs as it will not work with OPA devices.
            log.debug("  Excluding verbs provider for Omni-Path adapters")
            PROVIDER_KEYS.pop("verbs")

        # Detect all supported providers
        command = f"fi_info -d {interface} -l | grep -v 'version:'"
        result = run_remote(log, args.test_servers, command)
        if result.passed:
            # Find all supported providers
            keys_found = defaultdict(NodeSet)
            for data in result.output:
                for line in data.stdout:
                    provider_name = line.replace(":", "")
                    if provider_name in PROVIDER_KEYS:
                        keys_found[provider_name].update(data.hosts)

            # Only use providers available on all the server hosts
            if keys_found:
                log.debug("Detected supported providers:")
            provider_name_keys = list(keys_found)
            for provider_name in provider_name_keys:
                log.debug("  %4s: %s", provider_name, str(keys_found[provider_name]))
                if keys_found[provider_name] != args.test_servers:
                    keys_found.pop(provider_name)

            # Select the preferred found provider based upon PROVIDER_KEYS order
            log.debug("Supported providers detected: %s", list(keys_found))
            for key in PROVIDER_KEYS:
                if key in keys_found:
                    provider = PROVIDER_KEYS[key]
                    break

        # Report an error if a provider cannot be found
        if not provider:
            raise LaunchException(
                f"Error obtaining a supported provider for {interface} from: {list(PROVIDER_KEYS)}")

        log.debug("  Found %s provider for %s", provider, interface)

    # Update env definitions
    os.environ["CRT_PHY_ADDR_STR"] = provider
    log.info("Testing with provider:    %s", provider)
    log.debug("Testing with CRT_PHY_ADDR_STR=%s", os.environ["CRT_PHY_ADDR_STR"])


def set_python_environment(log):
    """Set up the test python environment.

    Args:
        log (logger): logger for the messages produced by this method
    """
    log.debug("-" * 80)
    required_python_paths = [
        os.path.abspath("util/apricot"),
        os.path.abspath("util"),
        os.path.abspath("cart"),
    ]

    # Include the cart directory paths when running from sources
    for cart_dir in os.listdir(os.path.abspath("cart")):
        cart_path = os.path.join(os.path.abspath("cart"), cart_dir)
        if os.path.isdir(cart_path):
            required_python_paths.append(cart_path)

    required_python_paths.extend(site.getsitepackages())

    # Check the PYTHONPATH env definition
    python_path = os.environ.get("PYTHONPATH")
    if python_path is None or python_path == "":
        # Use the required paths to define the PYTHONPATH env if it is not set
        os.environ["PYTHONPATH"] = ":".join(required_python_paths)
    else:
        # Append any missing required paths to the existing PYTHONPATH env
        defined_python_paths = [
            os.path.abspath(os.path.expanduser(path))
            for path in python_path.split(":")]
        for required_path in required_python_paths:
            if required_path not in defined_python_paths:
                python_path += ":" + required_path
        os.environ["PYTHONPATH"] = python_path
    log.debug("Testing with PYTHONPATH=%s", os.environ["PYTHONPATH"])


def get_device_replacement(log, args):
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
        log (logger): logger for the messages produced by this method
        args (argparse.Namespace): command line arguments for this program

    Raises:
        LaunchException: if there is a problem finding a device replacement

    Returns:
        str: a comma-separated list of nvme device pci addresses available on all of the specified
             test servers

    """
    log.debug("-" * 80)
    log.debug("Detecting devices that match: %s", args.nvme)
    devices = []
    device_types = []

    # Separate any optional filter from the key
    dev_filter = None
    nvme_args = args.nvme.split(":")
    if len(nvme_args) > 1:
        dev_filter = nvme_args[1]

    # First check for any VMD disks, if requested
    if nvme_args[0] in ["auto", "auto_vmd"]:
        vmd_devices = auto_detect_devices(log, args.test_servers, "NVMe", "5", dev_filter)
        if vmd_devices:
            # Find the VMD controller for the matching VMD disks
            vmd_controllers = auto_detect_devices(log, args.test_servers, "VMD", "4", None)
            devices.extend(get_vmd_address_backed_nvme(
                log, args.test_servers, vmd_devices, vmd_controllers))
        elif not dev_filter:
            # Use any VMD controller if no VMD disks found w/o a filter
            devices = auto_detect_devices(log, args.test_servers, "VMD", "4", None)
        if devices:
            device_types.append("VMD")

    # Second check for any non-VMD NVMe disks, if requested
    if nvme_args[0] in ["auto", "auto_nvme"]:
        dev_list = auto_detect_devices(log, args.test_servers, "NVMe", "4", dev_filter)
        if dev_list:
            devices.extend(dev_list)
            device_types.append("NVMe")

    # If no VMD or NVMe devices were found exit
    if not devices:
        raise LaunchException(
            f"Error: Unable to auto-detect devices for the '--nvme {args.nvme}' argument")

    log.debug(
        "Auto-detected %s devices on %s: %s", " & ".join(device_types), args.test_servers, devices)
    log.info("Testing with %s devices: %s", " & ".join(device_types), devices)
    return ",".join(devices)


def auto_detect_devices(log, hosts, device_type, length, device_filter=None):
    """Get a list of NVMe/VMD devices found on each specified host.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to find the NVMe/VMD devices
        device_type (str): device type to find, e.g. 'NVMe' or 'VMD'
        length (str): number of digits to match in the first PCI domain number
        device_filter (str, optional): optional filter to apply to device searching. Defaults to
            None.

    Raises:
        LaunchException: if there is a problem finding a devices

    Returns:
        list: A list of detected devices - empty if none found

    """
    found_devices = {}

    # Find the devices on each host
    if device_type == "VMD":
        # Exclude the controller revision as this causes heterogeneous clush output
        command_list = [
            "/sbin/lspci -D",
            f"grep -E '^[0-9a-f]{{{length}}}:[0-9a-f]{{2}}:[0-9a-f]{{2}}.[0-9a-f] '",
            "grep -E 'Volume Management Device NVMe RAID Controller'",
            r"sed -E 's/\(rev\s+([a-f0-9])+\)//I'"]
    elif device_type == "NVMe":
        command_list = [
            "/sbin/lspci -D",
            f"grep -E '^[0-9a-f]{{{length}}}:[0-9a-f]{{2}}:[0-9a-f]{{2}}.[0-9a-f] '",
            "grep -E 'Non-Volatile memory controller:'"]
        if device_filter and device_filter.startswith("-"):
            command_list.append(f"grep -v '{device_filter[1:]}'")
        elif device_filter:
            command_list.append(f"grep '{device_filter}'")
    else:
        raise LaunchException(
            f"ERROR: Invalid 'device_type' for NVMe/VMD auto-detection: {device_type}")
    command = " | ".join(command_list) + " || :"

    # Find all the VMD PCI addresses common to all hosts
    result = run_remote(log, hosts, command)
    if result.passed:
        for data in result.output:
            for line in data.stdout:
                if line not in found_devices:
                    found_devices[line] = NodeSet()
                found_devices[line].update(data.hosts)

        # Remove any non-homogeneous devices
        for key in list(found_devices):
            if found_devices[key] != hosts:
                log.debug("  device '%s' not found on all hosts: %s", key, found_devices[key])
                found_devices.pop(key)

    if not found_devices:
        raise LaunchException("Error: Non-homogeneous {device_type} PCI addresses.")

    # Get the devices from the successful, homogeneous command output
    return find_pci_address("\n".join(found_devices))


def get_vmd_address_backed_nvme(log, hosts, vmd_disks, vmd_controllers):
    """Find valid VMD address which has backing NVMe.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to find the VMD addresses
        vmd_disks (list): list of PCI domain numbers for each VMD controlled disk
        vmd_controllers (list): list of PCI domain numbers for each VMD controller

    Raises:
        LaunchException: if there is a problem finding a devices

    Returns:
        list: a list of the VMD controller PCI domain numbers which are connected to the VMD disks

    """
    disk_controllers = {}
    command_list = ["ls -l /sys/block/", "grep nvme"]
    if vmd_disks:
        command_list.append(f"grep -E '({'|'.join(vmd_disks)})'")
    command_list.extend(["cut -d'>' -f2", "cut -d'/' -f4"])
    command = " | ".join(command_list) + " || :"
    result = run_remote(log, hosts, command)

    # Verify the command was successful on each server host
    if not result.passed:
        raise LaunchException(f"Error issuing command '{command}'")

    # Collect a list of NVMe devices behind the same VMD addresses on each host.
    log.debug("Checking for %s in command output", vmd_controllers)
    if result.passed:
        for data in result.output:
            for device in vmd_controllers:
                if device in data.stdout:
                    if device not in disk_controllers:
                        disk_controllers[device] = NodeSet()
                    disk_controllers[device].update(data.hosts)

        # Remove any non-homogeneous devices
        for key in list(disk_controllers):
            if disk_controllers[key] != hosts:
                log.debug("  device '%s' not found on all hosts: %s", key, disk_controllers[key])
                disk_controllers.pop(key)

    # Verify each server host has the same NVMe devices behind the same VMD addresses.
    if not disk_controllers:
        raise LaunchException("Error: Non-homogeneous NVMe device behind VMD addresses.")

    return disk_controllers


def find_pci_address(value):
    """Find PCI addresses in the specified string.

    Args:
        value (str): string to search for PCI addresses

    Returns:
        list: a list of all the PCI addresses found in the string

    """
    digit = "0-9a-fA-F"
    pattern = rf"[{digit}]{{4,5}}:[{digit}]{{2}}:[{digit}]{{2}}\.[{digit}]"
    return re.findall(pattern, str(value))


def find_values(obj, keys, key=None, val_type=str):
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


def setup_test_files(log, test_list, args, yaml_dir):
    """Set up the test yaml files with any placeholder replacements.

    Args:
        log (logger): logger for the messages produced by this method
        test_list (list): list of test scripts to run
        args (argparse.Namespace): command line arguments for this program
        yaml_dir (str): directory in which to write the modified yaml files
    """
    # Replace any placeholders in the extra yaml file, if provided
    if args.extra_yaml:
        args.extra_yaml = [
            replace_yaml_file(log, extra, args, yaml_dir) for extra in args.extra_yaml]

    for test in test_list:
        test.yaml_file = replace_yaml_file(log, test.yaml_file, args, yaml_dir)

        # Display the modified yaml file variants with debug
        command = ["avocado", "variants", "--mux-yaml", test.yaml_file]
        if args.extra_yaml:
            command.extend(args.extra_yaml)
        command.extend(["--summary", "3"])
        run_local(log, command, check=False)

        # Collect the host information from the updated test yaml
        test.set_host_info(log, args.include_localhost)

    # Log the test information
    log.debug("-" * 80)
    log.debug("Test information:")
    log.debug("%3s  %-40s  %-50s  %-20s  %-20s", "UID", "Test", "Yaml File", "Servers", "Clients")
    log.debug("%3s  %-40s  %-50s  %-20s  %-20s", "-" * 3, "-" * 40, "-" * 50, "-" * 20, "-" * 20)
    for test in test_list:
        log.debug(
            "%3s  %-40s  %-50s  %-20s  %-20s",
            test.uid_str, test.test_file, test.yaml_file, test.hosts.servers, test.hosts.clients)


def replace_yaml_file(log, yaml_file, args, yaml_dir):
    # pylint: disable=too-many-nested-blocks,too-many-branches
    """Create a temporary test yaml file with any requested values replaced.

    Optionally replace the following test yaml file values if specified by the
    user via the command line arguments:

        test_servers:   Use the list specified by the --test_servers (-ts)
                        argument to replace any host name placeholders listed
                        under "test_servers:"

        test_clients    Use the list specified by the --test_clients (-tc)
                        argument (or any remaining names in the --test_servers
                        list argument, if --test_clients is not specified) to
                        replace any host name placeholders listed under
                        "test_clients:".

        bdev_list       Use the list specified by the --nvme (-n) argument to
                        replace the string specified by the "bdev_list:" yaml
                        parameter.  If multiple "bdev_list:" entries exist in
                        the yaml file, evenly divide the list when making the
                        replacements.

    Any replacements are made in a copy of the original test yaml file.  If no
    replacements are specified return the original test yaml file.

    Args:
        log (logger): logger for the messages produced by this method
        yaml_file (str): test yaml file
        args (argparse.Namespace): command line arguments for this program
        yaml_dir (str): directory in which to write the modified yaml files

    Returns:
        str: the test yaml file; None if the yaml file contains placeholders
            w/o replacements

    """
    log.debug("-" * 80)
    replacements = {}

    if args.test_servers or args.nvme or args.timeout_multiplier:
        # Find the test yaml keys and values that match the replaceable fields
        yaml_data = get_yaml_data(log, yaml_file)
        log.debug("Detected yaml data: %s", yaml_data)
        yaml_keys = list(YAML_KEYS.keys())
        yaml_find = find_values(yaml_data, yaml_keys, val_type=(list, int, dict, str))

        # Generate a list of values that can be used as replacements
        user_values = OrderedDict()
        for key, value in list(YAML_KEYS.items()):
            args_value = getattr(args, value)
            if isinstance(args_value, NodeSet):
                user_values[key] = list(args_value)
            elif isinstance(args_value, str):
                user_values[key] = args_value.split(",")
            elif args_value:
                user_values[key] = [args_value]
            else:
                user_values[key] = None

        # Assign replacement values for the test yaml entries to be replaced
        log.debug("Detecting replacements for %s in %s", yaml_keys, yaml_file)
        log.debug("  Found values: %s", yaml_find)
        log.debug("  User values:  %s", dict(user_values))

        node_mapping = {}
        for key, user_value in user_values.items():
            # If the user did not provide a specific list of replacement
            # test_clients values, use the remaining test_servers values to
            # replace test_clients placeholder values
            if key == "test_clients" and not user_value:
                user_value = user_values["test_servers"]

            # Replace test yaml keys that were:
            #   - found in the test yaml
            #   - have a user-specified replacement
            if key in yaml_find and user_value:
                if key.startswith("test_"):
                    # The entire server/client test yaml list entry is replaced
                    # by a new test yaml list entry, e.g.
                    #   '  test_servers: server-[1-2]' --> '  test_servers: wolf-[10-11]'
                    #   '  test_servers: 4'            --> '  test_servers: wolf-[10-13]'
                    if not isinstance(yaml_find[key], list):
                        yaml_find[key] = [yaml_find[key]]

                    for yaml_find_item in yaml_find[key]:
                        replacement = NodeSet()
                        try:
                            # Replace integer placeholders with the number of nodes from the user
                            # provided list equal to the quantity requested by the test yaml
                            quantity = int(yaml_find_item)
                            if args.override and args.test_clients:
                                # When individual lists of server and client nodes are provided with
                                # the override flag set use the full list of nodes specified by the
                                # test_server/test_client arguments
                                quantity = len(user_value)
                            elif args.override:
                                log.warn(
                                    "Warning: In order to override the node quantity a "
                                    "'--test_clients' argument must be specified: %s: %s",
                                    key, yaml_find_item)
                            for _ in range(quantity):
                                try:
                                    replacement.add(user_value.pop(0))
                                except IndexError:
                                    # Not enough nodes provided for the replacement
                                    if not args.override:
                                        replacement = None
                                    break

                        except ValueError:
                            try:
                                # Replace clush-style placeholders with nodes from the user provided
                                # list using a mapping so that values used more than once yield the
                                # same replacement
                                for node in NodeSet(yaml_find_item):
                                    if node not in node_mapping:
                                        try:
                                            node_mapping[node] = user_value.pop(0)
                                        except IndexError:
                                            # Not enough nodes provided for the replacement
                                            if not args.override:
                                                replacement = None
                                            break
                                        log.debug(
                                            "  - %s replacement node mapping: %s -> %s",
                                            key, node, node_mapping[node])
                                    replacement.add(node_mapping[node])

                            except TypeError:
                                # Unsupported format
                                replacement = None

                        hosts_key = r":\s+".join([key, str(yaml_find_item)])
                        hosts_key = hosts_key.replace("[", r"\[")
                        hosts_key = hosts_key.replace("]", r"\]")
                        if replacement:
                            replacements[hosts_key] = ": ".join([key, str(replacement)])
                        else:
                            replacements[hosts_key] = None

                elif key == "bdev_list":
                    # Individual bdev_list NVMe PCI addresses in the test yaml
                    # file are replaced with the new NVMe PCI addresses in the
                    # order they are found, e.g.
                    #   0000:81:00.0 --> 0000:12:00.0
                    for yaml_find_item in yaml_find[key]:
                        bdev_key = f"\"{yaml_find_item}\""
                        if bdev_key in replacements:
                            continue
                        try:
                            replacements[bdev_key] = f"\"{user_value.pop(0)}\""
                        except IndexError:
                            replacements[bdev_key] = None

                else:
                    # Timeouts - replace the entire timeout entry (key + value)
                    # with the same key with its original value multiplied by the
                    # user-specified value, e.g.
                    #   timeout: 60 -> timeout: 600
                    if isinstance(yaml_find[key], int):
                        timeout_key = r":\s+".join([key, str(yaml_find[key])])
                        timeout_new = max(1, round(yaml_find[key] * user_value[0]))
                        replacements[timeout_key] = ": ".join([key, str(timeout_new)])
                        log.debug(
                            "  - Timeout adjustment (x %s): %s -> %s",
                            user_value, timeout_key, replacements[timeout_key])
                    elif isinstance(yaml_find[key], dict):
                        for timeout_test, timeout_val in list(yaml_find[key].items()):
                            timeout_key = r":\s+".join([timeout_test, str(timeout_val)])
                            timeout_new = max(1, round(timeout_val * user_value[0]))
                            replacements[timeout_key] = ": ".join([timeout_test, str(timeout_new)])
                            log.debug(
                                "  - Timeout adjustment (x %s): %s -> %s",
                                user_value, timeout_key, replacements[timeout_key])

        # Display the replacement values
        for value, replacement in list(replacements.items()):
            log.debug("  - Replacement: %s -> %s", value, replacement)

    if replacements:
        # Read in the contents of the yaml file to retain the !mux entries
        log.debug("Reading %s", yaml_file)
        with open(yaml_file, encoding="utf-8") as yaml_buffer:
            yaml_data = yaml_buffer.read()

        # Apply the placeholder replacements
        missing_replacements = []
        log.debug("Modifying contents: %s", yaml_file)
        for key in sorted(replacements):
            value = replacements[key]
            if value:
                # Replace the host entries with their mapped values
                log.debug("  - Replacing: %s --> %s", key, value)
                yaml_data = re.sub(key, value, yaml_data)
            else:
                # Keep track of any placeholders without a replacement value
                log.debug("  - Missing:   %s", key)
                missing_replacements.append(key)
        if missing_replacements:
            # Report an error for all of the placeholders w/o a replacement
            log.error(
                "Error: Placeholders missing replacements in %s:\n  %s",
                yaml_file, ", ".join(missing_replacements))
            sys.exit(1)

        # Write the modified yaml file into a temporary file.  Use the path to
        # ensure unique yaml files for tests with the same filename.
        orig_yaml_file = yaml_file
        yaml_name = get_test_category(yaml_file)
        yaml_file = os.path.join(yaml_dir, f"{yaml_name}.yaml")
        log.debug("Creating copy: %s", yaml_file)
        with open(yaml_file, "w", encoding="utf-8") as yaml_buffer:
            yaml_buffer.write(yaml_data)

        # Optionally display a diff of the yaml file
        if args.verbose > 0:
            command = ["diff", "-y", orig_yaml_file, yaml_file]
            run_local(log, command, check=False)

    # Return the untouched or modified yaml file
    return yaml_file


def generate_certs(log):
    """Generate the certificates for the test.

    Args:
        log (logger): logger for the messages produced by this method

    Raises:
        LaunchException: if there is an error generating the certificates

    """
    log.debug("-" * 80)
    log.debug("Generating certificates")
    daos_test_log_dir = os.environ["DAOS_TEST_LOG_DIR"]
    certs_dir = os.path.join(daos_test_log_dir, "daosCA")
    certgen_dir = os.path.abspath(os.path.join("..", "..", "..", "..", "lib64", "daos", "certgen"))
    run_local(log, ["/usr/bin/rm", "-rf", certs_dir])
    run_local(log, [os.path.join(certgen_dir, "gen_certificates.sh"), daos_test_log_dir])


def setup_test_directory(log, test, mode="all"):
    """Set up the common test directory on all hosts.

    Ensure the common test directory exists on each possible test node.

    Args:
        log (logger): logger for the messages produced by this method
        test (TestInfo): the test information
        mode (str, optional): setup mode. Defaults to "all".
            "rm"    = remove the directory
            "mkdir" = create the directory
            "chmod" = change the permissions of the directory (a+rw)
            "list"  = list the contents of the directory
            "all"  = execute all of the mode options

    Raises:
        LaunchException: if there is an error setting up the test directory

    """
    log.debug("-" * 80)
    test_dir = os.environ["DAOS_TEST_LOG_DIR"]
    log.debug("Setting up '%s' on %s:", test_dir, test.hosts.all)
    if mode in ["all", "rm"]:
        run_remote(log, test.hosts.all, f"sudo rm -fr {test_dir}")
    if mode in ["all", "mkdir"]:
        run_remote(log, test.hosts.all, f"mkdir -p {test_dir}")
    if mode in ["all", "chmod"]:
        run_remote(log, test.hosts.all, f"chmod a+wr {test_dir}")
    if mode in ["all", "list"]:
        run_remote(log, test.hosts.all, f"ls -al {test_dir}")


def get_yaml_data(log, yaml_file):
    """Get the contents of a yaml file as a dictionary.

    Removes any mux tags and ignores any other tags present.

    Args:
        log (logger): logger for the messages produced by this method
        yaml_file (str): yaml file to read

    Raises:
        Exception: if an error is encountered reading the yaml file

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
        with open(yaml_file, "r", encoding="utf-8") as open_file:
            try:
                yaml_data = yaml.load(open_file.read(), Loader=DaosLoader)
            except yaml.YAMLError as error:
                log.error("Error reading %s: %s", yaml_file, str(error))
                sys.exit(1)
    return yaml_data


def get_test_category(test_file):
    """Get a category for the specified test using its path and name.

    Args:
        test_file (str): the test python file

    Returns:
        str: concatenation of the test path and base filename joined by dashes

    """
    file_parts = os.path.split(test_file)
    return "-".join([os.path.splitext(os.path.basename(part))[0] for part in file_parts])


def stop_daos_agent_services(log, test):
    """Stop any daos_agent.service running on the hosts running servers.

    Args:
        log (logger): logger for the messages produced by this method
        test (TestInfo): the test information

    Returns:
        int: status code: 0 = success, 512 = failure

    """
    service = "daos_agent.service"
    hosts = test.hosts.clients_with_localhost
    log.debug("-" * 80)
    log.debug("Verifying %s after running '%s'", service, test)
    return stop_service(log, hosts, service)


def stop_daos_server_service(log, test):
    """Stop any daos_server.service running on the hosts running servers.

    Args:
        log (logger): logger for the messages produced by this method
        test (TestInfo): the test information

    Returns:
        int: status code: 0 = success, 512 = failure

    """
    service = "daos_server.service"
    hosts = test.hosts.servers
    log.debug("-" * 80)
    log.debug("Verifying %s after running '%s'", service, test)
    return stop_service(log, hosts, service)


def stop_service(log, hosts, service):
    """Stop any daos_server.service running on the hosts running servers.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): list of hosts on which to stop the service.
        service (str): name of the service

    Returns:
        int: status code: 0 = success, 512 = failure

    """
    result = {"status": 0}
    if hosts:
        status_keys = ["reset-failed", "stop", "disable"]
        mapping = {"stop": "active", "disable": "enabled", "reset-failed": "failed"}
        check_hosts = NodeSet(hosts)
        loop = 1
        # Reduce 'max_loops' to 2 once https://jira.hpdd.intel.com/browse/DAOS-7809
        # has been resolved
        max_loops = 3
        while check_hosts:
            # Check the status of the service on each host
            result = get_service_status(log, check_hosts, service)
            check_hosts = NodeSet()
            for key in status_keys:
                if result[key]:
                    if loop == max_loops:
                        # Exit the while loop if the service is still running
                        log.error(" - Error %s still %s on %s", service, mapping[key], result[key])
                        result["status"] = 512
                    else:
                        # Issue the appropriate systemctl command to remedy the
                        # detected state, e.g. 'stop' for 'active'.
                        command = ["sudo", "systemctl", key, service]
                        run_remote(log, result[key], " ".join(command))

                        # Run the status check again on this group of hosts
                        check_hosts.add(result[key])
            loop += 1
    else:
        log.debug("  Skipping stopping %s service - no hosts", service)

    return result["status"]


def get_service_status(log, hosts, service):
    """Get the status of the daos_server.service.

    Args:
        log (logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to get the service state
        service (str): name of the service

    Returns:
        dict: a dictionary with the following keys:
            - "status":       status code: 0 = success, 512 = failure
            - "stop":         NodeSet where to stop the daos_server.service
            - "disable":      NodeSet where to disable the daos_server.service
            - "reset-failed": NodeSet where to reset the daos_server.service

    """
    status = {
        "status": 0,
        "stop": NodeSet(),
        "disable": NodeSet(),
        "reset-failed": NodeSet()}
    status_states = {
        "stop": ["active", "activating", "deactivating"],
        "disable": ["active", "activating", "deactivating"],
        "reset-failed": ["failed"]}
    command = ["systemctl", "is-active", service]
    result = run_remote(log, hosts, " ".join(command))
    for data in result.output:
        if data.timeout:
            status["status"] = 512
            status["stop"].add(data.hosts)
            status["disable"].add(data.hosts)
            status["reset-failed"].add(data.hosts)
            log.debug("  %s: TIMEOUT", data.hosts)
            break
        # log.debug(" %s: %s", data.hosts, "\n".join(data.stdout))
        for key, state_list in status_states.items():
            for line in data.stdout:
                if line in state_list:
                    status[key].add(data.hosts)
                    break
    return status


def reset_server_storage(log, test):
    """Reset the server storage for the hosts that ran servers in the test.

    This is a workaround to enable binding devices back to nvme or vfio-pci after they are unbound
    from vfio-pci to nvme.  This should resolve the "NVMe not found" error seen when attempting to
    start daos engines in the test.

    Args:
        log (logger): logger for the messages produced by this method
        test (TestInfo): the test information

    Returns:
        int: status code: 0 = success, 512 = failure

    """
    hosts = test.hosts.servers
    log.debug("-" * 80)
    log.debug("Resetting server storage after running %s", test)
    if hosts:
        commands = [
            "if lspci | grep -i nvme",
            "then daos_server storage prepare -n --reset && "
            "sudo rmmod vfio_pci && sudo modprobe vfio_pci",
            "fi"]
        log.info("Resetting server storage on %s after running '%s'", hosts, test)
        result = run_remote(log, hosts, f"bash -c '{';'.join(commands)}'", timeout=600)
        if not result.passed:
            log.debug("Ignoring any errors from these workaround commands")
    else:
        log.debug("  Skipping resetting server storage - no server hosts")
    return 0


class AvocadoInfo():
    """Information about this version of avocado."""

    def __init__(self):
        """Initialize an AvocadoInfo object.

        Raises:
            LaunchException: _description_
        """
        try:
            # pylint: disable=import-outside-toplevel
            from avocado.core.version import MAJOR, MINOR
            self.major = int(MAJOR)
            self.minor = int(MINOR)

        except ModuleNotFoundError:
            # Once lightweight runs are using python3-avocado, this can be removed
            log = logging.getLogger(__name__)
            result = run_local(log, ["avocado", "-v"], check=True)
            try:
                version = re.findall(r"(\d+)\.(\d+)", result.stdout)[0]
                self.major = int(version[0])
                self.minor = int(version[1])
            except IndexError as error:
                raise LaunchException("Error extracting avocado version from command") from error

    def __str__(self):
        """Get the avocado version as a string.

        Returns:
            str: the avocado version

        """
        return f"Avocado {str(self.major)}.{str(self.minor)}"

    def get_setting(self, section, key, default=None):
        """Get the value for the specified avocado setting.

        Args:
            section (str): avocado setting section name
            key (str): avocado setting key name
            default (object): default value to use if setting is undefined

        Raises:
            LaunchException: if there is an error getting the setting from the avocado command

        Returns:
            object: value for the avocado setting or None if not defined

        """
        try:
            # pylint: disable=import-outside-toplevel
            from avocado.core.settings import settings
            try:
                # Newer versions of avocado use this approach
                config = settings.as_dict()
                return config.get(".".join([section, key]))

            except AttributeError:
                # Older version of avocado, like 69LTS, use a different method
                # pylint: disable=no-member
                try:
                    return settings.get_value(section, key)
                except settings.SettingsError:
                    # Setting not found
                    pass

            except KeyError:
                # Setting not found
                pass

        except ModuleNotFoundError:
            # Once lightweight runs are using python3-avocado, this can be removed
            log = logging.getLogger(__name__)
            result = run_local(log, ["avocado", "config"], check=True)
            try:
                return re.findall(rf"{section}\.{key}\s+(.*)", result.stdout)[0]
            except IndexError:
                # Setting not found
                pass

        return default

    def get_logs_dir(self):
        """Get the avocado directory in which the test results are stored.

        Returns:
            str: the directory used by avocado to log test results

        """
        default_base_dir = os.path.expanduser(os.path.join("~", "avocado", "job-results"))
        return self.get_setting("datadir.paths", "logs_dir", default_base_dir)

    def get_directory(self, directory, create=True):
        """Get the avocado test directory for the test.

        Args:
            directory (str): name of the sub directory to add to the logs directory
            create (bool, optional): whether or not to create the directory if it doesn't exist.
                Defaults to True.

        Returns:
            str: the directory used by avocado to log test results

        """
        logs_dir = self.get_logs_dir()
        test_dir = os.path.join(logs_dir, directory)
        if create:
            os.makedirs(test_dir, exist_ok=True)
        return test_dir

    def get_list_command(self):
        """Get the avocado list command for this version of avocado.

        Returns:
            list: avocado list command parts

        """
        if self.major >= 83:
            return ["avocado", "list"]
        if self.major >= 82:
            return ["avocado", "--paginator=off", "list"]
        return ["avocado", "list", "--paginator=off"]

    def get_run_command(self, test, tag_filters, sparse, failfast, extra_yaml):
        """Get the avocado run command for this version of avocado.

        Args:
            test (TestInfo): the test information
            tag_filters (list): optional '--filter-by-tags' arguments
            sparse (bool): whether or not to provide sparse output of the test execution
            failfast (bool): _description_
            extra_yaml (list): additional yaml files to include on the command line

        Returns:
            list: avocado run command

        """
        command = ["avocado"]
        if not sparse and self.major >= 82:
            command.append("--show=test")
        command.append("run")
        if self.major >= 82:
            command.append("--ignore-missing-references")
        else:
            command.extend(["--ignore-missing-references", "on"])
        if self.major >= 83:
            command.append("--disable-tap-job-result")
        else:
            command.extend(["--html-job-result", "on"])
            command.extend(["--tap-job-result", "off"])
        if not sparse and self.major < 82:
            command.append("--show-job-log")
        if tag_filters:
            command.extend(tag_filters)
        if failfast:
            command.extend(["--failfast", "on"])
        command.extend(["--mux-yaml", test.yaml_file])
        if extra_yaml:
            command.extend(extra_yaml)
        command.extend(["--", str(test)])
        return command


class RemoteCommandResult():
    """Stores the command result from a Task object."""

    class ResultData():
        # pylint: disable=too-few-public-methods
        """Command result data for the set of hosts."""

        def __init__(self, command, returncode, hosts, stdout, timeout):
            """Initialize a ResultData object.

            Args:
                command (str): the executed command
                returncode (int): the return code of the executed command
                hosts (NodeSet): the host(s) on which the executed command yielded this result
                stdout (list): the result of the executed command split by newlines
                timeout (bool): indicator for a command timeout
            """
            self.command = command
            self.returncode = returncode
            self.hosts = hosts
            self.stdout = stdout
            self.timeout = timeout

    def __init__(self, command, task):
        """Create a RemoteCommandResult object.

        Args:
            command (str): command executed
            task (Task): object containing the results from an executed clush command
        """
        self.output = []
        self._process_task(task, command)

    @property
    def homogeneous(self):
        """Did all the hosts produce the same output.

        Returns:
            bool: if all the hosts produced the same output

        """
        return len(self.output) == 1

    @property
    def passed(self):
        """Did the command pass on all the hosts.

        Returns:
            bool: if the command was successful on each host

        """
        non_zero = any(data.returncode != 0 for data in self.output)
        return not non_zero and not self.timeout

    @property
    def timeout(self):
        """Did the command timeout on any hosts.

        Returns:
            bool: True if the command timed out on at least one set of hosts; False otherwise

        """
        return any(data.timeout for data in self.output)

    def _process_task(self, task, command):
        """Populate the output list and determine the passed result for the specified task.

        Args:
            task (Task): a ClusterShell.Task.Task object for the executed command
            command (str): the executed command
        """
        # Get a dictionary of host list values for each unique return code key
        results = dict(task.iter_retcodes())

        # Get a list of any hosts that timed out
        timed_out = [str(hosts) for hosts in task.iter_keys_timeout()]

        # Populate the a list of unique output for each NodeSet
        for code in sorted(results):
            output_data = list(task.iter_buffers(results[code]))
            if not output_data:
                output_data = [["<NONE>", results[code]]]
            for output, output_hosts in output_data:
                stdout = []
                for line in output.splitlines():
                    if isinstance(line, bytes):
                        stdout.append(line.decode("utf-8"))
                    else:
                        stdout.append(line)
                self.output.append(
                    self.ResultData(command, code, NodeSet.fromlist(output_hosts), stdout, False))
        if timed_out:
            self.output.append(
                self.ResultData(command, 124, NodeSet.fromlist(timed_out), None, True))

    def log_output(self, log):
        """Log the command result.

        Args:
            log (logger): logger for the messages produced by this method
        """
        for data in self.output:
            if data.timeout:
                log.debug("  %s (rc=%s): timed out", str(data.hosts), data.returncode)
            elif len(data.stdout) == 1:
                log.debug("  %s (rc=%s): %s", str(data.hosts), data.returncode, data.stdout[0])
            else:
                log.debug("  %s (rc=%s):", str(data.hosts), data.returncode)
                for line in data.stdout:
                    log.debug("    %s", line)


class TestInfo():
    """Defines the python test file and its associated test yaml file."""

    class HostInfo():
        # pylint: disable=too-few-public-methods
        """Defines the hosts being utilized by the test."""

        def __init__(self):
            """Initialize a HostInfo object."""
            self.all = NodeSet()
            self.servers = NodeSet()
            self.clients = NodeSet()

        @property
        def clients_with_localhost(self):
            """Get the test clients including the localhost.

            Returns:
                NodeSet: test clients including the localhost

            """
            return self.clients | NodeSet(get_local_host())

        @property
        def all_remote(self):
            """Get the all the hosts excluding the localhost.

            Returns:
                NodeSet: all the hosts excluding the localhost

            """
            return self.all - NodeSet(get_local_host())

    def __init__(self, test_file, uid):
        """Initialize a TestInfo object.

        Args:
            test_file (str): the test python file
        """
        self.name = {"name": test_file, "uid": uid, "variant": 0}
        self.test_file = test_file
        self.yaml_file = ".".join([os.path.splitext(self.test_file)[0], "yaml"])
        self.directory, self.python_file = self.test_file.split(os.path.sep)[1:]
        self.class_name = f"FTEST_launch.{self.directory}-{os.path.splitext(self.python_file)[0]}"
        self.hosts = self.HostInfo()

    def __str__(self):
        """Get the test file as a string.

        Returns:
            str: the test file

        """
        return self.test_file

    @property
    def name_str(self):
        """Get the test name as a string.

        Returns:
            str: combination of the uid and name

        """
        if self.repeat > 0:
            return f"{self.uid_str}-{self.name['name']}-{self.repeat_str}"
        return f"{self.uid_str}-{self.name['name']}"

    @property
    def uid(self):
        """Get the string representation of the name uid.

        Returns:
            str: the name uid as a string; useful for file/directory naming

        """
        return self.name['uid']

    @property
    def uid_str(self):
        """Get the string representation of the name uid.

        Returns:
            str: the name uid as a string; useful for file/directory naming

        """
        return f"repeat{self.uid:02}"

    @property
    def repeat(self):
        """Get the test name repeat counter.

        Returns:
            int: the test name repeat

        """
        return self.name['variant']

    @repeat.setter
    def repeat(self, value):
        """Set the test name repeat counter.

        Args:
            value (int): the test repeat counter
        """
        self.name['variant'] = value

    @property
    def repeat_str(self):
        """Get the string representation of the repeat counter.

        Returns:
            str: the repeat count as a string; useful for file/directory naming

        """
        return f"repeat{self.repeat:03}"

    def set_host_info(self, log, include_localhost=False):
        """Set the test host information using the test yaml file.

        Args:
            log (logger): logger for the messages produced by this method
            include_localhost (bool, optional): should the local host be included in the list of
                client matches. Defaults to False.
        """
        self.hosts.all.clear()
        self.hosts.all.update(self._get_hosts_from_yaml(log, include_localhost))

        self.hosts.servers.clear()
        self.hosts.servers.update(
            self._get_hosts_from_yaml(log, include_localhost, YAML_KEYS["test_servers"]))

        self.hosts.clients.clear()
        self.hosts.clients.update(
            self._get_hosts_from_yaml(log, include_localhost, YAML_KEYS["test_clients"]))

    def _get_hosts_from_yaml(self, log, include_localhost=False, key_match=None):
        """Extract the list of hosts from the test yaml file.

        This host will be included in the list if no clients are explicitly called
        out in the test's yaml file.

        Args:
            log (logger): logger for the messages produced by this method
            include_localhost (bool, optional): should the local host be included in the list of
                client matches. Defaults to False.
            key_match (str, optional): test yaml key used to filter which hosts to
                find.  Defaults to None which will match all keys.

        Returns:
            NodeSet: hosts specified in the test's yaml file

        """
        log.debug("Extracting hosts from %s that match key '%s'", self.yaml_file, key_match)
        local_host = NodeSet(get_local_host())
        yaml_hosts = NodeSet()
        if include_localhost and key_match != YAML_KEYS["test_servers"]:
            yaml_hosts.add(local_host)
        found_client_key = False
        for key, value in list(self._find_yaml_hosts(log).items()):
            log.debug("  Found %s: %s", key, value)
            if key_match is None or key == key_match:
                log.debug("    Adding %s", value)
                if isinstance(value, list):
                    yaml_hosts.add(NodeSet.fromlist(value))
                else:
                    yaml_hosts.add(NodeSet(value))
            if key in YAML_KEYS["test_clients"]:
                found_client_key = True

        # Include this host as a client if no clients are specified
        if not found_client_key and key_match != YAML_KEYS["test_servers"]:
            log.debug("    Adding the localhost: %s", local_host)
            yaml_hosts.add(local_host)

        return yaml_hosts

    def _find_yaml_hosts(self, log):
        """Find the all the host values in the specified yaml file.

        Args:
            log (logger): logger for the messages produced by this method

        Returns:
            dict: a dictionary of each host key and its host values

        """
        return find_values(
            get_yaml_data(log, self.yaml_file),
            [YAML_KEYS["test_servers"], YAML_KEYS["test_clients"]])

    def get_log_file(self, logs_dir, repeat, total):
        """Get the test log file name.

        Args:
            logs_dir (str): base directory in which to place the log file
            repeat (int): current test repetition
            total (int): total number of test repetitions

        Returns:
            str: a test log file name composed of the test class, name, and optional repeat count

        """
        name = os.path.splitext(self.python_file)[0]
        log_file = f"{self.uid_str}-{self.directory}-{name}-launch.log"
        if total > 1:
            self.repeat = repeat
            os.makedirs(os.path.join(logs_dir, self.repeat_str), exist_ok=True)
            return os.path.join(logs_dir, self.repeat_str, log_file)
        return os.path.join(logs_dir, log_file)


class Launch():
    """Class to launch avocado tests."""

    def __init__(self, name, repeat, mode):
        """Initialize a Launch object.

        Args:
            name (str): launch job name
            repeat (int): number of times to repeat executing all of the tests
            mode (str): execution mode, e.g. "normal", "manual", or "ci"
        """
        self.avocado = AvocadoInfo()
        self.name = name
        self.repeat = repeat
        self.mode = mode
        self.logdir = self.avocado.get_directory(os.path.join("launch", self.name.lower()), False)
        self.logfile = os.path.join(self.logdir, "job.log")
        self.tests = []
        self.tag_filters = []

        # Setup a logger
        renamed_log_dir = self._create_log_dir()
        console_format = "%(message)s"
        console = logging.StreamHandler()
        console.setFormatter(logging.Formatter(console_format))
        console.setLevel(logging.INFO)
        logging.basicConfig(
            format=console_format, datefmt=r"%Y/%m/%d %I:%M:%S", level=logging.DEBUG,
            handlers=[console])
        self.log = logging.getLogger(__name__)
        self.log.addHandler(self._get_file_handler(self.logfile))
        self._start_logging(renamed_log_dir)

        # Results tracking settings
        self.job_results_dir = self.avocado.get_logs_dir()
        self.local_host = NodeSet(get_local_host())
        max_chars = self.avocado.get_setting("job.run.result.xunit", "max_test_log_chars")
        self.job = Job(
            self.logfile, xml_enabled="on", html_enabled="on", log_dir=self.logdir,
            max_chars=max_chars)
        self.result = Results(self.logdir)

    @staticmethod
    def _get_file_handler(log_file):
        """Get a logging file handler.

        Args:
            log_file (str): file in which to log debug messages

        Returns
            str: a logging.FileHandler setup to log debug messages to the log file

        """
        log_format = logging.Formatter("%(asctime)s %(levelname)-5s %(funcName)27s: %(message)s")
        log_handler = logging.FileHandler(log_file, encoding='utf-8')
        log_handler.setLevel(logging.DEBUG)
        log_handler.setFormatter(log_format)
        return log_handler

    def _create_log_dir(self):
        """Create the log directory and rename it if it already exists.

        Returns:
            str: name of the old log directory if renamed, otherwise None

        """
        # When running manually save the previous log if one exists
        old_launch_log_dir = None
        if os.path.exists(self.logdir):
            old_launch_log_dir = "_".join([self.logdir, "old"])
            if os.path.exists(old_launch_log_dir):
                for file in os.listdir(old_launch_log_dir):
                    rm_file = os.path.join(old_launch_log_dir, file)
                    if os.path.isdir(rm_file):
                        for file2 in os.listdir(rm_file):
                            os.remove(os.path.join(rm_file, file2))
                        os.rmdir(rm_file)
                    else:
                        os.remove(rm_file)
                os.rmdir(old_launch_log_dir)
            os.rename(self.logdir, old_launch_log_dir)
        os.makedirs(self.logdir)

        return old_launch_log_dir

    def _start_logging(self, renamed_log_dir=None):
        """Log the information to the start of the log file.

        Args:
            renamed_log_dir (str, optional): name of the renamed log directory. Defaults to None.
        """
        self.log.info("-" * 80)
        self.log.info("DAOS functional test launcher")
        self.log.info("")
        self.log.info("Running with %s", self.avocado)
        self.log.info("Logging launch results to: %s", self.logdir)
        if renamed_log_dir is not None:
            self.log.info("  Renamed existing launch log directory to %s", renamed_log_dir)
        self.log.info("Launch log file: %s", self.logfile)
        self.log.info("-" * 80)

    def set_test_list(self, tags):
        """Define the list of tests and avocado tag filters from a list of tags.

        Args:
            tags (list): a list of tag or test file names

        Raises:
            LaunchException: if there is a problem listing tests

        """
        self.log.debug("-" * 80)
        self.tests = []
        self.tag_filters = []

        # Determine if fault injection is enabled
        fault_tag = "-faults"
        fault_filter = f"--filter-by-tags={fault_tag}"
        faults_enabled = self._faults_enabled()

        # Determine if each tag list entry is a tag or file specification
        test_files = []
        for tag in tags:
            if os.path.isfile(tag):
                # Assume an existing file is a test and add it to the list of tests
                test_files.append(tag)
                if not faults_enabled and fault_filter not in self.tag_filters:
                    self.tag_filters.append(fault_filter)
            else:
                # Otherwise it is assumed that this is a tag
                if not faults_enabled:
                    tag = ",".join((tag, fault_tag))
                self.tag_filters.append(f"--filter-by-tags={tag}")

        # Get the avocado list command to find all the tests that match the specified files and tags
        command = self.avocado.get_list_command()
        command.extend(self.tag_filters)
        command.extend(test_files)
        if not test_files:
            command.append("./")

        # Find all the test files that contain tests matching the tags
        self.log.info("Detecting tests matching tags: %s", " ".join(command))
        output = run_local(self.log, command, check=True)
        for index, test_file in enumerate(set(re.findall(r"INSTRUMENTED\s+(.*):", output.stdout))):
            self.tests.append(TestInfo(test_file, index + 1))
            self.log.info("  %s", self.tests[-1])

    def _faults_enabled(self):
        """Determine if fault injection is enabled.

        Returns:
            bool: whether or not fault injection is enabled

        """
        self.log.debug("Checking for fault injection enablement via 'fault_status':")
        try:
            run_local(self.log, ["fault_status"], check=True)
            self.log.debug("  Fault injection is enabled")
            return True
        except LaunchException:
            # Command failed or yielded a non-zero return status
            self.log.debug("  Fault injection is disabled")
        return False

    def run(self, sparse, fail_fast, extra_yaml, stop_daos, archive, rename, jenkinslog,
            core_files, threshold):
        """Run all the tests.

        Args:
            sparse (bool): whether or not to display the shortened avocado test output
            fail_fast (bool): whether or not to fail the avocado run command upon the first failure
            extra_yaml (list): optional test yaml file to use when running the test
            stop_daos (bool): whether or not to stop daos servers/clients after the test
            archive (bool): whether or not to collect remote files generated by the test
            rename (bool): whether or not to rename the default avocado job-results directory names
            jenkinslog (bool): whether or not to update the results.xml to use Jenkins-style names
            core_files (bool): whether or not to check for an process core files
            threshold (str): optional upper size limit for test log files

        Returns:
            int: status code to use when exiting launch.py

        """
        return_code = 0

        # Determine the location of the avocado logs for archiving or renaming
        self.log.info("Avocado logs stored in %s", self.job_results_dir)

        # Run each test for as many repetitions as requested
        for repeat in range(1, self.repeat + 1):
            self.log.info("-" * 80)
            self.log.info("Starting test repetition %s/%s", repeat, self.repeat)

            for test in self.tests:
                # Define a log for the execution of this test for this repetition
                test_log_file = test.get_log_file(self.logdir, repeat, self.repeat)

                self.log.info(
                    "Log file for repetition %s of running %s: %s", repeat, test, test_log_file)
                test_file_handler = self._get_file_handler(test_log_file)
                self.log.addHandler(test_file_handler)

                # Create a new TestResult for this test
                self.result.tests.append(TestResult(test.class_name, test.name, test_log_file))

                # Mark the start of this test
                self.result.tests[-1].start()

                # Prepare the hosts to run the tests
                step_status = self.prepare(test, repeat)
                if step_status:
                    return_code |= step_status
                    continue

                # Run the test with avocado
                return_code |= self.execute(test, repeat, sparse, fail_fast, extra_yaml)

                # Archive the test results
                return_code |= self.process(
                    test, repeat, stop_daos, archive, rename, jenkinslog, core_files, threshold)

                # Mark the execution of the test as passed if nothing went wrong
                if self.result.tests[-1].status is None:
                    self.result.tests[-1].status = TestResult.PASS

                # Mark the end of this test
                self.result.tests[-1].end()

                # Stop logging to the test log file
                self.log.removeHandler(test_file_handler)

        # Generate a results.xml for the this run
        create_xml(self.job, self.result)

        # Generate a results.html for the this run
        create_html(self.job, self.result)

        # Summarize the run
        return self._summarize_run(return_code)

    def display_disk_space(self, path):
        """Display disk space of provided path destination.

        Args:
            log (logger): logger for the messages produced by this method
            path (str): path to directory to print disk space for.
        """
        self.log.debug("Current disk space usage of %s", path)
        try:
            run_local(self.log, ["df", "-h", path], check=False)
        except LaunchException:
            pass

    def prepare(self, test, repeat):
        """Prepare the test for execution.

        Args:
            test (TestInfo): the test information
            repeat (int): the test repetition number

        Returns:
            int: status code: 0 = success, 4 = failure

        """
        self.log.debug("=" * 80)
        self.log.info("Preparing to run the %s test on repeat %s/%s", test, repeat, self.repeat)

        # Setup (remove/create/list) the common DAOS_TEST_LOG_DIR directory on each test host
        try:
            setup_test_directory(self.log, test)
        except LaunchException as error:
            message = "Error setting up test directories"
            self.log.debug(message, exc_info=True)
            self.result.tests[-1].set_status(TestResult.ERROR, "Prepare", message, error)
            return 4

        # Generate certificate files for the test
        try:
            generate_certs(self.log)
        except LaunchException as error:
            message = "Error generating certificates"
            self.log.debug(message, exc_info=True)
            self.result.tests[-1].set_status(TestResult.ERROR, "Prepare", message, error)
            return 4

        return 0

    def execute(self, test, repeat, sparse, fail_fast, extra_yaml):
        """Run the specified test.

        Args:
            test (TestInfo): the test information
            repeat (int): the test repetition number
            sparse (bool): whether to use avocado sparse output
            fail_fast(bool): whether to use the avocado fail fast option
            extra_yaml (list): whether to use an exta yaml file with the avocado run command

        Returns:
            int: status code: 0 = success, >0 = failure

        """
        self.log.debug("=" * 80)
        command = self.avocado.get_run_command(
            test, self.tag_filters, sparse, fail_fast, extra_yaml)
        self.log.info(
            "Running the %s test on repeat %s/%s: %s", test, repeat, self.repeat, " ".join(command))
        start_time = int(time.time())

        try:
            return_code = run_local(self.log, command, capture_output=False, check=False).returncode
            if return_code:
                self._collect_crash_files()

        except LaunchException as error:
            message = f"Error executing {test} on repeat {repeat}"
            self.log.debug(message, exc_info=True)
            self.result.tests[-1].set_status(TestResult.ERROR, "Execute", message, error)
            return_code = 1

        end_time = int(time.time())
        self.log.info("Total test time: %ss", end_time - start_time)
        return return_code

    def _collect_crash_files(self):
        """Move any avocado crash files into job-results/latest/crashes.

        Args:
            log (logger): logger for the messages produced by this method
            avocado_logs_dir (str): path to the avocado log files.
        """
        avocado_logs_dir = self.avocado.get_logs_dir()
        crash_dir = os.path.join(avocado_logs_dir.replace("job-results", "data"), "crashes")
        if os.path.isdir(crash_dir):
            crash_files = [
                os.path.join(crash_dir, crash_file)
                for crash_file in os.listdir(crash_dir)
                if os.path.isfile(os.path.join(crash_dir, crash_file))]

            if crash_files:
                latest_crash_dir = os.path.join(avocado_logs_dir, "latest", "crashes")
                try:
                    run_local(self.log, ["mkdir", "-p", latest_crash_dir], check=True)
                    for crash_file in crash_files:
                        run_local(self.log, ["mv", crash_file, latest_crash_dir], check=True)
                except LaunchException as error:
                    message = "Error collecting crash files"
                    self.log.debug(message, exc_info=True)
                    self.result.tests[-1].set_status(TestResult.ERROR, "Execute", message, error)
            else:
                self.log.debug("No avocado crash files found in %s", crash_dir)

    def process(self, test, repeat, stop_daos, archive, rename, jenkinslog, core_files, threshold):
        """Process the test results.

        This may include (depending upon argument values):
            - Stopping any running servers or agents
            - Resetting the server storage
            - Archiving any files generated by the test and including them with the test results
            - Renaming the test results directory and results.xml entries
            - Processing any core files generated by the test

        Args:
            test (TestInfo): the test information
            repeat (int): the test repetition number
            stop_daos (bool): whether or not to stop daos servers/clients after the test
            archive (bool): whether or not to collect remote files generated by the test
            rename (bool): whether or not to rename the default avocado job-results directory names
            jenkinslog (bool): whether or not to update the results.xml to use Jenkins-style names
            core_files (bool): whether or not to check for an process core files
            threshold (str): optional upper size limit for test log files

        Returns:
            int: status code: 0 = success, >0 = failure

        """
        return_code = 0
        self.log.debug("=" * 80)
        self.log.info(
            "Processing the %s test after the run on repeat %s/%s", test, repeat, self.repeat)

        # Stop any agents or servers running via systemd
        if stop_daos:
            return_code |= stop_daos_agent_services(self.log, test)
            return_code |= stop_daos_server_service(self.log, test)
            return_code |= reset_server_storage(self.log, test)

        # Optionally store all of the server and client config files and remote logs along with
        # this test's results. Also report an error if the test generated any log files with a
        # size exceeding the threshold.
        if archive:
            daos_test_log_dir = os.environ.get("DAOS_TEST_LOG_DIR", DEFAULT_DAOS_TEST_LOG_DIR)
            remote_files = OrderedDict()
            remote_files["local configuration files"] = {
                "source": daos_test_log_dir,
                "destination": os.path.join(self.job_results_dir, "latest", "daos_configs"),
                "pattern": "*_*_*.yaml",
                "hosts": self.local_host,
                "depth": 1,
            }
            remote_files["remote configuration files"] = {
                "source": os.path.join(os.sep, "etc", "daos"),
                "destination": os.path.join(self.job_results_dir, "latest", "daos_configs"),
                "pattern": "daos_*.yml",
                "hosts": test.hosts.all_remote,
                "depth": 1,
            }
            remote_files["daos log files"] = {
                "source": daos_test_log_dir,
                "destination": os.path.join(self.job_results_dir, "latest", "daos_logs"),
                "pattern": "*log*",
                "hosts": test.hosts.all,
                "depth": 1,
            }
            remote_files["cart log files"] = {
                "source": daos_test_log_dir,
                "destination": os.path.join(self.job_results_dir, "latest", "cart_logs"),
                "pattern": "*log*",
                "hosts": test.hosts.all,
                "depth": 2,
            }
            remote_files["ULTs stacks dump files"] = {
                "source": os.path.join(os.sep, "tmp"),
                "destination": os.path.join(self.job_results_dir, "latest", "daos_dumps"),
                "pattern": "daos_dump*.txt*",
                "hosts": test.hosts.servers,
                "depth": 1,
            }
            remote_files["valgrind log files"] = {
                "source": os.environ.get("DAOS_TEST_SHARED_DIR", os.environ['HOME']),
                "destination": os.path.join(self.job_results_dir, "latest", "valgrind_logs"),
                "pattern": "valgrind*",
                "hosts": test.hosts.servers,
                "depth": 1,
            }
            for summary, data in remote_files.items():
                if not data["hosts"]:
                    continue
                try:
                    self._archive_files(
                        summary, data["hosts"].copy(), data["source"], data["pattern"],
                        data["destination"], data["depth"], threshold)

                except LaunchException as error:
                    message = f"Error archiving {summary}"
                    self.log.debug(message, exc_info=True)
                    self.result.tests[-1].set_status(TestResult.ERROR, "Process", message, error)
                    return_code |= 16

                except Exception as error:      # pylint: disable=broad-except
                    message = f"Unexpected error archiving {summary}"
                    self.log.debug(message, exc_info=True)
                    self.result.tests[-1].set_status(TestResult.ERROR, "Process", message, error)
                    return_code |= 16

        # Optionally rename the test results directory for this test
        if rename:
            try:
                self._rename_avocado_test_dir(test, jenkinslog)

            except LaunchException as error:
                message = "Error renaming test results"
                self.log.debug(message, exc_info=True)
                self.result.tests[-1].set_status(TestResult.ERROR, "Process", message, error)
                return_code |= 1024

        # Optionally process core files
        if core_files:
            core_file_processing = CoreFileProcessing(self.log)
            try:
                if not core_file_processing.get_stacktraces(self.job_results_dir, test.hosts.all):
                    return_code |= 256

            except LaunchException as error:
                message = "Error processing test core files"
                self.log.debug(message, exc_info=True)
                self.result.tests[-1].set_status(TestResult.ERROR, "Process", message, error)
                return_code |= 256

            except Exception as error:  # pylint: disable=broad-except
                message = "Unhandled error processing test core files"
                self.log.debug(message, exc_info=True)
                self.result.tests[-1].set_status(TestResult.ERROR, "Process", message, error)
                return_code |= 256

        return return_code

    def _archive_files(self, summary, hosts, source, pattern, destination, depth, threshold):
        """Archive the files from the source to the destination.

        Args:
            summary (str): description of the files being processed
            hosts (NodSet): hosts on which the files are located
            source (str): where the files are currently located
            pattern (str): pattern used to limit which files are processed
            destination (str): where the files should be moved to on this host
            depth (int): max depth for find command
            threshold (str): optional upper size limit for test log files

        Raises:
            LaunchException: if there was a problem archiving the files

        """
        errors = []
        self.log.debug("=" * 80)
        self.log.info(
            "Archiving %s from %s:%s to %s",
            summary, hosts, os.path.join(source, pattern), destination)
        self.log.debug("  Remote hosts: %s", hosts.difference(self.local_host))
        self.log.debug("  Local host:   %s", hosts.intersection(self.local_host))

        # Get a list of remote files and their sizes
        try:
            if not self._list_files(hosts, source, pattern, depth):
                # If no files are found then there is nothing else to do
                self.log.debug("No %s files found on %s", os.path.join(source, pattern), hosts)
                return
        except LaunchException:
            errors.append("listing files")

        if "log" in pattern:
            # Report an error if any files sizes exceed the threshold
            if not self._check_log_size(hosts, source, pattern, depth, threshold):
                errors.append(f"verifying file sizes do not exceed {threshold}")

            # Run cart_logtest on log files
            if not self._cart_log_test(hosts, source, pattern, depth):
                errors.append("running cart_logtest")

        # Remove any empty files
        if not self._remove_empty_files(hosts, source, pattern, depth):
            errors.append("removing zero-length files")

        # Compress any files larger than 1 MB
        if not self._compress_files(hosts, source, pattern, depth):
            errors.append("compressing files")

        # Move the test files to the test-results directory on this host
        if not self._move_files(hosts, source, pattern, destination, depth):
            errors.append("moving files")

        # Report any errors
        if errors:
            raise LaunchException(f"Errors archiving: {', '.join(errors)}")

    def _list_files(self, hosts, source, pattern, depth):
        """List the files in source with that match the pattern.

        Args:
            hosts (NodSet): hosts on which the files are located
            source (str): where the files are currently located
            pattern (str): pattern used to limit which files are processed
            depth (int): max depth for find command

        Raises:
            LaunchException: if there was a error running the command to get the list of files

        Returns:
            bool: True if files where found; False otherwise

        """
        self.log.debug("-" * 80)
        self.log.debug("Listing any %s files on %s", os.path.join(source, pattern), hosts)
        command = f"find {source} -maxdepth {depth} -type f -name '{pattern}' -printf '%p %k KB\n'"
        result = run_remote(self.log, hosts, command)
        if not result.passed:
            message = f"Error determining if {os.path.join(source, pattern)} files exist on {hosts}"
            self.log.debug(message)
            raise LaunchException(message)
        for data in result.output:
            for line in data.stdout:
                if source in line:
                    return True
        return False

    def _check_log_size(self, hosts, source, pattern, depth, threshold):
        """Check if any file sizes exceed the threshold.

        Args:
            hosts (NodSet): hosts on which the files are located
            source (str): where the files are currently located
            pattern (str): pattern used to limit which files are processed
            depth (int): max depth for find command
            threshold (str): optional upper size limit for test log files

        Returns:
            bool: True if successful; False otherwise

        """
        if threshold is None:
            return True
        self.log.debug("-" * 80)
        self.log.debug(
            "Checking for any %s files in %s exceeding %s on %s", pattern, source, threshold, hosts)
        command = (f"find {source} -maxdepth {depth} -type f -name '{pattern}' -size +{threshold} "
                   "-printf '%p %k KB'")
        result = run_remote(self.log, hosts, command)
        if not result.passed:
            return False

        # The command output will include the source path if the threshold has been exceeded
        for data in result.output:
            if source in "\n".join(data.stdout):
                self.log.debug("One or more log file sizes exceeds the %s threshold", threshold)
                return False

        self.log.debug("No log file sizes found exceeding the %s threshold", threshold)
        return True

    def _cart_log_test(self, hosts, source, pattern, depth):
        """Run cart_logtest on the log files.

        Args:
            hosts (NodSet): hosts on which the files are located
            source (str): where the files are currently located
            pattern (str): pattern used to limit which files are processed
            depth (int): max depth for find command

        Returns:
            bool: True if successful; False otherwise

        """
        cart_logtest = os.path.abspath(os.path.join("cart", "cart_logtest.py"))
        self.log.debug("-" * 80)
        self.log.debug(
            "Running %s on any %s files in %s on %s", cart_logtest, pattern, source, hosts)
        command = (
            f"find {source} -maxdepth {depth} -type f -name '{pattern}' -print0 | "
            f"xargs -0 -r0 -n1 -I % sh -c '{cart_logtest} % > %.cart_logtest 2>&1'")
        return run_remote(self.log, hosts, command).passed

    def _remove_empty_files(self, hosts, source, pattern, depth):
        """Remove any files with zero size.

        Args:
            hosts (NodSet): hosts on which the files are located
            source (str): where the files are currently located
            pattern (str): pattern used to limit which files are processed
            depth (int): max depth for find command

        Returns:
            bool: True if successful; False otherwise

        """
        self.log.debug("-" * 80)
        self.log.debug("Removing any zero-length %s files in %s on %s", pattern, source, hosts)
        command = f"find {source} -maxdepth {depth} -type f -name '{pattern}' -empty -print -delete"
        return run_remote(self.log, hosts, command).passed

    def _compress_files(self, hosts, source, pattern, depth):
        """Compress any files larger than 1M.

        Args:
            hosts (NodSet): hosts on which the files are located
            source (str): where the files are currently located
            pattern (str): pattern used to limit which files are processed
            depth (int): max depth for find command

        Returns:
            bool: True if successful; False otherwise

        """
        self.log.debug("-" * 80)
        self.log.debug(
            "Compressing any %s files in %s on %s larger than 1M", pattern, source, hosts)
        command = (
            f"find {source} -maxdepth {depth} -type f -name '{pattern}' -size +1M -print0 | "
            "sudo xargs -0 -r0 lbzip2 -v")
        return run_remote(self.log, hosts, command).passed

    def _move_files(self, hosts, source, pattern, destination, depth):
        """Move files from the source to the destination.

        Args:
            hosts (NodSet): hosts on which the files are located
            source (str): where the files are currently located
            pattern (str): pattern used to limit which files are processed
            destination (str): where the files should be moved to on this host
            depth (int): max depth for find command

        Returns:
            bool: True if successful; False otherwise

        """
        self.log.debug("-" * 80)
        self.log.debug("Moving files from %s to %s on %s", source, destination, hosts)
        os.makedirs(destination, exist_ok=True)

        # Use the last directory in the destination path to create a temporary sub-directory on the
        # remote hosts in which all the source files matching the pattern will be copied. The entire
        # temporary sub-directory will then be copied back to this host and renamed as the original
        # destination directory plus the name of the host from which the files originated. Finally
        # delete this temporary sub-directory to remove the files from the remote hosts.
        rcopy_dest, tmp_copy_dir = os.path.split(destination)
        tmp_copy_dir = os.path.join(source, tmp_copy_dir)
        sudo = "sudo " if source.startswith("/etc") or source.startswith("/tmp") else ""

        # Create a temporary remote directory
        command = f"{sudo}mkdir -p {tmp_copy_dir}"
        if not run_remote(self.log, hosts, command).passed:
            self.log.debug("Error creating temporary remote copy directory %s", tmp_copy_dir)
            return False

        # Move all the source files matching the pattern into the temporary remote directory
        command = (f"find {source} -maxdepth {depth} -type f -name '{pattern}' -print0 | "
                   f"xargs -0 -r0 -I '{{}}' {sudo}mv '{{}}' {tmp_copy_dir}/")
        if not run_remote(self.log, hosts, command).passed:
            self.log.debug("Error moving files to temporary remote copy directory %s", tmp_copy_dir)
            return False

        # Clush -rcopy the temporary remote directory to this host
        command = ["clush", "-v", "-w", str(hosts), "--rcopy", tmp_copy_dir, "--dest", rcopy_dest]
        status = True
        try:
            run_local(self.log, command, check=True)

        except LaunchException:
            self.log.debug("Error copying remote files to %s", destination, exc_info=True)
            status = False

        finally:
            # Remove the temporary remote directory on each host
            command = f"{sudo}rm -fr {tmp_copy_dir}"
            if not run_remote(self.log, hosts, command).passed:
                self.log.debug("Error removing archived files temporary directory")
                status = False

        return status

    def _rename_avocado_test_dir(self, test, jenkinslog):
        """Append the test name to its avocado job-results directory name.

        Args:
            test (TestInfo): the test information
            jenkinslog (bool): whether to update the results.xml with the Jenkins test names

        Raises:
            LaunchException: if there is an error renaming the avocado test directory

        """
        avocado_logs_dir = self.avocado.get_logs_dir()
        test_logs_lnk = os.path.join(avocado_logs_dir, "latest")
        test_logs_dir = os.path.realpath(test_logs_lnk)

        self.log.debug("-" * 80)
        self.log.info("Renaming the avocado job-results directory")

        # Create the new avocado job-results test directory name
        new_test_logs_dir = "-".join([test_logs_dir, get_test_category(test.test_file)])
        if jenkinslog:
            new_test_logs_dir = os.path.join(avocado_logs_dir, test.directory, test.python_file)
            if self.repeat > 1:
                # When repeating tests ensure Jenkins-style avocado log directories
                # are unique by including the repeat count in the path
                new_test_logs_dir = os.path.join(
                    avocado_logs_dir, test.directory, test.python_file, test.repeat_str)
            try:
                os.makedirs(new_test_logs_dir)
            except OSError as error:
                message = f"Error creating {new_test_logs_dir}"
                self.log.debug(message, exc_info=True)
                raise LaunchException(message) from error

        # Rename the avocado job-results test directory and update the 'latest' symlink
        self.log.info("Renaming test results from %s to %s", test_logs_dir, new_test_logs_dir)
        try:
            os.rename(test_logs_dir, new_test_logs_dir)
            os.remove(test_logs_lnk)
            os.symlink(new_test_logs_dir, test_logs_lnk)
            self.log.debug("Renamed %s to %s", test_logs_dir, new_test_logs_dir)
        except OSError as error:
            message = f"Error renaming {test_logs_dir} to {new_test_logs_dir}"
            self.log.debug(message, exc_info=True)
            raise LaunchException(message) from error

        # Update the results.xml file with the new functional test class name
        if jenkinslog:
            xml_file = os.path.join(new_test_logs_dir, "results.xml")
            self.log.debug("Updating the 'classname' field in the %s", xml_file)
            try:
                with open(xml_file, encoding="utf-8") as xml_buffer:
                    xml_data = xml_buffer.read()
            except OSError as error:
                message = f"Error reading {xml_file}"
                self.log.debug(message, exc_info=True)
                raise LaunchException(message) from error

            # Save it for the Launchable [de-]mangle
            org_xml_data = xml_data

            # First, mangle the in-place file for Jenkins to consume
            xml_data = re.sub("classname=\"", f"classname=\"FTEST_{test.directory}.", xml_data)
            try:
                with open(xml_file, "w", encoding="utf-8") as xml_buffer:
                    xml_buffer.write(xml_data)
            except OSError as error:
                message = f"Error writing {xml_file}"
                self.log.debug(message, exc_info=True)
                raise LaunchException(message) from error

            # Now mangle (or rather unmangle back to canonical xunit1 format)
            # another copy for Launchable
            xml_file = xml_file[0:-11] + "xunit1_results.xml"
            self.log.debug("Updating the xml data for the Launchable %s file", xml_file)
            xml_data = org_xml_data
            org_name = r'(name=")\d+-\.\/.+.(test_[^;]+);[^"]+(")'
            new_name = rf'\1\2\3 file="{test.test_file}"'
            xml_data = re.sub(org_name, new_name, xml_data)
            try:
                with open(xml_file, "w", encoding="utf-8") as xml_buffer:
                    xml_buffer.write(xml_data)
            except OSError as error:
                message = f"Error writing {xml_file}"
                self.log.debug(message, exc_info=True)
                raise LaunchException(message) from error

    def _summarize_run(self, status):
        """Summarize any failures that occurred during testing.

        Args:
            status (int): overall status of running all tests

        Returns:
            int: status code to use when exiting launch.py

        """
        return_code = 0
        if status == 0:
            self.log.info("All avocado tests passed!")
            return return_code

        # Log any of errors that occurred during the run and determine an overall exit code
        bit_error_map = {
            1: "Failed avocado tests detected!",
            2: "ERROR: Failed avocado jobs detected!",
            4: "ERROR: Failed avocado commands detected!",
            8: "Interrupted avocado jobs detected!",
            16: "ERROR: Failed archiving files after one or more tests!",
            32: "ERROR: Failed log size threshold check after one or more tests!",
            64: "ERROR: Failed to create a junit xml test error file!",
            128: "ERROR: Failed to clean logs in preparation for test run!",
            256: "ERROR: Failed to process core files after one or more tests!",
            512: "ERROR: Failed to stop daos_server.service after one or more tests!",
            1024: "ERROR: Failed to rename logs and results after one or more tests!",
        }
        for bit_code, error_message in bit_error_map.items():
            if status & bit_code == bit_code:
                self.log.info(error_message)
                if self.mode == "ci" or (self.mode == "normal" and bit_code == 1) or bit_code == 8:
                    # In CI mode the errors are reported in the results.xml, so always return 0
                    # In normal mode avocado test failures do not yield a non-zero exit status
                    # Interrupted avocado tests do not yield a non-zero exit status
                    continue
                return_code = 1
        return return_code


class CoreFileProcessing():
    """Process core files generated by tests."""

    USE_DEBUGINFO_INSTALL = True

    def __init__(self, log):
        """Initialize a CoreFileProcessing object.

        Args:
            log (logger): object configured to log messages
        """
        # pylint: disable=import-outside-toplevel,import-error,no-name-in-module
        from util.distro_utils import detect

        self.log = log
        self.distro_info = detect()

    def get_stacktraces(self, avocado_logs_dir, test_hosts):
        """Copy all of the host test log files to the avocado results directory.

        Args:
            avocado_logs_dir (str): location of the avocado job logs
            test_hosts (NodeSet): hosts from which to collect core files

        Returns:
            bool: True if everything was done as expected, False if there were
                any issues processing core files

        """
        status = True
        daos_cores_dir = os.path.join(avocado_logs_dir, "latest", "stacktraces")
        self.log.debug("=" * 80)
        self.log.info("Processing cores from %s in %s", test_hosts, daos_cores_dir)

        # Processing core files is broken on EL7 currently
        if self.is_el7():
            self.log.debug("  Generating stacktraces is currently not suppotrted on EL7")
            return status

        # Create a subdirectory in the avocado logs directory for this test
        os.makedirs(daos_cores_dir, exist_ok=True)

        # Copy any core files that exist on the test hosts and remove them from the
        # test host if the copy is successful.  Attempt all of the commands and
        # report status at the end of the loop.  Include a listing of the file
        # related to any failed command.
        commands = [
            "set -eu",
            "rc=0",
            "copied=()",
            "df -h /var/tmp",
            "for file in /var/tmp/core.*",
            "do if [ -e $file ]",
            "then ls -al $file",
            "if [ ! -s $file ]",
            "then ((rc++))",
            "else if sudo chmod 644 $file && "
            f"scp $file {get_local_host()}:{daos_cores_dir}/${{file##*/}}-$(hostname -s)",
            "then copied+=($file)",
            "if ! sudo rm -fr $file",
            "then ((rc++))",
            "fi",
            "else ((rc++))",
            "fi",
            "fi",
            "fi",
            "done",
            "echo Copied ${copied[@]:-no files}",
            "exit $rc",
        ]
        result = run_remote(self.log, test_hosts, "; ".join(commands), timeout=1800)
        if not result.passed:
            # we might have still gotten some core files, so don't return here
            # but save a False return status for later
            status = False

        cores = os.listdir(daos_cores_dir)
        if not cores:
            return True

        try:
            self.install_debuginfos()
        except LaunchException as error:
            self.log.debug(error)
            self.log.debug("Removing core files to avoid archiving them")
            for corefile in cores:
                os.remove(os.path.join(daos_cores_dir, corefile))
            return False

        for corefile in cores:
            if not fnmatch.fnmatch(corefile, 'core.*[0-9]'):
                continue
            corefile_fqpn = os.path.join(daos_cores_dir, corefile)
            run_local(self.log, ['ls', '-l', corefile_fqpn])
            # can't use the file python magic binding here due to:
            # https://bugs.astron.com/view.php?id=225, fixed in:
            # https://github.com/file/file/commit/6faf2eba2b8c65fbac7acd36602500d757614d2f
            # but not available to us until that is in a released version
            # revert the commit this comment is in to see use python magic instead
            try:
                result = run_local(
                    self.log, ["gdb", "-c", corefile_fqpn, "-ex", "info proc exe", "-ex", "quit"])
                last_line = result.stdout.splitlines()[-1]
                cmd = last_line[7:-1]
                # assume there are no arguments on cmd
                find_char = "'"
                if cmd.find(" ") > -1:
                    # there are arguments on cmd
                    find_char = " "
                exe_name = cmd[0:cmd.find(find_char)]
            except LaunchException:
                self.log.debug("Error obtaining executable name for stacktrace", exc_info=True)
                exe_name = None

            if exe_name:
                cmd = [
                    "gdb", f"-cd={daos_cores_dir}",
                    "-ex", "set pagination off",
                    "-ex", "thread apply all bt full",
                    "-ex", "detach",
                    "-ex", "quit",
                    exe_name, corefile
                ]
                stack_trace_file = os.path.join(daos_cores_dir, f"{corefile}.stacktrace")
                try:
                    output = run_local(self.log, cmd, check=False)
                    with open(stack_trace_file, "w", encoding="utf-8") as stack_trace:
                        stack_trace.writelines(output.stdout)
                except IOError as error:
                    self.log.debug("Error writing %s: %s", stack_trace_file, error)
                    status = False
                except LaunchException as error:
                    self.log.debug("Error creating %s: %s", stack_trace_file, error)
                    status = False
            else:
                self.log.debug("Unable to determine executable name from gdb output")
                self.log.debug("Not creating stacktrace")
                status = False
            self.log.debug("Removing %s", corefile_fqpn)
            os.unlink(corefile_fqpn)

        return status

    def install_debuginfos(self):
        """Install debuginfo packages.

        NOTE: This does assume that the same daos packages that are installed
            on the nodes that could have caused the core dump are installed
            on this node also.

        Args:
            log (logger): logger for the messages produced by this method

        Raises:
            LaunchException: if there is an error installing debuginfo packages

        """
        install_pkgs = [{'name': 'gdb'}]
        if self.is_el():
            install_pkgs.append({'name': 'python3-debuginfo'})

        cmds = []

        # -debuginfo packages that don't get installed with debuginfo-install
        for pkg in ['systemd', 'ndctl', 'mercury', 'hdf5', 'argobots', 'libfabric',
                    'hdf5-vol-daos', 'hdf5-vol-daos-mpich', 'hdf5-vol-daos-mpich-tests',
                    'hdf5-vol-daos-openmpi', 'hdf5-vol-daos-openmpi-tests', 'ior']:
            try:
                debug_pkg = self.resolve_debuginfo(pkg)
            except LaunchException as error:
                self.log.error("Failed trying to install_debuginfos(): %s", str(error))
                raise

            if debug_pkg and debug_pkg not in install_pkgs:
                install_pkgs.append(debug_pkg)

        # remove any "source tree" test hackery that might interfere with RPM installation
        path = os.path.join(os.path.sep, "usr", "share", "spdk", "include")
        if os.path.islink(path):
            cmds.append(["sudo", "rm", "-f", path])

        if self.USE_DEBUGINFO_INSTALL:
            dnf_args = ["--exclude", "ompi-debuginfo"]
            if os.getenv("TEST_RPMS", 'false') == 'true':
                if "suse" in self.distro_info.name.lower():
                    dnf_args.extend(["libpmemobj1", "python3", "openmpi3"])
                elif "centos" in self.distro_info.name.lower() and self.distro_info.version == "7":
                    dnf_args.extend(
                        ["--enablerepo=*-debuginfo", "--exclude", "nvml-debuginfo", "libpmemobj",
                         "python36", "openmpi3", "gcc"])
                elif self.is_el() and self.distro_info.version == "8":
                    dnf_args.extend(
                        ["--enablerepo=*-debuginfo", "libpmemobj", "python3", "openmpi", "gcc"])
                else:
                    raise LaunchException(
                        f"install_debuginfos(): Unsupported distro: {self.distro_info}")
                cmds.append(["sudo", "dnf", "-y", "install"] + dnf_args)
            output = run_local(self.log, ["rpm", "-q", "--qf", "%{evr}", "daos"], check=False)
            rpm_version = output.stdout
            cmds.append(
                ["sudo", "dnf", "debuginfo-install", "-y"] + dnf_args
                + ["daos-client-" + rpm_version, "daos-server-" + rpm_version,
                   "daos-tests-" + rpm_version])
        # else:
        #     # We're not using the yum API to install packages
        #     # See the comments below.
        #     kwargs = {'name': 'gdb'}
        #     yum_base.install(**kwargs)

        # This is how you normally finish up a yum transaction, but
        # again, we need to employ sudo
        # yum_base.resolveDeps()
        # yum_base.buildTransaction()
        # yum_base.processTransaction(rpmDisplay=yum.rpmtrans.NoOutputCallBack())

        # Now install a few pkgs that debuginfo-install wouldn't
        cmd = ["sudo", "dnf", "-y"]
        if self.is_el() or "suse" in self.distro_info.name.lower():
            cmd.append("--enablerepo=*debug*")
        cmd.append("install")
        for pkg in install_pkgs:
            try:
                cmd.append(f"{pkg['name']}-{pkg['version']}-{pkg['release']}")
            except KeyError:
                cmd.append(pkg['name'])

        cmds.append(cmd)

        retry = False
        for cmd in cmds:
            try:
                run_local(self.log, cmd, check=True)
            except LaunchException:
                # got an error, so abort this list of commands and re-run
                # it with a dnf clean, makecache first
                retry = True
                break
        if retry:
            self.log.debug("Going to refresh caches and try again")
            cmd_prefix = ["sudo", "dnf"]
            if self.is_el() or "suse" in self.distro_info.name.lower():
                cmd_prefix.append("--enablerepo=*debug*")
            cmds.insert(0, cmd_prefix + ["clean", "all"])
            cmds.insert(1, cmd_prefix + ["makecache"])
            for cmd in cmds:
                try:
                    run_local(self.log, cmd)
                except LaunchException:
                    break

    def is_el(self):
        """Determine if the distro EL based.

        Args:
            distro (str): distribution to verify

        Returns:
            list: type of EL distribution

        """
        el_distros = ["almalinux", "rocky", "centos", "rhel"]
        return [d for d in el_distros if d in self.distro_info.name.lower()]

    def is_el7(self):
        """Determine if the distribution is CentOS 7.

        Returns:
            bool: True if the distribution is CentOS 7

        """
        return self.is_el() and self.distro_info.version == "7"

    def resolve_debuginfo(self, pkg):
        """Return the debuginfo package for a given package name.

        Args:
            pkg (str): a package name

        Raises:
            LaunchException: if there is an error searching for RPMs

        Returns:
            dict: dictionary of debug package information

        """
        package_info = None
        try:
            # Eventually use python libraries for this rather than exec()ing out
            output = run_local(
                self.log, ["rpm", "-q", "--qf", "%{name} %{version} %{release} %{epoch}", pkg],
                check=False)
            name, version, release, epoch = output.stdout.split()

            debuginfo_map = {"glibc": "glibc-debuginfo-common"}
            try:
                debug_pkg = debuginfo_map[name]
            except KeyError:
                debug_pkg = f"{name}-debuginfo"
            package_info = {
                "name": debug_pkg,
                "version": version,
                "release": release,
                "epoch": epoch
            }
        except ValueError:
            self.log.debug("Package %s not installed, skipping debuginfo", pkg)

        return package_info


def main():
    """Launch DAOS functional tests."""
    # pylint: disable=too-many-branches
    # Parse the command line arguments
    description = [
        "DAOS functional test launcher",
        "",
        "Launches tests by specifying a test tag.  For example:",
        "\tbadconnect  --run pool connect tests that pass NULL ptrs, etc.",
        "\tbadevict    --run pool client evict tests that pass NULL ptrs, "
        "etc.",
        "\tbadexclude  --run pool target exclude tests that pass NULL ptrs, "
        "etc.",
        "\tbadparam    --run tests that pass NULL ptrs, etc.",
        "\tbadquery    --run pool query tests that pass NULL ptrs, etc.",
        "\tmulticreate --run tests that create multiple pools at once",
        "\tmultitarget --run tests that create pools over multiple servers",
        "\tpool        --run all pool related tests",
        "\tpoolconnect --run all pool connection related tests",
        "\tpooldestroy --run all pool destroy related tests",
        "\tpoolevict   --run all client pool eviction related tests",
        "\tpoolinfo    --run all pool info retrieval related tests",
        "\tquick       --run tests that complete quickly, with minimal "
        "resources",
        "",
        "Multiple tags can be specified:",
        "\ttag_a,tag_b -- run all tests with both tag_a and tag_b",
        "\ttag_a tag_b -- run all tests with either tag_a or tag_b",
        "",
        "Specifying no tags will run all of the available tests.",
        "",
        "Tests can also be launched by specifying a path to the python script "
        "instead of its tag.",
        "",
        "The placeholder server and client names in the yaml file can also be "
        "replaced with the following options:",
        "\tlaunch.py -ts node1,node2 -tc node3 <tag>",
        "\t  - Use node[1-2] to run the daos server in each test",
        "\t  - Use node3 to run the daos client in each test",
        "\tlaunch.py -ts node1,node2 <tag>",
        "\t  - Use node[1-2] to run the daos server or client in each test",
        "\tlaunch.py -ts node1,node2 -d <tag>",
        "\t  - Use node[1-2] to run the daos server or client in each test",
        "\t  - Discard of any additional server or client placeholders for "
        "each test",
        "",
        "You can also specify the sparse flag -s to limit output to "
        "pass/fail.",
        "\tExample command: launch.py -s pool"
    ]
    parser = ArgumentParser(
        prog="launcher.py",
        formatter_class=RawDescriptionHelpFormatter,
        description="\n".join(description))
    parser.add_argument(
        "-a", "--archive",
        action="store_true",
        help="archive host log files in the avocado job-results directory")
    parser.add_argument(
        "-c", "--clean",
        action="store_true",
        help="remove daos log files from the test hosts prior to the test")
    parser.add_argument(
        "-dsd", "--disable_stop_daos",
        action="store_true",
        help="disable stopping DAOS servers and clients between running tests")
    parser.add_argument(
        "-e", "--extra_yaml",
        action="append",
        default=None,
        type=str,
        help="additional yaml file to include with the test yaml file. Any "
             "entries in the extra yaml file can be used to replace an "
             "existing entry in the test yaml file.")
    parser.add_argument(
        "--failfast",
        action="store_true",
        help="stop the test suite after the first failure")
    parser.add_argument(
        "-i", "--include_localhost",
        action="store_true",
        help="include the local host when cleaning and archiving")
    parser.add_argument(
        "-ins", "--insecure_mode",
        action="store_true",
        help="Launch test with insecure-mode")
    parser.add_argument(
        "-j", "--jenkinslog",
        action="store_true",
        help="rename the avocado test logs directory for publishing in Jenkins")
    parser.add_argument(
        "-l", "--list",
        action="store_true",
        help="list the python scripts that match the specified tags")
    parser.add_argument(
        "-m", "--modify",
        action="store_true",
        help="modify the test yaml files but do not run the tests")
    parser.add_argument(
        "-mo", "--mode",
        choices=['normal', 'manual', 'ci'],
        default='normal',
        help="provide the mode of test to be run under. Default is normal, "
             "in which the final return code of launch.py is still zero if "
             "any of the tests failed. 'manual' is where the return code is "
             "non-zero if any of the tests as part of launch.py failed.")
    parser.add_argument(
        "-na", "--name",
        action="store",
        default="_".join(os.environ.get("STAGE_NAME", "Functional Manual").split()),
        type=str,
        help="avocado job-results directory name in which to place the launch log files."
             "If a directory with this name already exists it will be renamed with a '_old' suffix")
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
        "-o", "--override",
        action="store_true",
        help="override the quantity of replacement values used in the test yaml file.")
    parser.add_argument(
        "-p", "--process_cores",
        action="store_true",
        help="process core files from tests")
    parser.add_argument(
        "-pr", "--provider",
        action="store",
        choices=[None] + list(PROVIDER_KEYS.values()),
        default=None,
        type=str,
        help="default provider to use in the test daos_server config file, "
             f"e.g. {', '.join(list(PROVIDER_KEYS.values()))}")
    parser.add_argument(
        "-r", "--rename",
        action="store_true",
        help="rename the avocado test logs directory to include the test name")
    parser.add_argument(
        "-re", "--repeat",
        action="store",
        default=1,
        type=int,
        help="number of times to repeat test execution")
    parser.add_argument(
        "-s", "--sparse",
        action="store_true",
        help="limit output to pass/fail")
    parser.add_argument(
        "tags",
        nargs="*",
        type=str,
        help="test category or file to run")
    parser.add_argument(
        "-tc", "--test_clients",
        action="store",
        help="comma-separated list of hosts to use as replacement values for "
             "client placeholders in each test's yaml file")
    parser.add_argument(
        "-th", "--logs_threshold",
        action="store",
        help="collect log sizes and report log sizes that go past provided"
             "threshold. e.g. '-th 5M'"
             "Valid threshold units are: B, K, M, G, T")
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
        "-v", "--verbose",
        action="count",
        default=0,
        help="verbosity output level. Specify multiple times (e.g. -vv) for "
             "additional output")
    parser.add_argument(
        "-y", "--yaml_directory",
        action="store",
        default=None,
        help="directory in which to write the modified yaml files. A temporary "
             "directory - which only exists for the duration of the launch.py "
             "command - is used by default.")
    args = parser.parse_args()

    # Setup the Launch object
    launch = Launch(args.name, args.repeat, args.mode)

    # Override arguments via the mode
    if args.mode == "ci":
        args.archive = True
        args.clean = True
        args.include_localhost = True
        args.jenkinslog = True
        args.process_cores = True
        args.rename = True
        args.sparse = True
        if not args.logs_threshold:
            args.logs_threshold = "1G"

    # Record the command line arguments
    launch.log.debug("Arguments:")
    for key in sorted(args.__dict__.keys()):
        launch.log.debug("  %s = %s", key, getattr(args, key))

    # Convert host specifications into NodeSets
    args.test_servers = NodeSet(args.test_servers)
    args.test_clients = NodeSet(args.test_clients)

    # A list of server hosts is required
    if not args.test_servers and not args.list:
        launch.log.error("Error: Missing a required '--test_servers' argument.")
        sys.exit(1)
    launch.log.info("Testing with hosts:       %s", args.test_servers.union(args.test_clients))

    # Setup the user environment
    try:
        set_test_environment(launch.log, args)
    except LaunchException:
        launch.log.error("Error setting test environment", exc_info=True)
        sys.exit(1)

    # Auto-detect nvme test yaml replacement values if requested
    if args.nvme and args.nvme.startswith("auto") and not args.list:
        try:
            args.nvme = get_device_replacement(launch.log, args)
        except LaunchException:
            launch.log.error(
                "Error auto-detecting NVMe test yaml file replacement values", exc_info=True)
            sys.exit(1)
    elif args.nvme and args.nvme.startswith("vmd:"):
        args.nvme = args.nvme.replace("vmd:", "")

    # Process the tags argument to determine which tests to run
    try:
        launch.set_test_list(args.tags)
    except LaunchException:
        launch.log.error("Error detecting tests that match tags: %s", args.tags, exc_info=True)
        sys.exit(1)

    # Verify at least one test was requested
    if not launch.tests:
        launch.log.error("Error: No tests found for tags '%s'", " ".join(args.tags), exc_info=True)
        sys.exit(1)

    # Done if just listing tests matching the tags
    if args.list and not args.modify:
        sys.exit(0)

    # Create a temporary directory
    if args.yaml_directory is None:
        # pylint: disable=consider-using-with
        temp_dir = TemporaryDirectory()
        yaml_dir = temp_dir.name
    else:
        yaml_dir = args.yaml_directory
        if not os.path.exists(yaml_dir):
            os.mkdir(yaml_dir)
    launch.log.info("Modified test yaml files being created in: %s", yaml_dir)

    # Create a dictionary of test and their yaml files
    setup_test_files(launch.log, launch.tests, args, yaml_dir)
    if args.modify:
        sys.exit(0)

    # Execute the tests
    status = launch.run(
        args.sparse, args.failfast, args.extra_yaml, not args.disable_stop_daos, args.archive,
        args.rename, args.jenkinslog, args.process_cores, args.logs_threshold)
    sys.exit(status)


if __name__ == "__main__":
    main()
