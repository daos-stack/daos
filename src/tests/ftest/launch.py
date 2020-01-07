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
from __future__ import print_function

from argparse import ArgumentParser, RawDescriptionHelpFormatter
import json
import os
import re
import socket
import subprocess
from sys import version_info
import time
import yaml

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

TEST_DAOS_SERVER_YAML = "daos_avocado_test.yaml"
BASE_LOG_FILE_YAML = "./data/daos_server_baseline.yaml"
SERVER_KEYS = (
    "test_servers",
    )
CLIENT_KEYS = (
    "test_clients",
    )


def get_build_environment():
    """Obtain DAOS build environment variables from the .build_vars.json file.

    Returns:
        dict: a dictionary of DAOS build environment variable names and values

    """
    build_vars_file = os.path.join(
        os.path.dirname(os.path.realpath(__file__)),
        "../../.build_vars.json")
    with open(build_vars_file) as vars_file:
        return json.load(vars_file)


def get_temporary_directory(base_dir=None):
    """Get the temporary directory used by functional tests.

    Args:
        base_dir (str, optional): base installation directory. Defaults to None.

    Returns:
        str: the full path of the temporary directory

    """
    if base_dir is None:
        base_dir = get_build_environment()["PREFIX"]
    if base_dir == "/usr":
        tmp_dir = os.getenv(
            "DAOS_TEST_SHARED_DIR", os.path.expanduser("~/daos_test"))
    else:
        tmp_dir = os.path.join(base_dir, "tmp")

    # Make sure the temporary directory exists to prevent pydaos import errors
    if not os.path.exists(tmp_dir):
        os.makedirs(tmp_dir)

    return tmp_dir


def set_test_environment():
    """Set up the test environment.

    Returns:
        None

    """
    base_dir = get_build_environment()["PREFIX"]
    bin_dir = os.path.join(base_dir, "bin")
    sbin_dir = os.path.join(base_dir, "sbin")
    # /usr/sbin is not setup on non-root user for CI nodes.
    # SCM formatting tool mkfs.ext4 is located under
    # /usr/sbin directory.
    usr_sbin = os.path.sep + os.path.join("usr", "sbin")
    path = os.environ.get("PATH")

    # Get the default interface to use if OFI_INTERFACE is not set
    print("Detecting network devices")
    available_interfaces = {}
    # Find all the /sys/class/net interfaces on the launch node (excluding lo)
    net_path = os.path.join(os.path.sep, "sys", "class", "net")
    for device in sorted([dev for dev in os.listdir(net_path) if dev != "lo"]):
        # Get the interface state - only include active (up) interfaces
        with open(os.path.join(net_path, device, "operstate"), "r") as buffer:
            state = buffer.read().strip()
        # Get the interface speed - used to select the fastest available
        with open(os.path.join(net_path, device, "speed"), "r") as buffer:
            speed = int(buffer.read().strip())
        print(
            "  - {0:<5} (speed: {1:>6} state: {2})".format(
                device, speed, state))
        # Only include the first active interface for each speed - first is
        # determined by an alphabetic sort: ib0 will be checked before ib1
        if state.lower() == "up" and speed not in available_interfaces:
            available_interfaces[speed] = device
    try:
        # Select the fastest active interface available by sorting the speeds
        interface = available_interfaces[sorted(available_interfaces)[-1]]
    except IndexError:
        print(
            "Error obtaining a default interface from: {}".format(
                os.listdir(net_path)))
        exit(1)
    print(
        "Using {} as the default interface from {}".format(
            interface, available_interfaces))

    # Update env definitions
    os.environ["PATH"] = ":".join([bin_dir, sbin_dir, usr_sbin, path])
    os.environ["DAOS_SINGLETON_CLI"] = "1"
    os.environ["CRT_CTX_SHARE_ADDR"] = "1"
    os.environ["OFI_INTERFACE"] = os.environ.get("OFI_INTERFACE", interface)
    os.environ["CRT_ATTACH_INFO_PATH"] = get_temporary_directory(base_dir)

    # Python paths required for functional testing
    python_version = "python{}{}".format(
        version_info.major,
        "" if version_info.major > 2 else ".{}".format(version_info.minor))
    required_python_paths = [
        os.path.abspath("util/apricot"),
        os.path.abspath("util"),
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


def get_output(cmd):
    """Get the output of given command executed on this host.

    Args:
        cmd (str): command from which to obtain the output

    Returns:
        str: command output

    """
    try:
        print("Running {}".format(cmd))
        return subprocess.check_output(
            cmd, stderr=subprocess.STDOUT, shell=True)

    except subprocess.CalledProcessError as err:
        print("Error executing '{}':\n\t{}".format(cmd, err))
        exit(1)


def time_command(cmd):
    """Execute the command on this host and display its duration.

    Args:
        cmd (str): command to time

    Returns:
        int: return code of the command

    """
    print("Running {}".format(cmd))
    start_time = int(time.time())
    return_code = subprocess.call(cmd, shell=True)
    end_time = int(time.time())
    print("Total test time: {}s".format(end_time - start_time))
    return return_code


def spawn_commands(host_list, command, timeout=120):
    """Run the command on each specified host in parallel.

    Args:
        host_list (list): list of hosts
        command (str): command to run on each host
        timeout (int): number of seconds to wait for all jobs to complete

    Returns:
        bool: True if the command completed successfully (rc=0) on each
            specified host; False otherwise

    """
    # Create a ClusterShell Task to run the command in parallel on the hosts
    nodes = NodeSet.fromlist(host_list)
    task = task_self()
    # task.set_info('debug', True)
    # Enable forwarding of the ssh authentication agent connection
    task.set_info("ssh_options", "-oForwardAgent=yes")
    print("Running on {}: {}".format(nodes, command))
    task.run(command=command, nodes=nodes, timeout=timeout)

    # Create a dictionary of hosts for each unique return code
    results = {code: hosts for code, hosts in task.iter_retcodes()}

    # Determine if the command completed successfully across all the hosts
    status = len(results) == 1 and 0 in results
    if not status:
        print("  Errors detected running \"{}\":".format(command))

    # Display the command output
    for code in sorted(results):
        output_data = list(task.iter_buffers(results[code]))
        if len(output_data) == 0:
            err_nodes = NodeSet.fromlist(results[code])
            print("    {}: rc={}, output: <NONE>".format(err_nodes, code))
        else:
            for output, o_hosts in output_data:
                n_set = NodeSet.fromlist(o_hosts)
                lines = str(output).splitlines()
                if len(lines) > 1:
                    output = "\n      {}".format("\n      ".join(lines))
                print("    {}: rc={}, output: {}".format(n_set, code, output))

    # List any hosts that timed out
    timed_out = [str(hosts) for hosts in task.iter_keys_timeout()]
    if timed_out:
        print("    {}: timeout detected".format(NodeSet.fromlist(timed_out)))

    return status


def find_values(obj, keys, key=None, val_type=list):
    """Find dictionary values of a certain type specified with certain keys.

    Args:
        obj (obj): a python object; initailly the dictionary to search
        keys (list): list of keys to find their matching list values
        key (str, optional): key to check for a match. Defaults to None.

    Returns:
        dict: a dictionary of each matching key and its value

    """
    matches = {}
    if isinstance(obj, val_type) and isinstance(key, str) and key in keys:
        # Match found
        matches[key] = obj
    elif isinstance(obj, dict):
        # Recursively look for matches in each dictionary entry
        for key, val in obj.items():
            matches.update(find_values(val, keys, key, val_type))
    elif isinstance(obj, list):
        # Recursively look for matches in each list entry
        for item in obj:
            matches.update(find_values(item, keys, None, val_type))
    return matches


def get_test_list(tags):
    """Generate a list of tests and avocado tag filter from a list of tags.

    Args:
        tags (list): a list of tag or test file names

    Returns:
        (str, list): a tuple of the avacado tag filter and lists of tests

    """
    test_tags = []
    test_list = []
    for tag in tags:
        if ".py" in tag:
            # Assume '.py' indicates a test and just add it to the list
            test_list.append(tag)
        else:
            # Otherwise it is assumed that this is a tag
            test_tags.append(" --filter-by-tags={}".format(tag))

    # Add to the list of tests any test that matches the specified tags.  If no
    # tags and no specific tests have been specified then all of the functional
    # tests will be added.
    if test_tags or not test_list:
        command = " | ".join([
            "avocado list --paginator off{} ./".format(" ".join(test_tags)),
            r"sed -ne '/INSTRUMENTED/s/.* \([^:]*\):.*/\1/p'",
            "uniq"])
        test_list.extend(get_output(command).splitlines())

    return " ".join(test_tags), test_list


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


def replace_yaml_file(yaml_file, args, tmp_dir):
    """Replace the server/client yaml file placeholders.

    Replace any server or client yaml file placeholder names with the host
    names provided by the command line arguments in a copy of the original
    test yaml file.  If no replacements are specified return the original
    test yaml file.

    Args:
        yaml_file (str): test yaml file
        args (argparse.Namespace): command line arguments for this program
        tmp_dir (TemporaryDirectory): temporary directory object to use to
            write modified yaml files

    Returns:
        str: the test yaml file; None if the yaml file contains placeholders
            w/o replacements

    """
    if args.test_servers:
        # Determine which placeholder names need to be replaced in this yaml by
        # getting the lists of hosts specified in the yaml file
        unique_hosts = {"servers": set(), "clients": set()}
        for key, placeholders in find_yaml_hosts(yaml_file).items():
            if key in SERVER_KEYS:
                unique_hosts["servers"].update(placeholders)
            elif key in CLIENT_KEYS:
                # If no specific clients are specified use a specified server
                key = "clients" if args.test_clients else "servers"
                unique_hosts[key].update(placeholders)

        # Map the placeholder names to values provided by the user
        mapping_pairings = [("servers", args.test_servers.split(","))]
        if args.test_clients:
            mapping_pairings.append(("clients", args.test_clients.split(",")))
        mapping = {
            tmp: node_list[index] if index < len(node_list) else None
            for key, node_list in mapping_pairings
            for index, tmp in enumerate(sorted(unique_hosts[key]))}

        # Read in the contents of the yaml file to retain the !mux entries
        print("Reading {}".format(yaml_file))
        with open(yaml_file) as yaml_buffer:
            file_str = yaml_buffer.read()

        # Apply the placeholder replacements
        missing_replacements = []
        for placeholder, host in mapping.items():
            if host:
                # Replace the host entries with their mapped values
                file_str = re.sub(
                    "- {}".format(placeholder), "- {}".format(host), file_str)
            elif args.discard:
                # Discard any host entries without a replacement value
                file_str = re.sub(r"\s+- {}".format(placeholder), "", file_str)
            else:
                # Keep track of any placeholders without a replacement value
                missing_replacements.append(placeholder)

        if missing_replacements:
            # Report an error for all of the placeholders w/o a replacement
            print(
                "Error: Placeholders missing replacements in {}:\n  {}".format(
                    yaml_file, ", ".join(missing_replacements)))
            return None

        # Write the modified yaml file into a temporary file.  Use the path to
        # ensure unique yaml files for tests with the same filename.
        yaml_name = get_test_category(yaml_file)
        yaml_file = os.path.join(tmp_dir.name, "{}.yaml".format(yaml_name))
        print("Creating {}".format(yaml_file))
        with open(yaml_file, "w") as yaml_buffer:
            yaml_buffer.write(file_str)

    # Return the untouched or modified yaml file
    return yaml_file


def run_tests(test_files, tag_filter, args):
    """Run or display the test commands.

    Args:
        test_files (dict): a list of dictionaries of each test script/yaml file
        tag_filter (str): the avocado tag filter command line argument
        args (argparse.Namespace): command line arguments for this program

    Returns:
        int: a bitwise-or of all the return codes of each 'avocado run' command

    """
    return_code = 0

    # Determine the location of the avocado logs for archiving or renaming
    avocado_logs_dir = None
    if args.archive or args.rename:
        avocado_logs_dir = get_output(
            "avocado config | sed -ne '/logs_dir/s/.*  *//p'").strip()
        avocado_logs_dir = os.path.expanduser(avocado_logs_dir)
        print("Avocado logs stored in {}".format(avocado_logs_dir))

    # Create the base avocado run command
    command_list = [
        "avocado",
        "run",
        "--ignore-missing-references on",
        "--show-job-log" if not args.sparse else "",
        "--html-job-result on",
        tag_filter
    ]

    # Run each test
    for test_file in test_files:
        if isinstance(test_file["yaml"], str):
            # Optionally clean the log files before running this test on the
            # servers and clients specified for this test
            if args.clean:
                if not clean_logs(test_file["yaml"], args):
                    return 128

            # Execute this test
            test_command_list = list(command_list)
            test_command_list.append("--mux-yaml {}".format(test_file["yaml"]))
            test_command_list.append("-- {}".format(test_file["py"]))
            return_code |= time_command(
                " ".join([item for item in test_command_list if item != ""]))

            # Optionally store all of the doas server and client log files
            # along with the test results
            if args.archive:
                archive_logs(avocado_logs_dir, test_file["yaml"], args)

            # Optionally rename the test results directory for this test
            if args.rename:
                rename_logs(avocado_logs_dir, test_file["py"])
        else:
            # The test was not run due to an error replacing host placeholders
            # in the yaml file.  Treat this like a failed avocado command.
            return_code |= 4

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


def get_log_files(config_yaml, daos_files=None):
    """Get a list of DAOS files used by the specified yaml file.

    Args:
        config_yaml (str): yaml file defining log file locations
        daos_files (dict, optional): dictionary of default DAOS log files whose
            keys define which yaml log parameters to use to update the default
            values. Defaults to None.

    Returns:
        dict: a dictionary of DAOS file name keys and full path values

    """
    # List of default DAOS files
    if daos_files is None:
        daos_core_test_dir = os.path.split(
            os.getenv("D_LOG_FILE", "/tmp/server.log"))[0]
        daos_files = {
            "log_file": "/tmp/server.log",
            "admin_log_file": "/tmp/daos_admin.log",
            "server_log_file": "/tmp/server.log",
            "agent_log_file": "/tmp/daos_agent.log",
            "control_log_file": "/tmp/daos_control.log",
            "helper_log_file": "/tmp/daos_admin.log",
            "socket_dir": "/tmp/daos_sockets",
            "debug_log_default": os.getenv("D_LOG_FILE", "/tmp/daos.log"),
            "test_variant_client_logs":
                "{}/*_client_daos.log".format(daos_core_test_dir),
            "test_variant_server_logs":
                "{}/*_server_daos.log".format(daos_core_test_dir),
        }

    # Determine the log file locations defined by the last run test
    print("Checking {} for daos log file locations".format(config_yaml))
    yaml_data = get_yaml_data(config_yaml)

    # Replace any default log file with its yaml definition
    matches = find_values(yaml_data, daos_files.keys(), val_type=str)
    for key, value in matches.items():
        if value != daos_files[key]:
            print(
                "  Update found for {}: {} -> {}".format(
                    key, daos_files[key], value))
            daos_files[key] = value

    return daos_files


def find_yaml_hosts(test_yaml):
    """Find the all the host values in the specified yaml file.

    Args:
        test_yaml (str): test yaml file

    Returns:
        dict: a dictionary of each host key and its host values

    """
    return find_values(get_yaml_data(test_yaml), SERVER_KEYS + CLIENT_KEYS)


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
        if key in CLIENT_KEYS:
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
    # Use the default server yaml and then the test yaml to update the default
    # DAOS log file locations.  This should simulate how the test defines which
    # log files it will use when it is run.
    log_files = get_log_files(test_yaml, get_log_files(BASE_LOG_FILE_YAML))
    host_list = get_hosts_from_yaml(test_yaml, args)
    command = "sudo rm -fr {}".format(" ".join(log_files.values()))
    print("Cleaning logs on {}".format(host_list))
    if not spawn_commands(host_list, command):
        print("Error cleaning logs, aborting")
        return False

    return True


def archive_logs(avocado_logs_dir, test_yaml, args):
    """Copy all of the host test log files to the avocado results directory.

    Args:
        avocado_logs_dir ([type]): [description]
        test_yaml (str): yaml file containing host names
        args (argparse.Namespace): command line arguments for this program
    """
    this_host = socket.gethostname().split(".")[0]
    log_files = get_log_files(
        os.path.join(get_temporary_directory(), TEST_DAOS_SERVER_YAML))
    host_list = get_hosts_from_yaml(test_yaml, args)
    doas_logs_dir = os.path.join(avocado_logs_dir, "latest", "daos_logs")

    # Create a subdirectory in the avocado logs directory for this test
    print("Archiving host logs from {} in {}".format(host_list, doas_logs_dir))
    get_output("mkdir {}".format(doas_logs_dir))

    # Create a list of log files that are not directories
    non_dir_files = [
        log_file for log_file in log_files.values()
        if os.path.splitext(os.path.basename(log_file))[1] != ""]

    # Copy any log files that exist on the test hosts and remove them from the
    # test host if the copy is successful.  Attempt all of the commands and
    # report status at the end of the loop.  Include a listing of the file
    # related to any failed command.
    commands = [
        "set -eu",
        "rc=0",
        "copied=()",
        "for file in {}".format(" ".join(non_dir_files)),
        "do if [ -e $file ]",
        "then if scp $file {}:{}/${{file##*/}}-$(hostname -s)".format(
            this_host, doas_logs_dir),
        "then copied+=($file)",
        "if ! sudo rm -fr $file",
        "then ((rc++))",
        "ls -al $file",
        "fi",
        "else ((rc++))",
        "ls -al $file",
        "fi",
        "fi",
        "done",
        "echo Copied ${copied[@]:-no files}",
        "exit $rc",
    ]
    spawn_commands(host_list, "; ".join(commands))


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
        "-r", "--rename",
        action="store_true",
        help="rename the avocado test logs directory to include the test name")
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
        "-ts", "--test_servers",
        action="store",
        help="comma-separated list of hosts to use as replacement values for "
             "server placeholders in each test's yaml file.  If the "
             "'--test_clients' argument is not specified, this list of hosts "
             "will also be used to replace client placeholders.")
    args = parser.parse_args()
    print("Arguments: {}".format(args))

    # Setup the user environment
    set_test_environment()

    # Process the tags argument to determine which tests to run
    tag_filter, test_list = get_test_list(args.tags)

    # Verify at least one test was requested
    if len(test_list) == 0:
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
        if status & 128 == 128:
            print("ERROR: Failed to clean logs in preparation for test run!")
            ret_code = 1
    exit(ret_code)


if __name__ == "__main__":
    main()
