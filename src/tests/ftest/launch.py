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
from multiprocessing import Process
import os
import socket
import subprocess
import time
import yaml


TEST_LOG_FILE_YAML = "./data/daos_avocado_test.yaml"
BASE_LOG_FILE_YAML = "./data/daos_server_baseline.yaml"


def get_build_environment():
    """Obtain DAOS build environment variables from the .build_vars.json file.

    Returns:
        dict: a dictionary of DAOS build environment variable names and values

    """
    build_vars_file = os.path.join(
        os.path.dirname(os.path.realpath(__file__)),
        "../../../.build_vars.json")
    with open(build_vars_file) as vars_file:
        return json.load(vars_file)


def set_test_environment():
    """Set up the test environment.

    Returns:
        None

    """
    build_vars = get_build_environment()
    bin_dir = build_vars["PREFIX"] + "/bin"
    sbin_dir = build_vars["PREFIX"] + "/sbin"
    path = os.environ.get("PATH")
    os.environ["PATH"] = ":".join([bin_dir, sbin_dir, path])
    os.environ["DAOS_SINGLETON_CLI"] = "1"
    os.environ["CRT_CTX_SHARE_ADDR"] = "1"
    os.environ["CRT_ATTACH_INFO_PATH"] = build_vars["PREFIX"] + "/tmp"

    # Verify the PYTHONPATH env is set
    python_path = os.environ.get("PYTHONPATH")
    required_python_paths = [
        os.path.abspath("util/apricot"),
        os.path.abspath("util"),
        os.path.abspath("../../utils/py"),
    ]
    if python_path is None or python_path == "":
        os.environ["PYTHONPATH"] = ":".join(required_python_paths)
    else:
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

    """
    # Create a job to run the command on each host
    jobs = []
    for host in host_list:
        host_command = command.format(host=host)
        jobs.append(Process(target=get_output, args=(host_command,)))

    # Start the background jobs
    for job in jobs:
        job.start()

    # Block until each job is complete or the overall timeout is reached
    elapsed = 0
    for job in jobs:
        # Update the join timeout to account for previously joined jobs
        join_timeout = timeout - elapsed if elapsed <= timeout else 0

        # Block until this job completes or the overall timeout is reached
        start_time = int(time.time())
        job.join(join_timeout)

        # Update the amount of time that has elapsed since waiting for jobs
        elapsed += int(time.time()) - start_time

    # Terminate any processes that may still be running after timeout
    for job in jobs:
        if job.is_alive():
            print("Terminating job {}".format(job.pid))
            job.terminate()


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

    # Find all the tests that match the tags.  If no tags are specified all of
    # the tests will be found.
    command = " | ".join([
        "avocado list --paginator off{} ./".format(" ".join(test_tags)),
        r"sed -ne '/INSTRUMENTED/s/.* \([^:]*\):.*/\1/p'",
        "uniq"])
    test_list.extend(get_output(command).splitlines())

    return " ".join(test_tags), test_list


def get_test_files(test_list):
    """Get a list of the test scripts to run and their yaml files.

    Args:
        test_list (list): list of test scripts to run

    Returns:
        list: a list of dictionaries of each test script and yaml file

    """
    test_files = [{"py": test, "yaml": None} for test in test_list]
    for test_file in test_files:
        base, _ = os.path.splitext(test_file["py"])
        test_file["yaml"] = "{}.yaml".format(base)

    return test_files


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
                clean_logs(test_file["yaml"])

            # Execute this test
            test_command_list = list(command_list)
            test_command_list.append("--mux-yaml {}".format(test_file["yaml"]))
            test_command_list.append("-- {}".format(test_file["py"]))
            return_code |= time_command(
                " ".join([item for item in test_command_list if item != ""]))

            # Optionally store all of the doas server and client log files
            # along with the test results
            if args.archive:
                archive_logs(avocado_logs_dir, test_file["yaml"])

            # Optionally reanme the test results directory for this test
            if args.rename:
                rename_logs(avocado_logs_dir, test_file["py"])

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
        env_log = os.getenv("D_LOG_FILE", "/tmp/daos.log")
        daos_files = {
            "log_file": "/tmp/server.log",
            "agent_log_file": "/tmp/daos_agent.log",
            "control_log_file": "/tmp/daos_control.log",
            "socket_dir": "/tmp/daos_sockets",
            "daos_log_file": env_log,
            "daos_core_test_logs": "{}/*_{}".format(*os.path.split(env_log))
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


def get_hosts_from_yaml(test_yaml):
    """Extract the list of hosts from the test yaml file.

    This host will be included in the list if no clients are explicitly called
    out in the test's yaml file.

    Args:
        test_yaml (str): test yaml file

    Returns:
        list: a unique list of hosts specified in the test's yaml file

    """
    yaml_data = get_yaml_data(test_yaml)
    server_keys = [
        "test_machines",
        "test_servers",
        "daos_servers",
        "servers",
        "test_machines1",
        "test_machines2",
        "test_machines2a",
        "test_machines3",
        "test_machines6"]
    client_keys = [
        "test_clients",
        "clients",
    ]
    host_set = set()
    found_client_key = False
    matches = find_values(yaml_data, server_keys + client_keys)
    for key, value in matches.items():
        host_set.update(value)
        if key in client_keys:
            found_client_key = True

    # Include this host as a client if no clients are specified
    if not found_client_key:
        host_set.add(socket.gethostname().split(".")[0])

    return sorted(list(host_set))


def clean_logs(test_yaml):
    """Remove the test log files on each test host.

    Args:
        test_yaml (str): yaml file containing host names
    """
    # Use the default server yaml and then the test yaml to update the default
    # DAOS log file locations.  This should simulate how the test defines which
    # log files it will use when it is run.
    log_files = get_log_files(test_yaml, get_log_files(BASE_LOG_FILE_YAML))
    host_list = get_hosts_from_yaml(test_yaml)
    command = "ssh {{host}} \"rm -fr {}\"".format(" ".join(log_files.values()))
    print("Cleaning logs on {}".format(host_list))
    spawn_commands(host_list, command)


def archive_logs(avocado_logs_dir, test_yaml):
    """Copy all of the host test log files to the avocado results directory.

    Args:
        avocado_logs_dir ([type]): [description]
        test_yaml (str): yaml file containing host names
    """
    this_host = socket.gethostname().split(".")[0]
    log_files = get_log_files(TEST_LOG_FILE_YAML)
    host_list = get_hosts_from_yaml(test_yaml)
    doas_logs_dir = os.path.join(avocado_logs_dir, "latest", "daos_logs")

    # Create a subdirectory in the avocado logs directory for this test
    print("Archiving host logs from {} in {}".format(host_list, doas_logs_dir))
    get_output("mkdir {}".format(doas_logs_dir))

    # Create a list of log files that are not directories
    non_dir_files = [
        log_file for log_file in log_files.values()
        if os.path.splitext(os.path.basename(log_file))[1] != ""]

    # Copy any log files that exist on the test hosts and remove them from the
    # test host if the copy is successful
    command = "ssh {{host}} 'set -e; {}'".format(
        "for file in {}; do if [ -e $file ]; then "
        "scp $file {}:{}/${{{{file##*/}}}}-{{host}} && rm -fr $file; fi; "
        "done".format(" ".join(non_dir_files), this_host, doas_logs_dir))
    spawn_commands(host_list, command)


def rename_logs(avocado_logs_dir, test_file):
    """Append the test name to its avocado job-results directory name.

    Args:
        avocado_logs_dir (str): avocado job-results directory
        test_file (str): the test python file
    """
    test_name = os.path.splitext(os.path.basename(test_file))[0]
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

    # Create a dictioanry of test and their yaml files
    test_files = get_test_files(test_list)

    # Run all the tests
    exit(run_tests(test_files, tag_filter, args))


if __name__ == "__main__":
    main()
