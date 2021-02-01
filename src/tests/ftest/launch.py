#!/usr/bin/python2 -u
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines
from __future__ import print_function

from argparse import ArgumentParser, RawDescriptionHelpFormatter
from datetime import datetime
import json
import os
import re
import socket
import subprocess
from sys import version_info
import time
import yaml
import errno
import xml.etree.ElementTree as ET
from xml.dom import minidom

from ClusterShell.NodeSet import NodeSet
from ClusterShell.Task import task_self

try:
    # For python versions >= 3.2
    from tempfile import TemporaryDirectory

except ImportError:
    # Basic implementation of TemporaryDirectory for python versions < 3.2
    from tempfile import mkdtemp
    from shutil import rmtree

    class TemporaryDirectory(object):
        # pylint: disable=too-few-public-methods
        """Create a temporary directory.

        When the last reference of this object goes out of scope the directory
        and its contents are removed.
        """

        def __init__(self):
            """Initialize a TemporaryDirectory object."""
            self.name = mkdtemp()

        def __del__(self):
            """Destroy a TemporaryDirectory object."""
            rmtree(self.name)

DEFAULT_DAOS_TEST_LOG_DIR = "/var/tmp/daos_testing"
YAML_KEYS = {
    "test_servers": "test_servers",
    "test_clients": "test_clients",
    "bdev_list": "nvme",
}
YAML_KEY_ORDER = ("test_servers", "test_clients", "bdev_list")


def display(args, message):
    """Display the message if verbosity is set.

    Args:
        args (argparse.Namespace): command line arguments for this program
        message (str): message to display if verbosity is set
    """
    if args.verbose:
        print(message)


def display_disk_space(path):
    """Display disk space of provided path destination.

    Args:
        path (str): path to directory to print disk space for.
    """
    print("Current disk space usage of {}".format(path))
    print(get_output(["df", "-h", path]))


def get_build_environment(args):
    """Obtain DAOS build environment variables from the .build_vars.json file.

    Returns:
        dict: a dictionary of DAOS build environment variable names and values

    """
    build_vars_file = os.path.join(
        os.path.dirname(os.path.realpath(__file__)),
        "../../.build_vars.json")
    try:
        with open(build_vars_file) as vars_file:
            return json.load(vars_file)
    except ValueError:
        if not args.list:
            raise
        return json.loads('{{"PREFIX": "{}"}}'.format(os.getcwd()))
    except IOError as error:
        if error.errno == errno.ENOENT:
            if not args.list:
                raise
            return json.loads('{{"PREFIX": "{}"}}'.format(os.getcwd()))


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
        tmp_dir = os.getenv(
            "DAOS_TEST_SHARED_DIR", os.path.expanduser("~/daos_test"))
    else:
        tmp_dir = os.path.join(base_dir, "tmp")

    # Make sure the temporary directory exists to prevent pydaos import errors
    if not os.path.exists(tmp_dir):
        os.makedirs(tmp_dir)

    return tmp_dir


def set_test_environment(args):
    """Set up the test environment.

    Args:
        args (argparse.Namespace): command line arguments for this program

    Returns:
        None

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
        # Get the default interface to use if OFI_INTERFACE is not set
        interface = os.environ.get("OFI_INTERFACE")
        if interface is None:
            # Find all the /sys/class/net interfaces on the launch node
            # (excluding lo)
            print("Detecting network devices - OFI_INTERFACE not set")
            available_interfaces = {}
            net_path = os.path.join(os.path.sep, "sys", "class", "net")
            net_list = [dev for dev in os.listdir(net_path) if dev != "lo"]
            for device in sorted(net_list):
                if device == "bonding_masters":
                    continue
                # Get the interface state - only include active (up) interfaces
                with open(os.path.join(net_path, device, "operstate"), "r") as \
                     fileh:
                    state = fileh.read().strip()
                # Only include interfaces that are up
                if state.lower() == "up":
                    # Get the interface speed - used to select the fastest
                    # available
                    with open(os.path.join(net_path, device, "speed"), "r") as \
                         fileh:
                        try:
                            speed = int(fileh.read().strip())
                            # KVM/Qemu/libvirt returns an EINVAL
                        except IOError as ioerror:
                            if ioerror.errno == errno.EINVAL:
                                speed = 1000
                            else:
                                raise
                    print(
                        "  - {0:<5} (speed: {1:>6} state: {2})".format(
                            device, speed, state))
                    # Only include the first active interface for each speed -
                    # first is determined by an alphabetic sort: ib0 will be
                    # checked before ib1
                    if speed not in available_interfaces:
                        available_interfaces[speed] = device
            print("Available interfaces: {}".format(available_interfaces))
            try:
                # Select the fastest active interface available by sorting
                # the speed
                interface = \
                    available_interfaces[sorted(available_interfaces)[-1]]
            except IndexError:
                print(
                    "Error obtaining a default interface from: {}".format(
                        os.listdir(net_path)))
                exit(1)
        print("Using {} as the default interface".format(interface))

        # Update env definitions
        os.environ["CRT_CTX_SHARE_ADDR"] = "0"
        os.environ["OFI_INTERFACE"] = os.environ.get("OFI_INTERFACE", interface)

        # Set the default location for daos log files written during testing
        # if not already defined.
        if "DAOS_TEST_LOG_DIR" not in os.environ:
            os.environ["DAOS_TEST_LOG_DIR"] = DEFAULT_DAOS_TEST_LOG_DIR
        os.environ["D_LOG_FILE"] = os.path.join(
            os.environ["DAOS_TEST_LOG_DIR"], "daos.log")

        # Ensure the daos log files directory exists on each possible test node
        test_hosts = NodeSet(socket.gethostname().split(".")[0])
        test_hosts.update(args.test_clients)
        test_hosts.update(args.test_servers)
        spawn_commands(
            test_hosts, "mkdir -p {}".format(os.environ["DAOS_TEST_LOG_DIR"]))

        # Assign the default value for transport configuration insecure mode
        os.environ["DAOS_INSECURE_MODE"] = str(args.insecure_mode)

    # Update PATH
    os.environ["PATH"] = ":".join([bin_dir, sbin_dir, usr_sbin, path])

    # Python paths required for functional testing
    python_version = "python{}{}".format(
        version_info.major,
        "" if version_info.major > 2 else ".{}".format(version_info.minor))
    required_python_paths = [
        os.path.abspath("util/apricot"),
        os.path.abspath("util"),
        os.path.abspath("cart/util"),
        os.path.join(base_dir, "lib64", python_version, "site-packages"),
    ]

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
    print("Using PYTHONPATH={}".format(os.environ["PYTHONPATH"]))
    if args.verbose:
        print("ENVIRONMENT VARIABLES")
        for key in sorted(os.environ):
            print("  {}: {}".format(key, os.environ[key]))


def run_command(cmd):
    """Get the output of given command executed on this host.

    Args:
        cmd (list): command from which to obtain the output

    Raises:
        RuntimeError: if the command fails

    Returns:
        str: command output

    """
    print("Running {}".format(" ".join(cmd)))
    process = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    stdout, _ = process.communicate()
    retcode = process.poll()
    if retcode:
        raise RuntimeError(
            "Error executing '{}':\n\tOutput:\n{}".format(
                " ".join(cmd), stdout))
    return stdout


def get_output(cmd, check=True):
    """Get the output of given command executed on this host.

    Args:
        cmd (list): command from which to obtain the output
        check (bool, optional): whether to emit an error and exit the
            program if the exit status of the command is non-zero. Defaults
            to True.

    Returns:
        str: command output
    """
    try:
        stdout = run_command(cmd)
    except RuntimeError as error:
        if check:
            print(error)
            exit(1)
        stdout = str(error)
    return stdout


def time_command(cmd):
    """Execute the command on this host and display its duration.

    Args:
        cmd (list): command to time

    Returns:
        int: return code of the command

    """
    print("Running: {}".format(" ".join(cmd)))
    start_time = int(time.time())
    return_code = subprocess.call(cmd)
    end_time = int(time.time())
    print("Total test time: {}s".format(end_time - start_time))
    return return_code


def get_remote_output(host_list, command, timeout=120):
    """Run the command on each specified host in parallel.

    Args:
        host_list (list): list of hosts
        command (str): command to run on each host
        timeout (int, optional): number of seconds to wait for all jobs to
            complete. Defaults to 120 seconds.

    Returns:
        Task: a Task object containing the result of the running the command on
            the specified hosts

    """
    # Create a ClusterShell Task to run the command in parallel on the hosts
    if isinstance(host_list, list):
        nodes = NodeSet.fromlist(host_list)
    else:
        nodes = NodeSet(host_list)
    task = task_self()
    # task.set_info('debug', True)
    # Enable forwarding of the ssh authentication agent connection
    task.set_info("ssh_options", "-oForwardAgent=yes")
    print("Running on {}: {}".format(nodes, command))
    task.run(command=command, nodes=nodes, timeout=timeout)
    return task


def check_remote_output(task, command):
    """Check if a remote command completed successfully on all hosts.

    Args:
        task (Task): a Task object containing the command result
        command (str): command run by the task

    Returns:
        bool: True if the command completed successfully (rc=0) on each
            specified host; False otherwise

    """
    # Create a dictionary of hosts for each unique return code
    results = {code: hosts for code, hosts in task.iter_retcodes()}

    # Determine if the command completed successfully across all the hosts
    status = len(results) == 1 and 0 in results
    if not status:
        print("  Errors detected running \"{}\":".format(command))

    # Display the command output
    for code in sorted(results):
        output_data = list(task.iter_buffers(results[code]))
        if not output_data:
            output_data = [["<NONE>", results[code]]]

        for output, o_hosts in output_data:
            n_set = NodeSet.fromlist(o_hosts)
            lines = str(output).splitlines()
            print("There are {} lines of output".format(len(lines)))
            if len(lines) > 1:
                print("    {}: rc={}, output:".format(n_set, code))
                for line in lines:
                    try:
                        print("      {}".format(line))
                    except IOError:
                        # DAOS-5781 Jenkins doesn't like receiving large
                        # amounts of data in a short space of time so catch
                        # this and retry.
                        time.sleep(5)
                        print("      {}".format(line))
            else:
                print("    {}: rc={}, output: {}".format(n_set, code, output))

    # List any hosts that timed out
    timed_out = [str(hosts) for hosts in task.iter_keys_timeout()]
    if timed_out:
        print("    {}: timeout detected".format(NodeSet.fromlist(timed_out)))

    return status


def spawn_commands(host_list, command, timeout=120):
    """Run the command on each specified host in parallel.

    Args:
        host_list (list): list of hosts
        command (str): command to run on each host
        timeout (int, optional): number of seconds to wait for all jobs to
            complete. Defaults to 120 seconds.

    Returns:
        bool: True if the command completed successfully (rc=0) on each
            specified host; False otherwise

    """
    # Create a dictionary of hosts for each unique return code
    task = get_remote_output(host_list, command, timeout)

    # Determine if the command completed successfully across all the hosts
    return check_remote_output(task, command)


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
        for obj_key, obj_val in obj.items():
            add_matches(find_values(obj_val, keys, obj_key, val_type))
    elif isinstance(obj, list):
        # Recursively look for matches in each list entry
        for item in obj:
            add_matches(find_values(item, keys, None, val_type))

    return matches


def get_test_list(tags):
    """Generate a list of tests and avocado tag filter from a list of tags.

    Args:
        tags (list): a list of tag or test file names

    Returns:
        (list, list): a tuple of an avocado tag filter list and lists of tests

    """
    test_tags = []
    test_list = []
    # Check if fault injection is enabled ( 0 return status)
    faults_disabled = False
    try:
        faults_disabled = time_command(["fault_status"])
    except OSError as error:
        if error.errno == errno.ENOENT:
            # Not built yet.  Must be trying to figure out which tests are
            # run for the given tag(s).  Assume this is not a release run
            # then and faults are enabled
            pass
    for tag in tags:
        if ".py" in tag:
            # Assume '.py' indicates a test and just add it to the list
            test_list.append(tag)
            fault_filter = "--filter-by-tags=-faults"
            if faults_disabled and fault_filter not in test_tags:
                test_tags.append(fault_filter)
        else:
            # Otherwise it is assumed that this is a tag
            if faults_disabled:
                tag = ",".join((tag, "-faults"))
            test_tags.append("--filter-by-tags={}".format(tag))

    # Update the list of tests with any test that match the specified tags.
    # Exclude any specified tests that do not match the specified tags.  If no
    # tags and no specific tests have been specified then all of the functional
    # tests will be added.
    if test_tags or not test_list:
        if not test_list:
            test_list = ["./"]
        command = ["avocado", "list", "--paginator=off"]
        for test_tag in test_tags:
            command.append(str(test_tag))
        command.extend(test_list if test_list else ["./"])
        tagged_tests = re.findall(r"INSTRUMENTED\s+(.*):", get_output(command))
        test_list = list(set(tagged_tests))

    return test_tags, test_list


def get_test_files(test_list, args, tmp_dir):
    """Get a list of the test scripts to run and their yaml files.

    Args:
        test_list (list): list of test scripts to run
        args (argparse.Namespace): command line arguments for this program
        tmp_dir (TemporaryDirectory): temporary directory object to use to
            write modified yaml files

    Returns:
        list: a list of dictionaries of each test script and yaml file; If
            there was an issue replacing a yaml host placeholder the yaml
            dictionary entry will be set to None.

    """
    test_files = [{"py": test, "yaml": None} for test in test_list]
    for test_file in test_files:
        base, _ = os.path.splitext(test_file["py"])
        test_file["yaml"] = replace_yaml_file(
            "{}.yaml".format(base), args, tmp_dir)

    return test_files


def get_nvme_replacement(args):
    """Determine the value to use for the '--nvme' command line argument.

    Parse the lspci output for any NMVe devices, e.g.
        $ lspci | grep 'Non-Volatile memory controller:'
        5e:00.0 Non-Volatile memory controller:
            Intel Corporation NVMe Datacenter SSD [3DNAND, Beta Rock Controller]
        5f:00.0 Non-Volatile memory controller:
            Intel Corporation NVMe Datacenter SSD [3DNAND, Beta Rock Controller]
        81:00.0 Non-Volatile memory controller:
            Intel Corporation NVMe Datacenter SSD [Optane]
        da:00.0 Non-Volatile memory controller:
            Intel Corporation NVMe Datacenter SSD [Optane]

    Optionally filter the above output even further with a specified search
    string (e.g. '--nvme=auto:Optane'):
        $ lspci | grep 'Non-Volatile memory controller:' | grep 'Optane'
        81:00.0 Non-Volatile memory controller:
            Intel Corporation NVMe Datacenter SSD [Optane]
        da:00.0 Non-Volatile memory controller:
            Intel Corporation NVMe Datacenter SSD [Optane]

    Args:
        args (argparse.Namespace): command line arguments for this program

    Returns:
        str: a comma-separated list of nvme device pci addresses available on
            all of the specified test servers

    """
    # A list of server host is required to able to auto-detect NVMe devices
    if not args.test_servers:
        print("ERROR: Missing a test_servers list to auto-detect NVMe devices")
        exit(1)

    # Get a list of NVMe devices from each specified server host
    host_list = list(args.test_servers)
    command_list = [
        "/sbin/lspci -D", "grep 'Non-Volatile memory controller:'"]
    if ":" in args.nvme:
        command_list.append("grep '{}'".format(args.nvme.split(":")[1]))
    command = " | ".join(command_list)
    task = get_remote_output(host_list, command)

    # Verify the command was successful on each server host
    if not check_remote_output(task, command):
        print("ERROR: Issuing commands to detect NVMe PCI addresses.")
        exit(1)

    # Verify each server host has the same NVMe PCI addresses
    output_data = list(task.iter_buffers())
    if len(output_data) > 1:
        print("ERROR: Non-homogeneous NVMe PCI addresses.")
        exit(1)

    # Get the list of NVMe PCI addresses found in the output
    devices = find_pci_address(output_data[0][0])
    print("Auto-detected NVMe devices on {}: {}".format(host_list, devices))
    return ",".join(devices)


def find_pci_address(value):
    """Find PCI addresses in the specified string.

    Args:
        value (str): string to search for PCI addresses

    Returns:
        list: a list of all the PCI addresses found in the string

    """
    pattern = r"[{0}]{{4}}:[{0}]{{2}}:[{0}]{{2}}\.[{0}]".format("0-9a-fA-F")
    return re.findall(pattern, str(value))


def replace_yaml_file(yaml_file, args, tmp_dir):
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
        yaml_file (str): test yaml file
        args (argparse.Namespace): command line arguments for this program
        tmp_dir (TemporaryDirectory): temporary directory object to use to
            write modified yaml files

    Returns:
        str: the test yaml file; None if the yaml file contains placeholders
            w/o replacements

    """
    replacements = {}

    if args.test_servers or args.nvme:
        # Find the test yaml keys and values that match the replaceable fields
        yaml_data = get_yaml_data(yaml_file)
        yaml_keys = list(YAML_KEYS.keys())
        yaml_find = find_values(yaml_data, yaml_keys)

        # Generate a list of values that can be used as replacements
        new_values = {}
        for key, value in YAML_KEYS.items():
            args_value = getattr(args, value)
            if isinstance(args_value, NodeSet):
                new_values[key] = list(args_value)
            elif args_value:
                new_values[key] = args_value.split(",")
            else:
                new_values[key] = []

        # Assign replacement values for the test yaml entries to be replaced
        display(args, "Detecting replacements for {} in {}".format(
            yaml_keys, yaml_file))
        display(args, "  Found values: {}".format(yaml_find))
        display(args, "  New values:   {}".format(new_values))

        for key in YAML_KEY_ORDER:
            # If the user did not provide a specific list of replacement
            # test_clients values, use the remaining test_servers values to
            # replace test_clients placeholder values
            if key == "test_clients" and not new_values[key]:
                new_values[key] = new_values["test_servers"]

            # Replace test yaml keys that were:
            #   - found in the test yaml
            #   - have a user-specified replacement
            if key in yaml_find and new_values[key]:
                if key.startswith("test_"):
                    # The entire server/client test yaml list entry is replaced
                    # by a new test yaml list entry, e.g.
                    #   '- serverA' --> '- wolf-1'
                    value_format = "- {}"
                    values_to_replace = [
                        value_format.format(item) for item in yaml_find[key]]

                else:
                    # Individual bdev_list NVMe PCI addresses in the test yaml
                    # file are replaced with the new NVMe PCI addresses in the
                    # order they are found, e.g.
                    #   0000:81:00.0 --> 0000:12:00.0
                    value_format = "\"{}\""
                    values_to_replace = [
                        value_format.format(item)
                        for item in find_pci_address(yaml_find[key])]

                # Add the next user-specified value as a replacement for the key
                for value in values_to_replace:
                    if value in replacements:
                        continue
                    try:
                        replacements[value] = value_format.format(
                            new_values[key].pop(0))
                    except IndexError:
                        replacements[value] = None
                    display(
                        args,
                        "  - Replacement: {} -> {}".format(
                            value, replacements[value]))

    if replacements:
        # Read in the contents of the yaml file to retain the !mux entries
        print("Reading {}".format(yaml_file))
        with open(yaml_file) as yaml_buffer:
            yaml_data = yaml_buffer.read()

        # Apply the placeholder replacements
        missing_replacements = []
        display(args, "Modifying contents: {}".format(yaml_file))
        for key in sorted(replacements):
            value = replacements[key]
            if value:
                # Replace the host entries with their mapped values
                display(args, "  - Replacing: {} --> {}".format(key, value))
                yaml_data = re.sub(key, value, yaml_data)
            elif args.discard:
                # Discard any host entries without a replacement value
                display(args, "  - Removing:  {}".format(key))
                yaml_data = re.sub(r"\s*[,]?{}".format(key), "", yaml_data)
            else:
                # Keep track of any placeholders without a replacement value
                display(args, "  - Missing:   {}".format(key))
                missing_replacements.append(key)
        if missing_replacements:
            # Report an error for all of the placeholders w/o a replacement
            print(
                "Error: Placeholders missing replacements in {}:\n  {}".format(
                    yaml_file, ", ".join(missing_replacements)))
            return None

        # Write the modified yaml file into a temporary file.  Use the path to
        # ensure unique yaml files for tests with the same filename.
        orig_yaml_file = yaml_file
        yaml_name = get_test_category(yaml_file)
        yaml_file = os.path.join(tmp_dir.name, "{}.yaml".format(yaml_name))
        print("Creating copy: {}".format(yaml_file))
        with open(yaml_file, "w") as yaml_buffer:
            yaml_buffer.write(yaml_data)

        # Optionally display the file
        if args.verbose:
            cmd = ["diff", "-y", orig_yaml_file, yaml_file]
            print(get_output(cmd, False))

    # Return the untouched or modified yaml file
    return yaml_file


def generate_certs():
    """Generate the certificates for the test."""
    daos_test_log_dir = os.environ["DAOS_TEST_LOG_DIR"]
    certs_dir = os.path.join(daos_test_log_dir, "daosCA")
    subprocess.call(["/usr/bin/rm", "-rf", certs_dir])
    subprocess.call(
        ["../../../../lib64/daos/certgen/gen_certificates.sh",
         daos_test_log_dir])


def run_tests(test_files, tag_filter, args):
    """Run or display the test commands.

    Args:
        test_files (dict): a list of dictionaries of each test script/yaml file
        tag_filter (list): the avocado tag filter command line argument
        args (argparse.Namespace): command line arguments for this program

    Returns:
        int: a bitwise-or of all the return codes of each 'avocado run' command

    """
    return_code = 0

    # Determine the location of the avocado logs for archiving or renaming
    data = get_output(["avocado", "config"]).strip()
    avocado_logs_dir = re.findall(r"datadir\.paths\.logs_dir\s+(.*)", data)
    avocado_logs_dir = os.path.expanduser(avocado_logs_dir[0])
    print("Avocado logs stored in {}".format(avocado_logs_dir))

    # Create the base avocado run command
    command_list = [
        "avocado",
        "run",
        "--ignore-missing-references", "on",
        "--html-job-result", "on",
        "--tap-job-result", "off",
    ]
    if not args.sparse:
        command_list.append("--show-job-log")
    if tag_filter:
        command_list.extend(tag_filter)

    # Run each test
    skip_reason = None
    for test_file in test_files:
        if skip_reason is not None:
            # An error was detected running clean_logs for a previous test.  As
            # this is typically an indication of a communication issue with one
            # of the hosts, do not attempt to run subsequent tests.
            if not report_skipped_test(
                    test_file["py"], avocado_logs_dir, skip_reason):
                return_code |= 64

        elif not isinstance(test_file["yaml"], str):
            # The test was not run due to an error replacing host placeholders
            # in the yaml file.  Treat this like a failed avocado command.
            reason = "error replacing yaml file placeholders"
            if not report_skipped_test(
                    test_file["py"], avocado_logs_dir, reason):
                return_code |= 64
            return_code |= 4

        else:
            # Optionally clean the log files before running this test on the
            # servers and clients specified for this test
            if args.clean:
                if not clean_logs(test_file["yaml"], args):
                    # Report errors for this skipped test
                    skip_reason = (
                        "host communication error attempting to clean out "
                        "leftover logs from a previous test run prior to "
                        "running this test")
                    if not report_skipped_test(
                            test_file["py"], avocado_logs_dir, skip_reason):
                        return_code |= 64
                    return_code |= 128
                    continue

            # Execute this test
            test_command_list = list(command_list)
            test_command_list.extend([
                "--mux-yaml", test_file["yaml"], "--", test_file["py"]])
            return_code |= time_command(test_command_list)

            # Optionally store all of the server and client config files
            # and archive remote logs and report big log files, if any.
            if args.archive:
                return_code |= archive_config_files(avocado_logs_dir, args)
                return_code |= archive_daos_logs(
                    avocado_logs_dir, test_file, args)
                return_code |= archive_cart_logs(
                    avocado_logs_dir, test_file, args)

                # Compress any log file that haven't been remotely compressed.
                compress_log_files(avocado_logs_dir, args)

            # Optionally rename the test results directory for this test
            if args.rename:
                rename_logs(avocado_logs_dir, test_file["py"])

            # Optionally process core files
            if args.process_cores:
                if not process_the_cores(avocado_logs_dir, test_file["yaml"],
                                         args):
                    return_code |= 256

    return return_code


def get_yaml_data(yaml_file):
    """Get the contents of a yaml file as a dictionary.

    Args:
        yaml_file (str): yaml file to read

    Raises:
        Exception: if an error is encountered reading the yaml file

    Returns:
        dict: the contents of the yaml file

    """
    yaml_data = {}
    if os.path.isfile(yaml_file):
        with open(yaml_file, "r") as open_file:
            try:
                file_data = open_file.read()
                yaml_data = yaml.safe_load(file_data.replace("!mux", ""))
            except yaml.YAMLError as error:
                print("Error reading {}: {}".format(yaml_file, error))
                exit(1)
    return yaml_data


def find_yaml_hosts(test_yaml):
    """Find the all the host values in the specified yaml file.

    Args:
        test_yaml (str): test yaml file

    Returns:
        dict: a dictionary of each host key and its host values

    """
    return find_values(
        get_yaml_data(test_yaml),
        [YAML_KEYS["test_servers"], YAML_KEYS["test_clients"]])


def get_hosts_from_yaml(test_yaml, args):
    """Extract the list of hosts from the test yaml file.

    This host will be included in the list if no clients are explicitly called
    out in the test's yaml file.

    Args:
        test_yaml (str): test yaml file
        args (argparse.Namespace): command line arguments for this program

    Returns:
        list: a unique list of hosts specified in the test's yaml file

    """
    host_set = set()
    if args.include_localhost:
        host_set.add(socket.gethostname().split(".")[0])
    found_client_key = False
    for key, value in find_yaml_hosts(test_yaml).items():
        host_set.update(value)
        if key in YAML_KEYS["test_clients"]:
            found_client_key = True

    # Include this host as a client if no clients are specified
    if not found_client_key:
        host_set.add(socket.gethostname().split(".")[0])

    return sorted(list(host_set))


def clean_logs(test_yaml, args):
    """Remove the test log files on each test host.

    Args:
        test_yaml (str): yaml file containing host names
        args (argparse.Namespace): command line arguments for this program
    """
    # Remove any log files from the DAOS_TEST_LOG_DIR directory
    logs_dir = os.environ.get("DAOS_TEST_LOG_DIR", DEFAULT_DAOS_TEST_LOG_DIR)
    host_list = get_hosts_from_yaml(test_yaml, args)
    command = "sudo rm -fr {}".format(os.path.join(logs_dir, "*.log*"))
    print("Cleaning logs on {}".format(host_list))
    if not spawn_commands(host_list, command):
        print("Error cleaning logs, aborting")
        return False

    return True


def get_remote_file_command():
    """Get path to get_remote_files.sh script."""
    return "{}/get_remote_files.sh".format(os.path.abspath(os.getcwd()))


def compress_log_files(avocado_logs_dir, args):
    """Compress log files.

    Args:
        avocado_logs_dir (str): path to the avocado log files
    """
    print("Compressing files in {}".format(socket.gethostname().split(".")[0]))
    logs_dir = os.path.join(avocado_logs_dir, "latest", "daos_logs", "*.log*")
    command = [
        get_remote_file_command(), "-z", "-x", "-f {}".format(logs_dir)]
    if args.verbose:
        command.append("-v")
    print(get_output(command, check=False))


def archive_daos_logs(avocado_logs_dir, test_files, args):
    """Archive daos log files to the avocado results directory.

    Args:
        avocado_logs_dir (str): path to the avocado log files
        test_files (dict): a list of dictionaries of each test script/yaml file
        args (argparse.Namespace): command line arguments for this program

    Returns:
        int: status code.

    """
    # Create a subdirectory in the avocado logs directory for this test
    destination = os.path.join(avocado_logs_dir, "latest", "daos_logs")

    # Copy any DAOS logs created on any host under test
    hosts = get_hosts_from_yaml(test_files["yaml"], args)
    print("Archiving host logs from {} in {}".format(hosts, destination))

    # Copy any log files written to the DAOS_TEST_LOG_DIR directory
    logs_dir = os.environ.get("DAOS_TEST_LOG_DIR", DEFAULT_DAOS_TEST_LOG_DIR)
    task = archive_files(
        destination, hosts, "{}/*.log*".format(logs_dir), True, args)

    # Determine if the command completed successfully across all the hosts
    status = 0
    if not check_remote_output(task, "archive_daos_logs command"):
        status |= 16
    if args.logs_threshold:
        test_name = get_test_category(test_files["py"])
        if not check_big_files(avocado_logs_dir, task, test_name, args):
            status |= 32
    return status


def archive_cart_logs(avocado_logs_dir, test_files, args):
    """Archive cart log files to the avocado results directory.

    Args:
        avocado_logs_dir (str): path to the avocado log files
        test_files (dict): a list of dictionaries of each test script/yaml file
        args (argparse.Namespace): command line arguments for this program

    Returns:
        int: status code.

    """
    # Create a subdirectory in the avocado logs directory for this test
    destination = os.path.join(avocado_logs_dir, "latest", "cart_logs")

    # Copy any DAOS logs created on any host under test
    hosts = get_hosts_from_yaml(test_files["yaml"], args)
    print("Archiving host logs from {} in {}".format(hosts, destination))

    # Copy any log files written to the DAOS_TEST_LOG_DIR directory
    logs_dir = os.environ.get("DAOS_TEST_LOG_DIR", DEFAULT_DAOS_TEST_LOG_DIR)
    task = archive_files(
        destination, hosts, "{}/*/*log*".format(logs_dir), True, args)

    # Determine if the command completed successfully across all the hosts
    status = 0
    if not check_remote_output(task, "archive_cart_logs command"):
        status |= 16
    if args.logs_threshold:
        test_name = get_test_category(test_files["py"])
        if not check_big_files(avocado_logs_dir, task, test_name, args):
            status |= 32
    return status


def archive_config_files(avocado_logs_dir, args):
    """Copy all of the configuration files to the avocado results directory.

    Args:
        avocado_logs_dir (str): path to the avocado log files
        args (argparse.Namespace): command line arguments for this program

    Returns:
        int: status code.

    """
    # Create a subdirectory in the avocado logs directory for this test
    destination = os.path.join(avocado_logs_dir, "latest", "daos_configs")

    # Config files can be copied from the local host as they are currently
    # written to a shared directory
    this_host = socket.gethostname().split(".")[0]
    host_list = [this_host]
    print("Archiving config files from {} in {}".format(host_list, destination))

    # Copy any config files
    base_dir = get_build_environment(args)["PREFIX"]
    configs_dir = get_temporary_directory(args, base_dir)
    task = archive_files(
        destination, host_list, "{}/*_*_*.yaml".format(configs_dir), False,
        args)

    status = 0
    if not check_remote_output(task, "archive_config_files"):
        status = 16
    return status


def archive_files(destination, hosts, source_files, cart, args):
    """Archive all of the remote files to the destination directory.

    Args:
        destination (str): path to which to archive files
        hosts (list): hosts from which to archive files
        source_files (str): remote files to archive
        cart (str): enable running cart_logtest.py
        args (argparse.Namespace): command line arguments for this program

    Returns:
        Task: a Task object containing the result of the running the command on
            the specified hosts

    """
    this_host = socket.gethostname().split(".")[0]

    # Create the destination directory
    if not os.path.exists(destination):
        get_output(["mkdir", destination])

    # Display available disk space prior to copy.  Allow commands to fail w/o
    # exiting this program.  Any disk space issues preventing the creation of a
    # directory will be caught in the archiving of the source files.
    display_disk_space(destination)

    command = [
        get_remote_file_command(),
        "-z",
        "-a \"{}:{}\"".format(this_host, destination),
        "-f \"{}\"".format(source_files),
    ]
    if cart:
        command.append("-c")
    if args.logs_threshold:
        command.append("-t \"{}\"".format(args.logs_threshold))
    if args.verbose:
        command.append("-v")
    return get_remote_output(hosts, " ".join(command), 900)


def rename_logs(avocado_logs_dir, test_file):
    """Append the test name to its avocado job-results directory name.

    Args:
        avocado_logs_dir (str): avocado job-results directory
        test_file (str): the test python file
    """
    test_name = get_test_category(test_file)
    test_logs_lnk = os.path.join(avocado_logs_dir, "latest")
    test_logs_dir = os.path.realpath(test_logs_lnk)
    new_test_logs_dir = "{}-{}".format(test_logs_dir, test_name)
    try:
        os.rename(test_logs_dir, new_test_logs_dir)
        os.remove(test_logs_lnk)
        os.symlink(new_test_logs_dir, test_logs_lnk)
        print("Renamed {} to {}".format(test_logs_dir, new_test_logs_dir))
    except OSError as error:
        print(
            "Error renaming {} to {}: {}".format(
                test_logs_dir, new_test_logs_dir, error))


def check_big_files(avocado_logs_dir, task, test_name, args):
    """Check the contents of the task object, tag big files, create junit xml.

    Args:
        avocado_logs_dir (str): path to the avocado log files.
        task (Task): a Task object containing the command result
        test_name (str): current running testname
        args (argparse.Namespace): command line arguments for this program

    Returns:
        bool: True if no errors occurred checking and creating junit file.
            False, otherwise.

    """
    status = True
    hosts = NodeSet()
    cdata = []
    for output, nodelist in task.iter_buffers():
        node_set = NodeSet.fromlist(nodelist)
        hosts.update(node_set)
        big_files = re.findall(r"Y:\s([0-9]+)", str(output))
        if big_files:
            cdata.append(
                "The following log files on {} exceeded the {} "
                "threshold:".format(node_set, args.logs_threshold))
            cdata.extend(["  {}".format(big_file) for big_file in big_files])
    if cdata:
        destination = os.path.join(avocado_logs_dir, "latest")
        message = "Log size has exceed threshold for this test on: {}".format(
            hosts)
        status = create_results_xml(
            message, test_name, "\n".join(cdata), destination)
    else:
        print("No log files found exceeding {}".format(args.logs_threshold))

    return status


def report_skipped_test(test_file, avocado_logs_dir, reason):
    """Report an error for the skipped test.

    Args:
        test_file (str): the test python file
        avocado_logs_dir (str): avocado job-results directory
        reason (str): test skip reason

    Returns:
        bool: status of writing to junit file

    """
    message = "The {} test was skipped due to {}".format(test_file, reason)
    print(message)

    # Generate a fake avocado results.xml file to report the skipped test.
    # This file currently requires being placed in a job-* subdirectory.
    test_name = get_test_category(test_file)
    time_stamp = datetime.now().strftime("%Y-%m-%dT%H.%M")
    destination = os.path.join(
        avocado_logs_dir, "job-{}-da03911-{}".format(time_stamp, test_name))
    try:
        os.makedirs(destination)
    except (OSError, FileExistsError) as error:
        print(
            "Warning: Continuing after failing to create {}: {}".format(
                destination, error))
    return create_results_xml(
        message, test_name, "See launch.py command output for more details",
        destination)


def create_results_xml(message, testname, output, destination):
    """Create JUnit xml file.

    Args:
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
    testcase_attributes = {"name": "framework_results", "time": "0.0"}
    testcase = ET.SubElement(testsuite, "testcase", testcase_attributes)
    ET.SubElement(testcase, "error", {"message": message})
    system_out = ET.SubElement(testcase, "system-out")
    system_out.text = output

    # Get xml as string and write it to a file
    rough_xml = ET.tostring(testsuite, "utf-8")
    junit_xml = minidom.parseString(rough_xml)
    results_xml = os.path.join(destination, "framework_results.xml")
    print("Generating junit xml file {} ...".format(results_xml))
    try:
        with open(results_xml, "w") as xml_buffer:
            xml_buffer.write(junit_xml.toprettyxml())
    except IOError as error:
        print("Failed to create xml file: {}".format(error))
        status = False
    return status


USE_DEBUGINFO_INSTALL = True


def resolve_debuginfo(pkg):
    """Return the debuginfo package for a given package name.

    Args:
        pkg (str): a package name

    Returns:
        dict: dictionary of debug package information

    """
    import yum      # pylint: disable=import-error,import-outside-toplevel

    yum_base = yum.YumBase()
    yum_base.conf.assumeyes = True
    yum_base.setCacheDir(force=True, reuse=True)
    yum_base.repos.enableRepo('*debug*')

    debuginfo_map = {'glibc':   'glibc-debuginfo-common'}

    try:
        debug_pkg = debuginfo_map[pkg]
    except KeyError:
        debug_pkg = pkg + "-debuginfo"
    try:
        pkg_data = yum_base.rpmdb.returnNewestByName(name=pkg)[0]
    except yum.Errors.PackageSackError as expn:
        if expn.__str__().rstrip() == "No Package Matching " + pkg:
            print("Package {} not installed, "
                  "skipping debuginfo".format(pkg))
            return None
        raise

    return {'name': debug_pkg,
            'version': pkg_data['version'],
            'release': pkg_data['release'],
            'epoch': pkg_data['epoch']}


def install_debuginfos():
    """Install debuginfo packages."""
    install_pkgs = [{'name': 'gdb'}]

    cmds = []

    # -debuginfo packages that don't get installed with debuginfo-install
    for pkg in ['python', 'daos', 'systemd', 'ndctl', 'mercury']:
        debug_pkg = resolve_debuginfo(pkg)
        if debug_pkg and debug_pkg not in install_pkgs:
            install_pkgs.append(debug_pkg)

    # remove any "source tree" test hackery that might interfere with RPM
    # installation
    path = os.path.sep + os.path.join('usr', 'share', 'spdk', 'include')
    if os.path.islink(path):
        cmds.append(["sudo", "rm", "-f", path])

    if USE_DEBUGINFO_INSTALL:
        yum_args = [
            "--exclude", "ompi-debuginfo", "libpmemobj", "python", "openmpi3"]
        cmds.append(["sudo", "yum", "-y", "install"] + yum_args)
        cmds.append(["sudo", "debuginfo-install", "--enablerepo=*-debuginfo",
                     "-y"] + yum_args + ["daos-server", "gcc"])
    else:
        # We're not using the yum API to install packages
        # See the comments below.
        # kwarg = {'name': 'gdb'}
        # yum_base.install(**kwarg)

        for debug_pkg in install_pkgs:
            # This is how you actually use the API to add a package
            # But since we need sudo to do it, we need to call out to yum
            # kwarg = debug_pkg
            # yum_base.install(**kwarg)
            install_pkgs.append(debug_pkg)

    # This is how you normally finish up a yum transaction, but
    # again, we need to employ sudo
    # yum_base.resolveDeps()
    # yum_base.buildTransaction()
    # yum_base.processTransaction(rpmDisplay=yum.rpmtrans.NoOutputCallBack())

    # Now install a few pkgs that debuginfo-install wouldn't
    cmd = ["sudo", "yum", "-y", "--enablerepo=*debug*", "install"]
    for pkg in install_pkgs:
        try:
            cmd.append(
                "{}-{}-{}".format(pkg['name'], pkg['version'], pkg['release']))
        except KeyError:
            cmd.append(pkg['name'])

    cmds.append(cmd)

    retry = False
    for cmd in cmds:
        try:
            print(run_command(cmd))
        except RuntimeError as error:
            # got an error, so abort this list of commands and re-run
            # it with a yum clean, makecache first
            print(error)
            retry = True
            break
    if retry:
        print("Going to refresh caches and try again")
        cmd_prefix = ["sudo", "yum", "--enablerepo=*debug*"]
        cmds.insert(0, cmd_prefix + ["clean", "all"])
        cmds.insert(1, cmd_prefix + ["makecache"])
        for cmd in cmds:
            print(run_command(cmd))


def process_the_cores(avocado_logs_dir, test_yaml, args):
    """Copy all of the host test log files to the avocado results directory.

    Args:
        avocado_logs_dir ([type]): [description]
        test_yaml (str): yaml file containing host names
        args (argparse.Namespace): command line arguments for this program

    Returns:
        bool: True if everything was done as expected, False if there were
              any issues processing core files
    """
    import fnmatch  # pylint: disable=import-outside-toplevel

    return_status = True
    this_host = socket.gethostname().split(".")[0]
    host_list = get_hosts_from_yaml(test_yaml, args)
    daos_cores_dir = os.path.join(avocado_logs_dir, "latest", "stacktraces")

    # Create a subdirectory in the avocado logs directory for this test
    print("Processing cores from {} in {}".format(host_list, daos_cores_dir))
    get_output(["mkdir", daos_cores_dir])

    # Copy any core files that exist on the test hosts and remove them from the
    # test host if the copy is successful.  Attempt all of the commands and
    # report status at the end of the loop.  Include a listing of the file
    # related to any failed command.
    commands = [
        "set -eu",
        "rc=0",
        "copied=()",
        "for file in /var/tmp/core.*",
        "do if [ -e $file ]",
        "then if [ ! -s $file ]",
        "then ((rc++))",
        "ls -al $file",
        "else if sudo chmod 644 $file && "
        "scp $file {}:{}/${{file##*/}}-$(hostname -s)".format(
            this_host, daos_cores_dir),
        "then copied+=($file)",
        "if ! sudo rm -fr $file",
        "then ((rc++))",
        "ls -al $file",
        "fi",
        "else ((rc++))",
        "ls -al $file",
        "fi",
        "fi",
        "fi",
        "done",
        "echo Copied ${copied[@]:-no files}",
        "exit $rc",
    ]
    if not spawn_commands(host_list, "; ".join(commands), timeout=1800):
        # we might have still gotten some core files, so don't return here
        # but save a False return status for later
        return_status = False

    cores = os.listdir(daos_cores_dir)

    if not cores:
        return True

    try:
        install_debuginfos()
    except RuntimeError as error:
        print(error)
        print("Removing core files to avoid archiving them")
        for corefile in cores:
            os.remove(os.path.join(daos_cores_dir, corefile))
        return False

    for corefile in cores:
        if not fnmatch.fnmatch(corefile, 'core.*[0-9]'):
            continue
        corefile_fqpn = os.path.join(daos_cores_dir, corefile)
        # can't use the file python magic binding here due to:
        # https://bugs.astron.com/view.php?id=225, fixed in:
        # https://github.com/file/file/commit/6faf2eba2b8c65fbac7acd36602500d757614d2f
        # but not available to us until that is in a released version
        # revert the commit this comment is in to see use python magic instead
        try:
            gdb_output = run_command(["gdb", "-c", corefile_fqpn, "-ex",
                                      "info proc exe", "-ex",
                                      "quit"])

            last_line = gdb_output.splitlines()[-1]
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
                "gdb", "-cd={}".format(daos_cores_dir),
                "-ex", "set pagination off",
                "-ex", "thread apply all bt full",
                "-ex", "detach",
                "-ex", "quit",
                exe_name, corefile
            ]
            stack_trace_file = os.path.join(
                daos_cores_dir, "{}.stacktrace".format(corefile))
            try:
                with open(stack_trace_file, "w") as stack_trace:
                    stack_trace.writelines(get_output(cmd))
            except IOError as error:
                print("Error writing {}: {}".format(stack_trace_file, error))
                return_status = False
        else:
            print(
                "Unable to determine executable name from gdb output: '{}'\n"
                "Not creating stacktrace".format(gdb_output))
            return_status = False
        print("Removing {}".format(corefile_fqpn))
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
    return "-".join(
        [os.path.splitext(os.path.basename(part))[0] for part in file_parts])


def main():
    """Launch DAOS functional tests."""
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
        "-d", "--discard",
        action="store_true",
        help="when replacing server/client yaml file placeholders, discard "
             "any placeholders that do not end up with a replacement value")
    parser.add_argument(
        "-i", "--include_localhost",
        action="store_true",
        help="include the local host when cleaning and archiving")
    parser.add_argument(
        "-l", "--list",
        action="store_true",
        help="list the python scripts that match the specified tags")
    parser.add_argument(
        "-m", "--modify",
        action="store_true",
        help="modify the test yaml files but do not run the tests")
    parser.add_argument(
        "-n", "--nvme",
        action="store",
        help="comma-separated list of NVMe device PCI addresses to use as "
             "replacement values for the bdev_list in each test's yaml file.  "
             "Using the 'auto[:<filter>]' keyword will auto-detect the NVMe "
             "PCI address list on each of the '--test_servers' hosts - the "
             "optional '<filter>' can be used to limit auto-detected "
             "addresses, e.g. 'auto:Optane' for Intel Optane NVMe devices.")
    parser.add_argument(
        "-r", "--rename",
        action="store_true",
        help="rename the avocado test logs directory to include the test name")
    parser.add_argument(
        "-p", "--process_cores",
        action="store_true",
        help="process core files from tests")
    parser.add_argument(
        "-th", "--logs_threshold",
        action="store",
        help="collect log sizes and report log sizes that go past provided"
             "threshold. e.g. '-th 5M'"
             "Valid threshold units are: B, K, M, G, T")
    parser.add_argument(
        "-s", "--sparse",
        action="store_true",
        help="limit output to pass/fail")
    parser.add_argument(
        "-ins", "--insecure_mode",
        action="store_true",
        help="Launch test with insecure-mode")
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
        "-ts", "--test_servers",
        action="store",
        help="comma-separated list of hosts to use as replacement values for "
             "server placeholders in each test's yaml file.  If the "
             "'--test_clients' argument is not specified, this list of hosts "
             "will also be used to replace client placeholders.")
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="verbose output")
    args = parser.parse_args()
    print("Arguments: {}".format(args))

    # Convert host specifications into NodeSets
    args.test_servers = NodeSet(args.test_servers)
    args.test_clients = NodeSet(args.test_clients)

    # Setup the user environment
    set_test_environment(args)

    # Auto-detect nvme test yaml replacement values if requested
    if args.nvme and args.nvme.startswith("auto"):
        args.nvme = get_nvme_replacement(args)

    # Process the tags argument to determine which tests to run
    tag_filter, test_list = get_test_list(args.tags)

    # Verify at least one test was requested
    if not test_list:
        print("ERROR: No tests or tags found via {}".format(args.tags))
        exit(1)

    # Display a list of the tests matching the tags
    print("Detected tests:  \n{}".format("  \n".join(test_list)))
    if args.list:
        exit(0)

    # Create a temporary directory
    tmp_dir = TemporaryDirectory()

    # Create a dictionary of test and their yaml files
    test_files = get_test_files(test_list, args, tmp_dir)
    if args.modify:
        exit(0)

    # Generate certificate files
    generate_certs()

    # Run all the tests
    status = run_tests(test_files, tag_filter, args)

    # Process the avocado run return codes and only treat job and command
    # failures as errors.
    ret_code = 0
    if status == 0:
        print("All avocado tests passed!")
    else:
        if status & 1 == 1:
            print("Detected one or more avocado test failures!")
        if status & 8 == 8:
            print("Detected one or more interrupted avocado jobs!")
        if status & 2 == 2:
            print("ERROR: Detected one or more avocado job failures!")
            ret_code = 1
        if status & 4 == 4:
            print("ERROR: Detected one or more failed avocado commands!")
            ret_code = 1
        if status & 16 == 16:
            print("ERROR: Detected one or more tests that failed archiving!")
            ret_code = 1
        if status & 32 == 32:
            print("ERROR: Detected one or more tests with unreported big logs!")
            ret_code = 1
        if status & 64 == 64:
            print("ERROR: Failed to create a junit xml test error file!")
        if status & 128 == 128:
            print("ERROR: Failed to clean logs in preparation for test run!")
            ret_code = 1
        if status & 256 == 256:
            print("ERROR: Detected one or more tests with failure to create "
                  "stack traces from core files!")
            ret_code = 1
    exit(ret_code)


if __name__ == "__main__":
    main()
