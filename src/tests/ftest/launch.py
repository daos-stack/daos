#!/usr/bin/env python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines

from argparse import ArgumentParser, RawDescriptionHelpFormatter
from collections import OrderedDict
from datetime import datetime
from tempfile import TemporaryDirectory
import errno
import json
import logging
import os
import re
import socket
import subprocess   # nosec
import site
import sys
import time
from xml.etree.ElementTree import Element, SubElement, tostring     # nosec
import yaml
from defusedxml import minidom
import defusedxml.ElementTree as ET

# from avocado.core.settings import settings
# from avocado.core.version import MAJOR, MINOR
from ClusterShell.NodeSet import NodeSet
from ClusterShell.Task import task_self

# Graft some functions from xml.etree into defusedxml etree.
ET.Element = Element
ET.SubElement = SubElement
ET.tostring = tostring

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
    def passed(self):
        """Did the command pass on all the hosts.

        Returns:
            bool: if the command was successful on each host

        """
        timeout = all(data.timeout for data in self.output)
        non_zero = any(data.returncode != 0 for data in self.output)
        return not non_zero and not timeout

    @property
    def homogeneous(self):
        """Did all the hosts produce the same output.

        Returns:
            bool: if all the hosts produced the same output
        """
        return len(self.output) == 1

    def _process_task(self, task, command):
        """Populate the output list and determine the passed result for the specified task.

        Args:
            task (Task): a ClusterShell.Task.Task object for the executed command
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
    # pylint: disable=too-few-public-methods
    """Defines the python test file and its associated test yaml file."""

    def __init__(self, test_file):
        """Initialize a TestInfo object.

        Args:
            test_file (str): the test python file
        """
        base, _ = os.path.splitext(test_file)
        self.test_file = test_file
        self.yaml_file = ".".join([base, "yaml"])

    def __str__(self):
        """Get the test file as a string.

        Returns:
            str: the test file

        """
        return self.test_file


def get_avocado_version():
    """Get the avocado major and minor version.

    Rasies:
        LaunchException: if there is an error getting the version from the avocado command

    Returns:
        tuple: major and minor avocado versions as strings

    """
    try:
        # pylint: disable=import-outside-toplevel
        from avocado.core.version import MAJOR, MINOR
        return MAJOR, MINOR

    except ModuleNotFoundError:
        # Once lightweight runs are using python3-avocado, this can be removed
        log = logging.getLogger(__name__)
        result = run_local(log, ["avocado", "-v"], check=True)
        try:
            return re.findall(r"(\d+)\.(\d+)", result.stdout)[0]
        except IndexError as error:
            raise LaunchException("Error extracting avocado version from command") from error


def get_avocado_setting(section, key, default=None):
    """Get the value for the specified avocado setting.

    Args:
        section (str): avocado setting section name
        key (str): avocado setting key name
        default (object): default value to use if setting is undefined

    Rasies:
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


def get_avocado_logs_dir():
    """Get the avocado directory in which the test results are stored.

    Returns:
        str: the directory used by avocado to log test results

    """
    default_base_dir = os.path.expanduser(os.path.join("~", "avocaod", "job-results"))
    return get_avocado_setting("datadir.paths", "logs_dir", default_base_dir)


def get_avocado_directory(test):
    """Get the avocado test directory for the test.

    Args:
        test (str): name of the test (file)

    Returns:
        str: the directory used by avocado to log test results

    """
    logs_dir = get_avocado_logs_dir()
    test_dir = os.path.join(logs_dir, test)
    os.makedirs(test_dir, exist_ok=True)
    return test_dir


def get_avocado_list_command():
    """Get the avocado list command for this version of avocado.

    Returns:
        list: avocado list command parts

    """
    MAJOR, _ = get_avocado_version()    # pylint: disable=invalid-name
    if int(MAJOR) >= 83:
        return ["avocado", "list"]
    if int(MAJOR) >= 82:
        return ["avocado", "--paginator=off", "list"]
    return ["avocado", "list", "--paginator=off"]


def get_avocado_run_command(test, tag_filter, sparse, failfast, extra_yaml):
    """Get the avocado run command for this version of avocado.

    Args:
        test (TestInfo): the test script/yaml file
        tag_filter (str): _description_
        sparse (bool): _description_
        failfast (bool): _description_

    Returns:
        list: avocado run command

    """
    MAJOR, _ = get_avocado_version()    # pylint: disable=invalid-name
    command = ["avocado"]
    if not sparse and int(MAJOR) >= 82:
        command.append("--show=test")
    command.append("run")
    if int(MAJOR) >= 82:
        command.append("--ignore-missing-references")
    else:
        command.extend(["--ignore-missing-references", "on"])
    if int(MAJOR) >= 83:
        command.append("--disable-tap-job-result")
    else:
        command.extend(["--html-job-result", "on"])
        command.extend(["--tap-job-result", "off"])
    if not sparse and int(MAJOR) < 82:
        command.append("--show-job-log")
    if tag_filter:
        command.extend(tag_filter)
    if failfast:
        command.extend(["--failfast", "on"])
    command.extend(["--mux-yaml", test.yaml_file])
    if extra_yaml:
        command.append(extra_yaml)
    command.extend(["--", str(test)])
    return command


def get_local_host():
    """Get the local host name.

    Returns:
        str: name of the local host
    """
    return socket.gethostname().split(".")[0]


def add_file_handler(log, log_file):
    """Add a logging.FileHandler to the specified logger.

    Args:
        log (logger): logger to which to add the FileHandler
        log_file (str): file in which to log debug messages
    """
    log_format = logging.Formatter("%(asctime)s %(levelname)-5s %(funcName)27s: %(message)s")
    log_handler = logging.FileHandler(log_file, encoding='utf-8')
    log_handler.setLevel(logging.DEBUG)
    log_handler.setFormatter(log_format)
    log.addHandler(log_handler)


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
    command_str = " ".join(command)
    kwargs = {"encoding": "utf-8", "shell": False, "check": check, "timeout": timeout}
    if capture_output:
        kwargs["stdout"] = subprocess.PIPE
        kwargs["stderr"] = subprocess.STDOUT
    if timeout:
        log.debug("Running on %s with a %s timeout: %s", get_local_host(), timeout, command_str)
    else:
        log.debug("Running on %s: %s", get_local_host(), command_str)

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
        log.debug("Command '%s' (rc=%s):", result.returncode)
        if result.stdout:
            for line in result.stdout.splitlines():
                log.debug("  %s", line)

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
        RemoteCommandResult: _description_

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


def display_disk_space(log, path):
    """Display disk space of provided path destination.

    Args:
        log (logger): logger for the messages produced by this method
        path (str): path to directory to print disk space for.
    """
    log.debug("Current disk space usage of %s", path)
    try:
        run_local(log, ["df", "-h", path])
    except LaunchException:
        pass


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


def get_temporary_directory(args, base_dir=None):
    """Get the temporary directory used by functional tests.

    Args:
        base_dir (str, optional): base installation directory. Defaults to None.

    Returns:
        str: the full path of the temporary directory

    """
    if base_dir is None:
        base_dir = get_build_environment(args)["PREFIX"]
    if base_dir == "/usr":
        tmp_dir = os.getenv("DAOS_TEST_SHARED_DIR", os.path.expanduser("~/daos_test"))
    else:
        tmp_dir = os.path.join(base_dir, "tmp")

    # Make sure the temporary directory exists to prevent pydaos import errors
    if not os.path.exists(tmp_dir):
        os.makedirs(tmp_dir)

    return tmp_dir


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
    # Get the default interface to use if OFI_INTERFACE is not set
    interface = os.environ.get("OFI_INTERFACE")
    if interface is None:
        # Find all the /sys/class/net interfaces on the launch node
        # (excluding lo)
        log.info("Detecting network devices - OFI_INTERFACE not set")
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
    log.info("Testing with '%s' as the default interface", interface)
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

    active_interfaces = {}
    result = run_remote(log, all_hosts, command)
    if result.passed:
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
    else:
        raise LaunchException(
            f"Error obtaining a default interface on {str(all_hosts)} from {net_path}")

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
    # Use the detected provider if one is not set
    if args.provider:
        provider = args.provider
    else:
        provider = os.environ.get("CRT_PHY_ADDR_STR")
    if provider is None:
        log.info("Detecting provider for %s - CRT_PHY_ADDR_STR not set", interface)

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
            keys_found = {}
            for data in result.output:
                for line in data.stdout:
                    provider_name = line.replace(":", "")
                    if provider_name in PROVIDER_KEYS:
                        if provider_name not in keys_found:
                            keys_found[provider_name] = NodeSet()
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
    log.info("Testing with '%s' as the provider", provider)
    log.debug("Testing with CRT_PHY_ADDR_STR=%s", os.environ["CRT_PHY_ADDR_STR"])


def set_python_environment(log):
    """Set up the test python environment.

    Args:
        log (logger): logger for the messages produced by this method
    """
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

    log.info(
        "Auto-detected %s devices on %s: %s", " & ".join(device_types), args.test_servers, devices)
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


def get_test_list(log, tags):
    """Generate a list of tests and avocado tag filter from a list of tags.

    Args:
        log (logger): logger for the messages produced by this method
        tags (list): a list of tag or test file names

    Raises:
        LaunchException: if there is a problem listing tests

    Returns:
        (list, list): a tuple of an avocado tag filter list and lists of tests

    """
    test_tags = []
    test_list = []

    # Check if fault injection is enabled ( 0 return status)
    log.debug("Checking for fault injection enablement via 'fault_status':")
    fault_tag = "-faults"
    fault_filter = f"--filter-by-tags={fault_tag}"
    try:
        run_local(log, ["fault_status"], check=True)
        log.debug("  Fault injection is enabled")
        faults_enabled = True
    except LaunchException:
        # Command failed or yielded a non-zero return status
        log.debug("  Fault injection is disabled")
        faults_enabled = False

    for tag in tags:
        if os.path.isfile(tag):
            # Assume an existing file is a test and just add it to the list
            test_list.append(tag)
            if not faults_enabled and fault_filter not in test_tags:
                test_tags.append(fault_filter)
        else:
            # Otherwise it is assumed that this is a tag
            if not faults_enabled:
                tag = ",".join((tag, fault_tag))
            test_tags.append(f"--filter-by-tags={tag}")

    # Update the list of tests with any test that match the specified tags.
    # Exclude any specified tests that do not match the specified tags.  If no
    # tags and no specific tests have been specified then all of the functional
    # tests will be added.
    if test_tags or not test_list:
        command = get_avocado_list_command()
        for test_tag in test_tags:
            command.append(str(test_tag))
        command.extend(test_list if test_list else ["./"])
        output = run_local(log, command, check=True)
        tagged_tests = re.findall(r"INSTRUMENTED\s+(.*):", output.stdout)
        test_list = [TestInfo(match) for match in set(tagged_tests)]

    return test_tags, test_list


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
        args.extra_yaml = replace_yaml_file(log, args.extra_yaml, args, yaml_dir)

    for test in test_list:
        test.yaml_file = replace_yaml_file(log, test.yaml_file, args, yaml_dir)

        # Display the modified yaml file variants with debug
        command = ["avocado", "variants", "--mux-yaml", test.yaml_file]
        if args.extra_yaml:
            command.append(args.extra_yaml)
        command.extend(["--summary", "3"])
        run_local(log, command, check=False)


def replace_yaml_file(log, yaml_file, args, yaml_dir):
    # pylint: disable=too-many-nested-blocks
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
        log.info("Creating copy: %s", yaml_file)
        with open(yaml_file, "w", encoding="utf-8") as yaml_buffer:
            yaml_buffer.write(yaml_data)

        # Optionally display a diff of the yaml file
        if args.verbose > 0:
            command = ["diff", "-y", orig_yaml_file, yaml_file]
            run_local(log, command, check=False)

    # Return the untouched or modified yaml file
    return yaml_file


def setup_test_directory(log, args, mode="all"):
    """Set up the common test directory on all hosts.

    Ensure the common test directory exists on each possible test node.

    Args:
        log (logger): logger for the messages produced by this method
        args (argparse.Namespace): command line arguments for this program
        mode (str, optional): setup mode. Defaults to "all".
            "rm"    = remove the directory
            "mkdir" = create the directory
            "chmod" = change the permissions of the directory (a+rw)
            "list"  = list the contents of the directory
            "all"  = execute all of the mode options

    Raises:
        LaunchException: if there is an error setting up the test directory

    """
    all_hosts = NodeSet(get_local_host())
    all_hosts.update(args.test_clients)
    all_hosts.update(args.test_servers)
    test_dir = os.environ["DAOS_TEST_LOG_DIR"]
    log.debug("Setting up '%s' on %s:", test_dir, all_hosts)
    if mode in ["all", "rm"]:
        run_remote(log, all_hosts, f"sudo rm -fr {test_dir}")
    if mode in ["all", "mkdir"]:
        run_remote(log, all_hosts, f"mkdir -p {test_dir}")
    if mode in ["all", "chmod"]:
        run_remote(log, all_hosts, f"chmod a+wr {test_dir}")
    if mode in ["all", "list"]:
        run_remote(log, all_hosts, f"ls -al {test_dir}")


def generate_certs(log):
    """Generate the certificates for the test.

    Args:
        log (logger): logger for the messages produced by this method

    Raises:
        LaunchException: if there is an error generating the certificates

    """
    daos_test_log_dir = os.environ["DAOS_TEST_LOG_DIR"]
    certs_dir = os.path.join(daos_test_log_dir, "daosCA")
    run_local(log, ["/usr/bin/rm", "-rf", certs_dir])
    run_local(log, ["../../../../lib64/daos/certgen/gen_certificates.sh", daos_test_log_dir])


def run_tests(log, test_list, tag_filter, args):
    # pylint: disable=too-many-branches
    """Run or display the test commands.

    Args:
        log (logger): logger for the messages produced by this method
        test_list (list): a list of TestInfo objects defining which tests to run
        tag_filter (list): the avocado tag filter command line argument
        args (argparse.Namespace): command line arguments for this program

    Returns:
        int: a bitwise-or of all the return codes of each 'avocado run' command

    """
    return_code = 0

    # Determine the location of the avocado logs for archiving or renaming
    avocado_logs_dir = get_avocado_logs_dir()
    log.debug("Avocado logs stored in %s", avocado_logs_dir)

    # Run each test for as many repetitions as requested
    for loop in range(1, args.repeat + 1):
        log.debug("-" * 80)
        log.info("Starting loop %s/%s", loop, args.repeat)

        skip_reason = None
        for test in test_list:
            # Prepare to run the test
            test_return_code = prepare_test(log, test, avocado_logs_dir, skip_reason, args)
            if test_return_code != 0:
                return_code |= test_return_code
                continue

            # Run the test
            test_return_code = run_test(log, test, tag_filter, args)
            if test_return_code != 0:
                collect_crash_files(log, avocado_logs_dir)
                return_code |= test_return_code
                continue

            # Post-process the test
            test_return_code = process_test(log, test, loop, avocado_logs_dir, args)
            if test_return_code != 0:
                return_code |= test_return_code
                continue

        if args.jenkinslog:
            # Archive bullseye coverage logs
            hosts = NodeSet(get_local_host())
            hosts.add(args.test_servers)
            return_code |= archive_files(
                log,
                "bullseye coverage logs",
                os.path.join(avocado_logs_dir, "bullseye_coverage_logs"),
                hosts,
                "/tmp/test.cov*",
                args)

        # If the test failed and the user requested that testing should
        # stop after the first failure, then we should break out of the
        # loop and not re-run the tests.
        if return_code != 0 and args.failfast:
            break

    return return_code


def prepare_test(log, test, avocado_logs_dir, skip_reason, args):
    """Prepare the test to be executed.

    Args:
        log (logger): logger for the messages produced by this method
        test (TestInfo): the test script/yaml file
        avocado_logs_dir (str): path to the avocado log files.
        skip_reason (str): message indicating the reason to skip the test
        args (argparse.Namespace): command line arguments for this program

    Returns:
        int: a return code which will be non-zero if there were an issues preparing this test

    """
    if skip_reason is not None:
        # An error was detected running clean_logs for a previous test. As this is typically an
        # indication of a communication issue with one of the hosts, do not attempt to run
        # subsequent tests.
        if not report_skipped_test(log, str(test), avocado_logs_dir, skip_reason):
            return 64

    if not isinstance(test.yaml_file, str):
        # The test was not run due to an error replacing host placeholders in the yaml file.
        # Treat this like a failed avocado command.
        reason = "error replacing yaml file placeholders"
        if not report_skipped_test(log, str(test), avocado_logs_dir, reason):
            return 64
        return 4

    # Optionally clean the log files before running this test on the
    # servers and clients specified for this test
    if args.clean:
        if not clean_logs(log, test.yaml_file, args):
            # Report errors for this skipped test
            skip_reason = (
                "host communication error attempting to clean out leftover logs from a previous "
                "test run prior to running this test")
            if not report_skipped_test(log, str(test), avocado_logs_dir, skip_reason):
                return 64
            return 128

    return 0


def run_test(log, test, tag_filter, args):
    """Run the specified test.

    Args:
        log (logger): logger for the messages produced by this method
        test (TestInfo): the test script/yaml file
        tag_filter (list): the avocado tag filter command line argument
        args (argparse.Namespace): command line arguments for this program

    Returns:
        int: the return code from the 'avocado run' command

    """
    command = get_avocado_run_command(test, tag_filter, args.sparse, args.failfast, args.extra_yaml)
    start_time = int(time.time())

    try:
        return_code = run_local(log, command, False).returncode

    except LaunchException as error:
        log.error("Error running %s: %s", str(test), str(error))
        return_code = 1

    end_time = int(time.time())
    log.info("Total test time: %ss", end_time - start_time)
    return return_code


def process_test(log, test, loop, avocado_logs_dir, args):
    """Process the test result after the test has completed.

    Args:
        log (logger): logger for the messages produced by this method
        test (TestInfo): the test script/yaml file
        loop (int): the loop counter for running this test
        avocado_logs_dir (str): path to the avocado log files.
        args (argparse.Namespace): command line arguments for this program

    Returns:
        int: a return code which will be non-zero if there were an issues processing this test

    """
    return_code = 0

    # Get a logger for this test
    test_log = logging.getLogger(".".join([__name__, get_test_category(str(test)), str(loop)]))
    log.debug("Test post-processing log: %s", test_log)
    add_file_handler(test_log, os.path.join(avocado_logs_dir, "latest", "launch.log"))

    # Stop any agents or servers running via systemd
    if not args.disable_stop_daos:
        return_code |= stop_daos_agent_services(test_log, test, args)
        return_code |= stop_daos_server_service(test_log, test, args)
        return_code |= reset_server_storage(test_log, test, args)

    # Optionally store all of the server and client config files
    # and archive remote logs and report big log files, if any.
    if args.archive:
        test_hosts = get_hosts_from_yaml(test_log, test.yaml_file, args)
        test_servers = get_hosts_from_yaml(
            test_log, test.yaml_file, args, YAML_KEYS["test_servers"])
        test_clients = get_hosts_from_yaml(
            test_log, test.yaml_file, args, YAML_KEYS["test_clients"])
        test_log_dir = os.environ.get("DAOS_TEST_LOG_DIR", DEFAULT_DAOS_TEST_LOG_DIR)

        # Archive local config files
        return_code |= archive_files(
            test_log,
            "local configuration files",
            os.path.join(avocado_logs_dir, "latest", "daos_configs"),
            NodeSet(get_local_host()),
            os.path.join(
                get_temporary_directory(args, get_build_environment(args)["PREFIX"]),
                "*_*_*.yaml"),
            args)

        # Archive remote server configuration files
        return_code |= archive_files(
            test_log,
            "remote server config files",
            os.path.join(avocado_logs_dir, "latest", "daos_configs"),
            test_servers,
            os.path.join(os.sep, "etc", "daos", "daos_server*.yml"),
            args)

        # Archive remote client configuration files
        return_code |= archive_files(
            test_log,
            "remote client config files",
            os.path.join(avocado_logs_dir, "latest", "daos_configs"),
            test_clients,
            " ".join(
                [os.path.join(os.sep, "etc", "daos", "daos_agent*.yml"),
                    os.path.join(os.sep, "etc", "daos", "daos_control*.yml")]
            ),
            args)

        # Archive remote daos log files - use an extended timeout for potentially large logs
        return_code |= archive_files(
            test_log,
            "daos log files",
            os.path.join(avocado_logs_dir, "latest", "daos_logs"),
            test_hosts,
            f"{test_log_dir}/*.log*",
            args,
            avocado_logs_dir,
            get_test_category(str(test)),
            1800)

        # Archive remote ULTs stacks dump files
        return_code |= archive_files(
            test_log,
            "ULTs stacks dump files",
            os.path.join(avocado_logs_dir, "latest", "daos_dumps"),
            test_servers,
            "/tmp/daos_dump*.txt*",
            args,
            avocado_logs_dir,
            get_test_category(str(test)))

        # Archive remote cart log files
        return_code |= archive_files(
            test_log,
            "cart log files",
            os.path.join(avocado_logs_dir, "latest", "cart_logs"),
            test_hosts,
            os.path.join(test_log_dir, "*", "*log*"),
            args,
            avocado_logs_dir,
            get_test_category(str(test)))

        # Compress any log file that haven't been remotely compressed.
        compress_log_files(test_log, avocado_logs_dir, args)

        # Archive valgrind log files from shared dir
        valgrind_logs_dir = os.environ.get("DAOS_TEST_SHARED_DIR", os.environ['HOME'])
        return_code |= archive_files(
            test_log,
            "valgrind log files",
            os.path.join(avocado_logs_dir, "latest", "valgrind_logs"),
            NodeSet(test_hosts[0]),
            os.path.join(valgrind_logs_dir, "valgrind*"),
            args,
            avocado_logs_dir)

    # Optionally rename the test results directory for this test
    if args.rename:
        return_code |= rename_logs(test_log, avocado_logs_dir, str(test), loop, args)

    # Optionally process core files
    if args.process_cores:
        try:
            if not process_the_cores(test_log, avocado_logs_dir, test.yaml_file, args):
                return_code |= 256
        except Exception as error:  # pylint: disable=broad-except
            test_log.error("Error: unhandled exception processing core files: %s", str(error))
            return_code |= 256

    return return_code


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


def find_yaml_hosts(log, test_yaml):
    """Find the all the host values in the specified yaml file.

    Args:
        log (logger): logger for the messages produced by this method
        test_yaml (str): test yaml file

    Returns:
        dict: a dictionary of each host key and its host values

    """
    return find_values(
        get_yaml_data(log, test_yaml),
        [YAML_KEYS["test_servers"], YAML_KEYS["test_clients"]])


def get_hosts_from_yaml(log, test_yaml, args, key_match=None):
    """Extract the list of hosts from the test yaml file.

    This host will be included in the list if no clients are explicitly called
    out in the test's yaml file.

    Args:
        log (logger): logger for the messages produced by this method
        test_yaml (str): test yaml file
        args (argparse.Namespace): command line arguments for this program
        key_match (str, optional): test yaml key used to filter which hosts to
            find.  Defaults to None which will match all keys.

    Returns:
        NodeSet: hosts specified in the test's yaml file

    """
    log.debug("Extracting hosts from %s - matching key '%s'", test_yaml, key_match)
    local_host = NodeSet(get_local_host())
    yaml_hosts = NodeSet()
    if args.include_localhost and key_match != YAML_KEYS["test_servers"]:
        yaml_hosts.add(local_host)
    found_client_key = False
    for key, value in list(find_yaml_hosts(log, test_yaml).items()):
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


def clean_logs(log, test_yaml, args):
    """Remove the test log files on each test host.

    Args:
        log (logger): logger for the messages produced by this method
        test_yaml (str): yaml file containing host names
        args (argparse.Namespace): command line arguments for this program
    """
    # Remove any log files and ABT info/stack dumps from the DAOS_TEST_LOG_DIR directory
    logs_dir = os.environ.get("DAOS_TEST_LOG_DIR", DEFAULT_DAOS_TEST_LOG_DIR)
    test_hosts = get_hosts_from_yaml(log, test_yaml, args)
    command = ["sudo", "rm", "-rf", os.path.join(logs_dir, "*.log*"), "/tmp/daos_dump*.txt*"]
    log.debug("-" * 80)
    log.info("Cleaning logs on %s", test_hosts)
    result = run_remote(log, test_hosts, " ".join(command))
    if not result.passed:
        log.error("Error cleaning logs, aborting")
    return result.passed


def collect_crash_files(log, avocado_logs_dir):
    """Move any avocado crash files into job-results/latest/crashes.

    Args:
        log (logger): logger for the messages produced by this method
        avocado_logs_dir (str): path to the avocado log files.

    Raises:
        RuntimeError: if there was an error creating directories for the crash files
    """
    data_dir = avocado_logs_dir.replace("job-results", "data")
    crash_dir = os.path.join(data_dir, "crashes")
    if os.path.isdir(crash_dir):
        crash_files = [
            os.path.join(crash_dir, crash_file)
            for crash_file in os.listdir(crash_dir)
            if os.path.isfile(os.path.join(crash_dir, crash_file))]
        if crash_files:
            latest_dir = os.path.join(avocado_logs_dir, "latest")
            latest_crash_dir = os.path.join(latest_dir, "crashes")
            try:
                run_local(log, ["mkdir", latest_crash_dir], check=True)
                for crash_file in crash_files:
                    run_local(log, ["mv", crash_file, latest_crash_dir], check=True)
            except LaunchException:
                pass
        else:
            log.debug("No avocado crash files found in %s", crash_dir)


def get_remote_file_command():
    """Get path to get_remote_files.sh script."""
    return os.path.join(os.path.abspath(os.getcwd()), "get_remote_files.sh")


def compress_log_files(log, avocado_logs_dir, args):
    """Compress log files.

    Args:
        log (logger): logger for the messages produced by this method
        avocado_logs_dir (str): path to the avocado log files
    """
    log.debug("-" * 80)
    log.info("Compressing files on %s", get_local_host())
    logs_dir = os.path.join(avocado_logs_dir, "latest", "daos_logs", "*.log*")
    command = [
        get_remote_file_command(), "-z", "-x", f"-f {logs_dir}"]
    if args.verbose > 1:
        command.append("-v")
    run_local(log, command, check=False)


def archive_files(log, description, destination, hosts, source_files, args,
                  avocado_logs_dir=None, test_name=None, timeout=900):
    """Archive all of the remote files to a local directory.

    Args:
        log (logger): logger for the messages produced by this method
        description (str): string identifying the archiving operation
        destination (str): path in which to archive files
        hosts (NodeSet): hosts from which to archive files
        source_files (str): remote files to archive
        args (argparse.Namespace): command line arguments for this program
        avocado_logs_dir (optional, str): path to the avocado log files.
            Required for checking for large log files - see 'test_name'.
            Defaults to None.
        test_name (optional, str): current running testname. If specified the
            cart_logtest.py will be run against each log file and the size of
            each log file will be checked against the threshold (if enabled).
            Defaults to None.
        timeout (int, optional): number of seconds to wait for the archiving
            operation to complete. Defaults to 900 seconds.

    Returns:
        int: status of archiving the files

    """
    status = 0
    if hosts:
        log.debug("-" * 80)
        log.info("Archiving %s from %s in %s", description, hosts, destination)

        # Create the destination directory
        if not os.path.exists(destination):
            os.makedirs(destination, exist_ok=True)

        # Display available disk space prior to copy.  Allow commands to fail
        # w/o exiting this program.  Any disk space issues preventing the
        # creation of a directory will be caught in the archiving of the source
        # files.
        display_disk_space(log, destination)

        command = [
            get_remote_file_command(),
            "-z",
            f"-a \"{get_local_host()}:{destination}\"",
            f"-f \"{source_files}\"",
        ]
        if test_name is not None:
            command.append("-c")
        if args.logs_threshold:
            command.append(f"-t \"{args.logs_threshold}\"")
        if args.verbose > 1:
            command.append("-v")
        result = run_remote(log, hosts, " ".join(command), timeout)

        # Determine if the command completed successfully across all the hosts
        if not result.passed:
            log.error("Error: archiving files for %s to %s", test_name, destination)
            status |= 16
        if test_name is not None and args.logs_threshold:
            if not check_big_files(log, avocado_logs_dir, result, test_name, args):
                status |= 32

    return status


def rename_logs(log, avocado_logs_dir, test_file, loop, args):
    """Append the test name to its avocado job-results directory name.

    Args:
        log (logger): logger for the messages produced by this method
        avocado_logs_dir (str): avocado job-results directory
        test_file (str): the test python file
        loop (int): test execution loop count
        args (argparse.Namespace): command line arguments for this program

    Returns:
        int: status of renaming the avocado job-results directory name

    """
    status = 0
    test_name = get_test_category(test_file)
    test_logs_lnk = os.path.join(avocado_logs_dir, "latest")
    test_logs_dir = os.path.realpath(test_logs_lnk)

    log.debug("-" * 80)
    log.info("Renaming the avocado job-results directory")

    if args.jenkinslog:
        if args.repeat > 1:
            # When repeating tests ensure Jenkins-style avocado log directories
            # are unique by including the loop count in the path
            new_test_logs_dir = os.path.join(
                avocado_logs_dir, test_file, str(loop))
        else:
            new_test_logs_dir = os.path.join(avocado_logs_dir, test_file)
        try:
            os.makedirs(new_test_logs_dir)
        except OSError as error:
            log.error("Error mkdir %s: %s", new_test_logs_dir, error)
            status |= 1024
    else:
        new_test_logs_dir = "-".join([test_logs_dir, test_name])

    try:
        os.rename(test_logs_dir, new_test_logs_dir)
        os.remove(test_logs_lnk)
        os.symlink(new_test_logs_dir, test_logs_lnk)
        log.debug("Renamed %s to %s", test_logs_dir, new_test_logs_dir)
    except OSError as error:
        log.error("Error renaming %s to %s: %s", test_logs_dir, new_test_logs_dir, str(error))

    if args.jenkinslog:
        xml_file = os.path.join(new_test_logs_dir, "results.xml")
        try:
            with open(xml_file, encoding="utf-8") as xml_buffer:
                xml_data = xml_buffer.read()
        except OSError as error:
            log.error("Error reading %s : %s", xml_file, str(error))
            status |= 1024
            return status

        # Save it for the Launchable [de-]mangle
        org_xml_data = xml_data

        # First, mangle the in-place file for Jenkins to consume
        test_dir = os.path.split(os.path.dirname(test_file))[-1]
        org_class = "classname=\""
        new_class = f"{org_class}FTEST_{test_dir}."
        xml_data = re.sub(org_class, new_class, xml_data)

        try:
            with open(xml_file, "w", encoding="utf-8") as xml_buffer:
                xml_buffer.write(xml_data)
        except OSError as error:
            log.error("Error writing %s: %s", xml_file, str(error))
            status |= 1024

        # Now mangle (or rather unmangle back to canonical xunit1 format)
        # another copy for Launchable
        xml_data = org_xml_data
        org_name = r'(name=")\d+-\.\/.+.(test_[^;]+);[^"]+(")'
        new_name = rf'\1\2\3 file="{test_file}"'
        xml_data = re.sub(org_name, new_name, xml_data)
        xml_file = xml_file[0:-11] + "xunit1_results.xml"

        try:
            with open(xml_file, "w", encoding="utf-8") as xml_buffer:
                xml_buffer.write(xml_data)
        except OSError as error:
            log.error("Error writing %s: %s", xml_file, str(error))
            status |= 1024
    return status


def check_big_files(log, avocado_logs_dir, result, test_name, args):
    """Check the contents of the task object, tag big files, create junit xml.

    Args:
        log (logger): logger for the messages produced by this method
        avocado_logs_dir (str): path to the avocado log files.
        result (RemoteCommandResult): a RemoteCommandResult object containing the command result
        test_name (str): current running testname
        args (argparse.Namespace): command line arguments for this program

    Returns:
        bool: True if no errors occurred checking and creating junit file.
            False, otherwise.

    """
    status = True
    hosts = NodeSet()
    cdata = []
    for data in result.output:
        big_files = re.findall(r"Y:\s([0-9]+)", "\n".join(data.stdout))
        if big_files:
            hosts.update(data.hosts)
            cdata.append(
                f"The following log files on {str(data.hosts)} exceeded the "
                f"{args.logs_threshold} threshold")
            cdata.extend([f"  {big_file}" for big_file in big_files])
    if cdata:
        log.debug("One or more log files found exceeding %s", args.logs_threshold)
        destination = os.path.join(avocado_logs_dir, "latest")
        message = f"Log size has exceed threshold for this test on: {str(hosts)}"
        status = create_results_xml(log, message, test_name, "\n".join(cdata), destination)
    else:
        log.debug("No log files found exceeding %s", args.logs_threshold)

    return status


def report_skipped_test(log, test_file, avocado_logs_dir, reason):
    """Report an error for the skipped test.

    Args:
        log (logger): logger for the messages produced by this method
        test_file (str): the test python file
        avocado_logs_dir (str): avocado job-results directory
        reason (str): test skip reason

    Returns:
        bool: status of writing to junit file

    """
    message = f"The {test_file} test was skipped due to {reason}"
    log.debug(message)

    # Generate a fake avocado results.xml file to report the skipped test.
    # This file currently requires being placed in a job-* subdirectory.
    test_name = get_test_category(test_file)
    time_stamp = datetime.now().strftime("%Y-%m-%dT%H.%M")
    destination = os.path.join(avocado_logs_dir, f"job-{time_stamp}-da03911-{test_name}")
    try:
        os.makedirs(destination)
    except (OSError, FileExistsError) as error:
        log.debug("Warning: Continuing after failing to create %s: %s", destination, error)

    return create_results_xml(
        log, message, test_name, "See launch.py command output for more details", destination)


def create_results_xml(log, message, testname, output, destination):
    """Create JUnit xml file.

    Args:
        log (logger): logger for the messages produced by this method
        message (str): error summary message
        testname (str): name of test
        output (dict): result of the command.
        destination (str): directory where junit xml will be created

    Returns:
        bool: status of writing to junit file

    """
    status = True

    # Define the test suite
    testsuite_attributes = {
        "name": str(testname),
        "errors": "1",
        "failures": "0",
        "skipped": "0",
        "test": "1",
        "time": "0.0",
    }
    testsuite = ET.Element("testsuite", testsuite_attributes)

    # Define the test case error
    testcase_attributes = {"classname": testname, "name": "framework_results",
                           "time": "0.0"}
    testcase = ET.SubElement(testsuite, "testcase", testcase_attributes)
    ET.SubElement(testcase, "error", {"message": message})
    system_out = ET.SubElement(testcase, "system-out")
    system_out.text = output

    # Get xml as string and write it to a file
    rough_xml = ET.tostring(testsuite, "utf-8")
    junit_xml = minidom.parseString(rough_xml)
    results_xml = os.path.join(destination, "framework_results.xml")
    log.debug("Generating junit xml file %s ...", results_xml)
    try:
        with open(results_xml, "w", encoding="utf-8") as xml_buffer:
            xml_buffer.write(junit_xml.toprettyxml())
    except IOError as error:
        log.debug("Failed to create xml file: %s", str(error))
        status = False
    return status


USE_DEBUGINFO_INSTALL = True


def resolve_debuginfo(log, pkg):
    """Return the debuginfo package for a given package name.

    Args:
        log (logger): logger for the messages produced by this method
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
            log, ["rpm", "-q", "--qf", "%{name} %{version} %{release} %{epoch}", pkg], check=False)
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
        log.debug("Package %s not installed, skipping debuginfo", pkg)

    return package_info


def is_el(distro):
    """Return True if a distro is an EL."""
    return [d for d in ["almalinux", "rocky", "centos", "rhel"] if d in distro.name.lower()]


def install_debuginfos(log):
    """Install debuginfo packages.

    NOTE: This does assume that the same daos packages that are installed
        on the nodes that could have caused the core dump are installed
        on this node also.

    Args:
        log (logger): logger for the messages produced by this method

    Raises:
        LaunchException: if there is an error installing debuginfo packages

    """
    # The distro_utils.py file is installed in the util sub-directory relative to this file location
    sys.path.append(os.path.join(os.getcwd(), "util"))
    from distro_utils import detect         # pylint: disable=import-outside-toplevel

    distro_info = detect()
    install_pkgs = [{'name': 'gdb'}]
    if is_el(distro_info):
        install_pkgs.append({'name': 'python3-debuginfo'})

    cmds = []

    # -debuginfo packages that don't get installed with debuginfo-install
    for pkg in ['systemd', 'ndctl', 'mercury', 'hdf5', 'argobots', 'libfabric',
                'hdf5-vol-daos', 'hdf5-vol-daos-mpich', 'hdf5-vol-daos-mpich-tests',
                'hdf5-vol-daos-openmpi', 'hdf5-vol-daos-openmpi-tests', 'ior']:
        try:
            debug_pkg = resolve_debuginfo(log, pkg)
        except LaunchException as error:
            log.error("Failed trying to install_debuginfos(): %s", str(error))
            raise

        if debug_pkg and debug_pkg not in install_pkgs:
            install_pkgs.append(debug_pkg)

    # remove any "source tree" test hackery that might interfere with RPM installation
    path = os.path.join(os.path.sep, "usr", "share", "spdk", "include")
    if os.path.islink(path):
        cmds.append(["sudo", "rm", "-f", path])

    if USE_DEBUGINFO_INSTALL:
        dnf_args = ["--exclude", "ompi-debuginfo"]
        if os.getenv("TEST_RPMS", 'false') == 'true':
            if "suse" in distro_info.name.lower():
                dnf_args.extend(["libpmemobj1", "python3", "openmpi3"])
            elif "centos" in distro_info.name.lower() and \
                 distro_info.version == "7":
                dnf_args.extend(["--enablerepo=*-debuginfo", "--exclude",
                                 "nvml-debuginfo", "libpmemobj",
                                 "python36", "openmpi3", "gcc"])
            elif is_el(distro_info) and distro_info.version == "8":
                dnf_args.extend(["--enablerepo=*-debuginfo", "libpmemobj",
                                 "python3", "openmpi", "gcc"])
            else:
                raise LaunchException(f"install_debuginfos(): Unsupported distro: {distro_info}")
            cmds.append(["sudo", "dnf", "-y", "install"] + dnf_args)
        output = run_local(log, ["rpm", "-q", "--qf", "%{evr}", "daos"], check=False)
        rpm_version = output.stdout
        cmds.append(
            ["sudo", "dnf", "debuginfo-install", "-y"] + dnf_args
            + ["daos-client-" + rpm_version,
               "daos-server-" + rpm_version,
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
    if is_el(distro_info) or "suse" in distro_info.name.lower():
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
            run_local(log, cmd, check=True)
        except LaunchException:
            # got an error, so abort this list of commands and re-run
            # it with a dnf clean, makecache first
            retry = True
            break
    if retry:
        log.debug("Going to refresh caches and try again")
        cmd_prefix = ["sudo", "dnf"]
        if is_el(distro_info) or "suse" in distro_info.name.lower():
            cmd_prefix.append("--enablerepo=*debug*")
        cmds.insert(0, cmd_prefix + ["clean", "all"])
        cmds.insert(1, cmd_prefix + ["makecache"])
        for cmd in cmds:
            try:
                run_local(log, cmd)
            except LaunchException:
                break


def process_the_cores(log, avocado_logs_dir, test_yaml, args):
    """Copy all of the host test log files to the avocado results directory.

    Args:
        log (logger): logger for the messages produced by this method
        avocado_logs_dir (str): location of the avocado job logs
        test_yaml (str): yaml file containing host names
        args (argparse.Namespace): command line arguments for this program

    Returns:
        bool: True if everything was done as expected, False if there were
              any issues processing core files

    """
    import fnmatch  # pylint: disable=import-outside-toplevel

    return_status = True
    test_hosts = get_hosts_from_yaml(log, test_yaml, args)
    daos_cores_dir = os.path.join(avocado_logs_dir, "latest", "stacktraces")

    # Create a subdirectory in the avocado logs directory for this test
    log.debug("-" * 80)
    log.debug("Processing cores from %s in %s", test_hosts, daos_cores_dir)
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
    result = run_remote(log, test_hosts, "; ".join(commands), timeout=1800)
    if not result.passed:
        # we might have still gotten some core files, so don't return here
        # but save a False return status for later
        return_status = False

    cores = os.listdir(daos_cores_dir)

    if not cores:
        return True

    try:
        install_debuginfos(log)
    except LaunchException as error:
        log.debug(error)
        log.debug("Removing core files to avoid archiving them")
        for corefile in cores:
            os.remove(os.path.join(daos_cores_dir, corefile))
        return False

    for corefile in cores:
        if not fnmatch.fnmatch(corefile, 'core.*[0-9]'):
            continue
        corefile_fqpn = os.path.join(daos_cores_dir, corefile)
        run_local(log, ['ls', '-l', corefile_fqpn])
        # can't use the file python magic binding here due to:
        # https://bugs.astron.com/view.php?id=225, fixed in:
        # https://github.com/file/file/commit/6faf2eba2b8c65fbac7acd36602500d757614d2f
        # but not available to us until that is in a released version
        # revert the commit this comment is in to see use python magic instead
        try:
            gdb_output = run_local(
                log, ["gdb", "-c", corefile_fqpn, "-ex", "info proc exe", "-ex", "quit"])

            last_line = gdb_output.stdout[-1]
            cmd = last_line[7:-1]
            # assume there are no arguments on cmd
            find_char = "'"
            if cmd.find(" ") > -1:
                # there are arguments on cmd
                find_char = " "
            exe_name = cmd[0:cmd.find(find_char)]
        except RuntimeError:
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
                output = run_local(log, cmd, check=False)
                with open(stack_trace_file, "w", encoding="utf-8") as stack_trace:
                    stack_trace.writelines(output.stdout)
            except IOError as error:
                log.debug("Error writing %s: %s", stack_trace_file, error)
                return_status = False
            except LaunchException as error:
                log.debug("Error creating %s: %s", stack_trace_file, error)
                return_status = False
        else:
            log.debug(
                "Unable to determine executable name from gdb output: '%s'\n"
                "Not creating stacktrace", gdb_output)
            return_status = False
        log.debug("Removing %s", corefile_fqpn)
        os.unlink(corefile_fqpn)

    return return_status


def get_test_category(test_file):
    """Get a category for the specified test using its path and name.

    Args:
        test_file (str): the test python file

    Returns:
        str: concatenation of the test path and base filename joined by dashes

    """
    file_parts = os.path.split(test_file)
    return "-".join([os.path.splitext(os.path.basename(part))[0] for part in file_parts])


def stop_daos_agent_services(log, test, args):
    """Stop any daos_agent.service running on the hosts running servers.

    Args:
        log (logger): logger for the messages produced by this method
        test (TestInfo): the test script/yaml file
        args (argparse.Namespace): command line arguments for this program

    Returns:
        int: status code: 0 = success, 512 = failure

    """
    service = "daos_agent.service"
    hosts = get_hosts_from_yaml(log, test.yaml_file, args, YAML_KEYS["test_clients"])
    hosts.add(NodeSet(get_local_host()))
    log.debug("-" * 80)
    log.debug("Verifying %s on %s after running '%s'", service, hosts, str(test))
    return stop_service(log, hosts, service)


def stop_daos_server_service(log, test, args):
    """Stop any daos_server.service running on the hosts running servers.

    Args:
        log (logger): logger for the messages produced by this method
        test (TestInfo): the test script/yaml file
        args (argparse.Namespace): command line arguments for this program

    Returns:
        int: status code: 0 = success, 512 = failure

    """
    service = "daos_server.service"
    hosts = get_hosts_from_yaml(log, test.yaml_file, args, YAML_KEYS["test_servers"])
    log.debug("-" * 80)
    log.debug("Verifying %s on %s after running '%s'", service, hosts, str(test))
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
        log.debug(" %s: %s", data.hosts, "\n".join(data.stdout))
        for key, state_list in status_states.items():
            for line in data.stdout:
                if line in state_list:
                    status[key].add(data.hosts)
                    break
    return status


def reset_server_storage(log, test, args):
    """Reset the server storage for the hosts that ran servers in the test.

    This is a workaround to enable binding devices back to nvme or vfio-pci after they are unbound
    from vfio-pci to nvme.  This should resolve the "NVMe not found" error seen when attempting to
    start daos engines in the test.

    Args:
        log (logger): logger for the messages produced by this method
        test (TestInfo): the test script/yaml file
        args (argparse.Namespace): command line arguments for this program

    Returns:
        int: status code: 0 = success, 512 = failure

    """
    hosts = get_hosts_from_yaml(log, test.yaml_file, args, YAML_KEYS["test_servers"])
    log.debug("-" * 80)
    if hosts:
        commands = [
            "if lspci | grep -i nvme",
            "then daos_server storage prepare -n --reset && "
            "sudo rmmod vfio_pci && sudo modprobe vfio_pci",
            "fi"]
        log.info("Resetting server storage on %s after running '%s'", hosts, str(test))
        result = run_remote(log, hosts, f"bash -c '{';'.join(commands)}'", timeout=600)
        if not result.passed:
            log.debug("Ignoring any errors from these workaround commands")
    else:
        log.debug(
            "Skipping resetting server storage after running '%s' - no server hosts", str(test))
    return 0


def main():
    """Launch DAOS functional tests."""
    # Setup logging
    # log_format = "%(asctime)s - %(levelname)-5s - %(funcName)-15s: %(message)s"
    # log_format = "%(message)s"
    # console = logging.StreamHandler()
    # console.setLevel(logging.DEBUG)
    # console.setFormatter(logging.Formatter(log_format))
    # logging.basicConfig(
    #     format=log_format, datefmt=r"%Y/%m/%d %I:%M:%S", level=logging.DEBUG, handlers=[console])
    # log = logging.getLogger(__name__)

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
        action="store",
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

    # Setup the logger based upon the mode
    log_level = logging.INFO if args.mode == "ci" else logging.DEBUG
    log_format = "%(message)s"
    console = logging.StreamHandler()
    console.setFormatter(logging.Formatter(log_format))
    console.setLevel(log_level)
    logging.basicConfig(
        format=log_format, datefmt=r"%Y/%m/%d %I:%M:%S", level=logging.DEBUG, handlers=[console])
    log = logging.getLogger(__name__)
    log.info("-" * 80)
    log.info("DAOS functional test launcher")
    log.info("-" * 80)
    MAJOR, MINOR = get_avocado_version()    # pylint: disable=invalid-name
    log.info("Running with Avocado %s.%s", MAJOR, MINOR)

    # Create an avocado job-result for the launch.py execution using the stage name
    default_stage_name = "Functional Stage Unknown" if args.mode == "ci" else "Manual"
    stage_name = os.environ.get("STAGE_NAME", default_stage_name)
    ftest_name = "_".join(stage_name.replace("Functional", "FTEST").split())
    log_file = os.path.join(get_avocado_directory(ftest_name), "launch.log")
    rename_log = os.path.exists(log_file)
    if rename_log:
        os.rename(log_file, ".".join([log_file, "old"]))
    add_file_handler(log, log_file)
    log.info("Log file: %s", log_file)
    if rename_log:
        log.info("  Renaming existing log file to %s", ".".join([log_file, "old"]))

    if args.mode == "ci":
        # Enforce CI mode default arguments
        args.archive = True
        args.clean = True
        args.include_localhost = True
        args.jenkinslog = True
        args.process_cores = True
        args.rename = True
        args.sparse = True
        if not args.logs_threshold:
            args.logs_threshold = "1G"

    log.debug("Arguments:")
    for key in sorted(args.__dict__.keys()):
        log.debug("  %s = %s", key, getattr(args, key))

    # Convert host specifications into NodeSets
    args.test_servers = NodeSet(args.test_servers)
    args.test_clients = NodeSet(args.test_clients)

    # A list of server hosts is required
    if not args.test_servers and not args.list:
        log.error("Error: Missing a required '--test_servers' argument.")
        sys.exit(1)

    # Setup the user environment
    try:
        set_test_environment(log, args)
    except LaunchException:
        log.exception("Error setting test environment")
        sys.exit(1)

    # Auto-detect nvme test yaml replacement values if requested
    if args.nvme and args.nvme.startswith("auto") and not args.list:
        try:
            args.nvme = get_device_replacement(log, args)
        except LaunchException:
            log.exception("Error auto-detecting NVMe test yaml file replacement values")
            sys.exit(1)
    elif args.nvme and args.nvme.startswith("vmd:"):
        args.nvme = args.nvme.replace("vmd:", "")

    # Process the tags argument to determine which tests to run
    try:
        tag_filter, test_list = get_test_list(log, args.tags)
    except LaunchException:
        log.exception("Error detecting tests that match tags: %s", args.tags)
        sys.exit(1)

    # Verify at least one test was requested
    if not test_list:
        log.error("Error: No tests or tags found via %s", args.tags)
        sys.exit(1)

    # Display a list of the tests matching the tags
    log.info("Detected tests:")
    for test in test_list:
        log.info("  %s", test)
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

    # Create a dictionary of test and their yaml files
    setup_test_files(log, test_list, args, yaml_dir)
    if args.modify:
        sys.exit(0)

    # Setup (clean/create/list) the common test directory
    setup_test_directory(log, args)

    # Generate certificate files
    generate_certs(log)

    # Run all the tests
    status = run_tests(log, test_list, tag_filter, args)

    # Process the avocado run return codes and only treat job and command
    # failures as errors.
    ret_code = 0
    if status == 0:
        log.info("All avocado tests passed!")
    else:
        if status & 1 == 1:
            log.info("Detected one or more avocado test failures!")
            if args.mode == 'manual':
                ret_code = 1
        if status & 8 == 8:
            log.info("Detected one or more interrupted avocado jobs!")
        if status & 2 == 2:
            log.info("ERROR: Detected one or more avocado job failures!")
            ret_code = 1
        if status & 4 == 4:
            log.info("ERROR: Detected one or more failed avocado commands!")
            ret_code = 1
        if status & 16 == 16:
            log.info("ERROR: Detected one or more tests that failed archiving!")
            ret_code = 1
        if status & 32 == 32:
            log.info("ERROR: Detected one or more tests with unreported big logs!")
            ret_code = 1
        if status & 64 == 64:
            log.info("ERROR: Failed to create a junit xml test error file!")
        if status & 128 == 128:
            log.info("ERROR: Failed to clean logs in preparation for test run!")
            ret_code = 1
        if status & 256 == 256:
            log.info("ERROR: Detected one or more tests with a failure processing core files!")
            ret_code = 1
        if status & 512 == 512:
            log.info("ERROR: Detected stopping daos_server.service after one or more tests!")
            ret_code = 1
        if status & 1024 == 1024:
            log.info(
                "ERROR: Detected one or more failures in renaming logs and results for Jenkins!")
            ret_code = 1
    if args.mode == 'ci':
        ret_code = 0
    sys.exit(ret_code)


if __name__ == "__main__":
    main()
