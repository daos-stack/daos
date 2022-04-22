#!/usr/bin/python3 -u
"""
(C) Copyright 2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from argparse import ArgumentParser, RawDescriptionHelpFormatter
import os
import re
import socket
import sys

from ClusterShell.NodeSet import NodeSet

from launch import display, get_remote_output, run_command, spawn_commands, get_output

# The distro_utils.py file is installed in the util sub-directory relative to this file location
sys.path.append(os.path.join(os.getcwd(), "util"))
from distro_utils import detect                         # pylint: disable=import-outside-toplevel

USE_DEBUGINFO_INSTALL = True
EL_DISTRO_NAMES = ["almalinux", "rocky", "centos", "rhel"]
PACKAGE_NAMES = ["gdb"]
DEBUGINFO_NAMES = [
    "systemd",
    "ndctl",
    "mercury",
    "hdf5",
    "argobots",
    "libfabric",
    "hdf5-vol-daos",
    "hdf5-vol-daos-mpich",
    "hdf5-vol-daos-mpich-tests",
    "hdf5-vol-daos-openmpi",
    "hdf5-vol-daos-openmpi-tests",
    "ior",
    "daos-client",
    "daos-server",
    "daos-tests",
]


class PackageInfo():
    # pylint: disable=too-few-public-methods
    """Collection of information for a RPM package."""

    def __init__(self, name, version=None, release=None):
        """Create a PackageInfo object.

        Args:
            name (str): package name
            version (str, optional): package version. Defaults to None.
            release (str, optional): package release. Defaults to None.
        """
        self.name = name
        self.version = version
        self.release = release

    def __str__(self):
        """Get the string format of this class.

        Returns:
            str: the package information as a combined string

        """
        info = []
        for item in (self.name, self.version, self.release):
            if item is None:
                break
            info.append(item)
        return "-".join(info)


def get_core_files(args):
    """Get the names of the core files on the remote nodes and their total 1k block size.

    Args:
        args (argparse.Namespace): command line arguments for this program

    Raises:
        RuntimeError: if there is a problem listing core.* files on the remote nodes

    Returns:
        tuple: the total 1k block size required for all of the core files, a list of lists of core
            files detected per set of nodes

    """
    required_1k_blocks = 0
    core_files = []

    # Get a list of any remote core files and their size in 1k blocks
    command = ["ls", "-1sk", os.path.join(args.source, "core.*")]
    task = get_remote_output(list(args.nodes), " ".join(command))
    for output, nodelist in task.iter_buffers():
        core_files.append([NodeSet.fromlist(nodelist)])
        display(args, "Core files detected on {}:".format(str(core_files[-1][0])), 0)
        for line in output:
            match = re.findall(r"\s+(\d+)\s+(core.*)", line.decode("utf-8"))
            if not match:
                display(args, "  - No core files found", 0)
                continue
            for item in match:
                try:
                    size = int(item[0])
                except (ValueError, TypeError) as error:
                    display(args, "  - Error obtaining file size in {}: {}".format(line, error), 0)
                try:
                    name = item[1]
                except IndexError:
                    display(args, "  - Error obtaining file name in {}".format(line), 0)

                display(args, "  - {} ({} 1k blocks)".format(name, size), 0)
                required_1k_blocks += size
                core_files[-1].append(name)

    return required_1k_blocks, core_files


def get_available_blocks(args):
    """Get the local number of available 1k blocks where the core files will be copied.

    Args:
        args (argparse.Namespace): command line arguments for this program

    Raises:
        RuntimeError: if there was an error getting the number of available 1k blocks

    Returns:
        int: the number of available 1k blocks where the core files will be copied

    """
    command = ["df", "-k", "--output=avail,target", args.destination]
    try:
        stdout = run_command(command).splitlines()[-1]
    except (RuntimeError, AttributeError, ValueError) as error:
        # pylint: disable=raise-missing-from
        raise RuntimeError(
            "Error obtaining available 1k blocks for {}: {}".format(args.destination, error))

    display(args, "Available 1k blocks for {1}: {0}".format(*stdout.split()), 0)
    return int(stdout.split()[0])


def get_package_info(name, args):
    """Get the RPM package version and release information.

    Args:
        name (str): package name
        args (argparse.Namespace): command line arguments for this program

    Returns:
        PackageInfo: the RPM package information or None if the package is not found.

    """
    command = ["rpm", "-q", "--qf", "%{name} %{version} %{release}", name]
    try:
        info = PackageInfo(*run_command(command).split())
    except RuntimeError as error:
        display(
            args,
            "Warning: {} package not installed, skipping installing debuginfo package: {}".format(
                name, error),
            0)
        info = None
    return info


def is_el(distro):
    """Determine if the provided distribution is an EL distribution.

    Args:
        distro (str): distribution name

    Returns:
        bool: True if the distribution name is an EL distribution; False otherwise

    """
    return [name for name in EL_DISTRO_NAMES if name in distro.name.lower()]


def install_debuginfo_packages(package_list, args):
    """Install any packages required to generate a stacktrace.

    Args:
        package_list (list): list of package names from which to install debuginfo packages
        args (argparse.Namespace): command line arguments for this program

    Returns:
        bool: True if all the packages installed successfully; False otherwise

    """
    # The list of commands - as a list of space-separated parts of each command
    command_list = []

    # Remove any "source tree" test hackery that might interfere with RPM installation
    path = os.path.join(os.path.sep, "usr", "share", "spdk", "include")
    if os.path.islink(path):
        command_list.append(["sudo", "rm", "-f", path])

    # Populate the list of debuginfo packages to install
    packages = []
    options = ["--exclude", "ompi"]
    if os.getenv("TEST_RPMS", 'false') == 'true':
        distro_info = detect()
        if "suse" in distro_info.name.lower():
            packages.extend(["libpmemobj1", "python3", "openmpi3"])
        elif "centos" in distro_info.name.lower() and distro_info.version == "7":
            options.extend(["--exclude", "nvml"])
            packages.extend(["libpmemobj", "python36", "openmpi3", "gcc"])
        elif is_el(distro_info) and distro_info.version == "8":
            packages.extend(["libpmemobj", "python3", "openmpi", "gcc"])
        else:
            raise RuntimeError("Error unsupported distro: {}".format(distro_info))
    for name in package_list:
        info = get_package_info(name, args)
        if info is not None:
            packages.append(str(info))
    if packages:
        command_list.append(["sudo", "dnf", "-y", "debuginfo-install"] + options + packages)

    retry = 0
    while retry < 2:
        for command in command_list:
            try:
                display(args, run_command(command), 0)
            except RuntimeError as error:
                # Abort running the list of commands if an error is encountered
                display(args, error, 0)
                retry += 1
                break

        if retry == 1:
            # Re-run the lists of commands with a dnf clean and makecache
            display(args, "Going to refresh caches and try again", 0)
            command_prefix = ["sudo", "dnf"]
            command_list.insert(0, command_prefix + ["clean", "all"])
            command_list.insert(1, command_prefix + ["makecache"])

    # Return False if the retry of the commands failed
    return not retry == 2


def process_core_files(required_1k_blocks, core_file_data, args):
    """Process the core files.

    Copy the remote core files to this node and generate a stack trace from the core file.  Finally
    remove the local copy and the remote core file.

    Args:
        required_1k_blocks (int): _description_
        core_file_data (list): _description_
        args (argparse.Namespace): command line arguments for this program

    Returns:
        bool:

    """
    status = True
    this_host = socket.gethostname().split(".")[0]

    if args.install:
        install_debuginfo_packages(DEBUGINFO_NAMES, args)

    # Copy all the core files in parallel if there is enough disk space
    available_1k_blocks = get_available_blocks(args)
    if required_1k_blocks < available_1k_blocks:
        display(args, "Copying all core files from {}".format(args.nodes), 0)
        commands = [
            "for file in {}/core.*".format(args.source),
            "do scp $file {}:{}/${{file##*/}}-$(hostname -s)".format(this_host, args.destination),
            "done",
        ]
        if not spawn_commands(list(args.nodes), "; ".join(commands), timeout=1800):
            display(args, "  Not all core files copied in parallel", 0)

    # Process each core file
    for nodes, core_file_names in core_file_data:
        for node in list(nodes):
            display(args, "Processing core files from {}".format(node), 0)
            display(args, "  core_file_names: {}".format(core_file_names), 1)
            for core_file_name in core_file_names:
                local_core_file = os.path.join(args.destination, "-".join([core_file_name, node]))
                display(args, "  local_core_file: {}".format(local_core_file), 1)

                # Copy the core file from the remote node if not already copied
                if not os.path.exists(local_core_file):
                    remote_core_file = os.path.join(args.source, core_file_name)
                    display(args, "  Copying {} from {}".format(remote_core_file, node), 0)
                    commands = ["scp {} {}:{}".format(remote_core_file, this_host, local_core_file)]
                    if not spawn_commands([node], "; ".join(commands), timeout=1800):
                        display(args, "    Error copying core file from {}".format(node), 0)
                        status = False
                        continue

                # List the local core file
                try:
                    display(args, run_command(['ls', '-l', local_core_file]), 0)
                except RuntimeError as error:
                    display(args, "Unexpected error - no local core file found", 0)
                    status = False
                    continue

                # Generate a stack trace
                try:
                    generate_stacktrace(local_core_file, args)
                except RuntimeError as error:
                    display(args, error, 0)
                    status = False

    # Remove any core files from the remote nodes
    if args.remove:
        display(args, "Removing core files from {}".format(args.nodes), 0)
        commands = [
            "for file in {}/core.*".format(args.source),
            "do rm $file",
            "done",
        ]
        if not spawn_commands(list(args.nodes), "; ".join(commands), timeout=1800):
            display(args, "  Errors removing core files", 0)
            status = False

    return status


def generate_stacktrace(core_file, args):
    """Generate a stacktrace from the core file.

    Args:
        core_file (str): full path and file name of the core file
        args (argparse.Namespace): command line arguments for this program

    Raises:
        RuntimeError: if there was an error generating a stacktrace

    """
    core_file_name = os.path.basename(core_file)
    stack_trace_file = os.path.join(args.destination, ".".join([core_file_name, "stacktrace"]))

    display("  Generating {} from {}".format(stack_trace_file, core_file), 0)
    try:
        command = ["gdb", "-c", core_file, "-ex", "info proc exe", "-ex", "quit"]
        gdb_output = run_command(command)
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
            "gdb", "-cd={}".format(args.destination),
            "-ex", "set pagination off",
            "-ex", "thread apply all bt full",
            "-ex", "detach",
            "-ex", "quit",
            exe_name, core_file_name
        ]

        try:
            with open(stack_trace_file, "w") as stack_trace:
                stack_trace.writelines(get_output(cmd, check=False))
        except IOError as error:
            # pylint: disable=raise-missing-from
            raise RuntimeError("Error writing {}: {}".format(stack_trace_file, error))
        except RuntimeError as error:
            # pylint: disable=raise-missing-from
            raise RuntimeError("Error creating {}: {}".format(stack_trace_file, error))
    else:
        raise RuntimeError(
            "Error creating {}: unable to determine executable name for {} from gdb: {}".format(
                stack_trace_file, core_file, gdb_output))
    display(args, "  Removing {}".format(core_file), 0)
    os.unlink(core_file)


def main():
    """Process core files."""
    # Parse the command line arguments
    description = [
        "DAOS functional test core file processing.",
        "",
        "Collect any core.* files from the specified source path on each provided remote node,",
        "use them to generate a stacktrace in the specified destination directory on this node,",
        "and remove the core.* files from the remote nodes.  As part of this process any required",
        "debuginfo packages will be installed on this node in order to generate the stacktraces."
    ]
    parser = ArgumentParser(
        prog="launcher.py",
        formatter_class=RawDescriptionHelpFormatter,
        description="\n".join(description))
    parser.add_argument(
        "-d", "--destination",
        action="store",
        default="~/avocado/job-results/latest/stacktraces",
        type=str,
        help="number of times to repeat test execution")
    parser.add_argument(
        "-n", "--nodes",
        action="store",
        help="comma-separated list of nodes from which to collect and remove core files")
    parser.add_argument(
        "-i", "--install",
        action="store_true",
        help="install debuginfo packages prior to processing core files")
    parser.add_argument(
        "-s", "--source",
        action="store",
        default="/var/tmp",
        type=str,
        help="number of times to repeat test execution")
    parser.add_argument(
        "-v", "--verbose",
        action="count",
        default=0,
        help="verbosity output level. Specify multiple times (e.g. -vv) for additional output")
    args = parser.parse_args()
    display(args, "Arguments: {}".format(args), 1)

    # Convert node specification into a NodeSet
    args.nodes = NodeSet(args.nodes)

    # Find any core.* files found on the remote nodes
    try:
        required_1k_blocks, core_files = get_core_files(args)
    except RuntimeError as error:
        display(args, error, 0)
        sys.exit(1)

    # Generate stack traces
    if core_files:
        if not process_core_files(required_1k_blocks, core_files, args):
            sys.exit(1)

    sys.exit(0)


if __name__ == "__main__":
    main()
