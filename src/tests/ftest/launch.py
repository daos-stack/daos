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

import argparse
import json
import os
import re
import subprocess
import time
import yaml


def display(message, debug=None):
    """Display messages that are either non-debug or when debug is enabled.

    Only print the specified message if either a debug flag is not provided
    or the debug flag is set.

    Args:
        message (str): mesage to print
        debug (bool|None): if specified only print the message if set to True

    Returns:
        None

    """
    if not isinstance(debug, bool) or debug:
        print(message)


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
    """Execute the command on this host and return its duration.

    Args:
        cmd (str): command to time

    Returns:
        int: duration of the command in seconds

    """
    print("Running {}".format(cmd))
    start_time = int(time.time())
    subprocess.call(cmd, shell=True)
    end_time = int(time.time())
    return end_time - start_time


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
    bin_dir = build_vars['PREFIX'] + '/bin'
    sbin_dir = build_vars['PREFIX'] + '/sbin'
    path = os.environ.get('PATH')
    os.environ['PATH'] = bin_dir + ':' + sbin_dir + ':' + path
    os.environ['DAOS_SINGLETON_CLI'] = "1"
    os.environ['CRT_CTX_SHARE_ADDR'] = "1"
    os.environ['CRT_ATTACH_INFO_PATH'] = build_vars['PREFIX'] + '/tmp'


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
        with open(yaml_file, "r") as file:
            try:
                filedata = file.read()
                yaml_data = yaml.safe_load(filedata.replace("!mux", ""))
            except yaml.YAMLError as error:
                raise Exception(
                    "Error reading {}: {}".format(yaml_file, error))
    return yaml_data


def clean_test_environment(machines, debug):
    """Clean the daos test environment on each specified machine.

    Args:
        machines (list): a list of machines to clean
        debug (bool): whether or not to dispaly debug messages

    Returns:
        None

    """
    # List of processes to kill on each daos server/client if active
    daos_process_names = [
        "orterun",
        "orted",
        "daos_server",
        "daos_io_server",
        "aexpect-helper-2.7",
        "avocado",
        "aexpect-helper-",
        "daos_agent",
    ]

    # List of files to remove on each daos server/client
    daos_files = {
        "server_log": "/tmp/server.log",
        "agent_log": "/tmp/daos_agent.log",
        "control_log": "/tmp/daos_control.log",
        "sockets_dir": "/tmp/daos_sockets",
    }

    # Replace any default log files/dirs with yaml definitions
    yaml_data = get_yaml_data("./data/daos_server_baseline.yaml")
    display("DAOS server yaml data: {}".format(yaml_data), debug)
    if isinstance(yaml_data, dict):
        for key, value in yaml_data.items():
            if isinstance(value, dict):
                for key2, value2 in yaml_data[key].items():
                    if key2 == "log_file":
                        daos_files["server_log"] = value2
            elif key == "control_log_file":
                daos_files["control_log"] = value
            elif key == "sockets_dir":
                daos_files["sockets_dir"] = value

    clean_commands = [
        "killall -9 {}".format(" ".join(daos_process_names)),
        "rm -rf /mnt/daos/*",
    ]
    clean_commands.extend(
        ["rm -rf {}".format(log) for log in daos_files.values()])
    for machine in machines:
        if len(machine) > 0:
            display("Cleaning test environment on {}".format(machine))
            output = get_output("ssh {} \"{}\"".format(
                machine, "; ".join(clean_commands)))
            display("  Output: {}".format(output), debug)


def get_test_list(tags):
    """Generate a list of tests and avocado tag filter from a list of tags.

    Args:
        tags (list): a list of tag or test file names

    Returns:
        (str, list): a tuple of the avacado tag filter and lists of tests

    """
    tag_filter = ""
    test_list = []
    for tag in tags:
        if ".py" in tag:
            # Assume '.py' indicates a test and just add it to the list
            test_list.append(tag)
        elif tag == "all":
            # Use an empty filter to find all tests
            tag_filter = " "
            break
        else:
            # Otherwise it is assumed that this is a tag
            tag_filter += " --filter-by-tags={}".format(tag)

    if not tag_filter == "":
        # Find all the tests that match the tags
        command = "avocado list --paginator off{} ./ | " \
                  "grep '^INSTRUMENTED' | awk '{{print $2}}' | " \
                  "awk -F ':' '{{print $1}}' | uniq".format(tag_filter)
        test_list.extend(get_output(command).splitlines())

    return tag_filter, test_list


def get_test_files(test_list, machine_mapping, debug):
    """Get a list of the test scripts to run and their yaml files.

    Args:
        test_list (list): list of test scripts to run
        machine_mapping (dict): the mapping of machine replacement keys to
            their replacement values
        debug (bool): whether or not to dispaly debug messages

    Returns:
        list: a list of dictionaries of each test script and yaml file

    """
    test_files = [{"py": test, "yaml": None} for test in test_list]
    home_tmp = os.path.join(os.path.expanduser("~"), "tmp")
    get_output("mkdir -p {}".format(home_tmp))
    for test_file in test_files:
        base, _ = os.path.splitext(test_file["py"])
        file = os.path.basename(base)
        if os.path.exists("{}.yaml".format(base)):
            # Read the yaml file
            display("Reading {}".format("{}.yaml".format(base)), debug)
            with open("{}.yaml".format(base), "r") as f:
                data = f.readlines()

            # Replace the 'boro-[A-Z]' names with the specified machine names.
            # Exclude any machine name placeholders that do not have a mapping.
            tmp_yaml = os.path.join(home_tmp, "{}.yaml".format(file))
            display("Creating {}".format(tmp_yaml), debug)
            with open(tmp_yaml, "w") as f:
                for line in data:
                    match = re.findall(r"(.*- )(boro-[A-Z])", line)
                    if len(match) > 0:
                        replace_machine = match[0][1]
                        display(
                            "    MATCH: {} -> {}".format(
                                line.rstrip(), replace_machine),
                            debug)
                        if replace_machine in machine_mapping.keys():
                            new_line = "{}{}\n".format(
                                match[0][0], machine_mapping[replace_machine])
                            display(
                                "  Line: {}".format(new_line.rstrip()),
                                debug)
                            f.write(new_line)
                    else:
                        display("  Line: {}".format(line.rstrip()), debug)
                        f.write(line)
            test_file["yaml"] = tmp_yaml

        else:
            display(
                "ERROR: No yaml file exists for {}; excluding".format(
                    test_file["py"]))
            test_file["yaml"] = None
    return test_files


def map_machines(machines):
    """Sequentially map each specified machine name to a replacement key.

    Args:
        machines (list): list of machine names to map to replacement keys

    Retrurns:
        dict: a dictionary of machines indexed by their repalcement key

    """
    machine_mapping = {}
    for x, machine in enumerate(machines):
        if len(machine) > 0:
            # Sequentially map each specified machine name to a replacement
            # key:
            #   boro-[A-Z]
            ascii_upper_code = 65 + x
            if ascii_upper_code <= 91:
                alpha_name = "boro-{}".format(chr(ascii_upper_code))
                machine_mapping[alpha_name] = machine
            else:
                display(
                    "WARNING: Unused machine name specified ({}): {}".format(
                        ascii_upper_code, machine))
    return machine_mapping


def run_tests(test_files, sparse, tag_filter, prepare, rename):
    """Run or display the test commands.

    Args:
        test_files (dict): a list of dictionaries of each test script/yaml file
        sparse (bool):
        tag_filter (str):
        prepare (bool): if True dispaly the test command instead of running it
        rename (bool): rename the avocado test results directory to incude the
            test script name

    Returns:
        None

    """
    avocado_logs_dir = None
    if rename:
        avocado_logs_dir = get_output(
            "avocado config | grep logs_dir | awk '{print $2}'").strip()
        avocado_logs_dir = os.path.expanduser(avocado_logs_dir)
    command = "avocado run{}{}{}{}".format(
        " --ignore-missing-references on",
        " --show-job-log" if not sparse else "",
        " --html-job-result on",
        tag_filter)
    for test_file in test_files:
        if isinstance(test_file["yaml"], str):
            test_cmd = "{}  --mux-yaml {} -- {}".format(
                command, test_file["yaml"], test_file["py"])
            if prepare:
                # Only display the avocado commands for tests
                display("Prepared: {}".format(test_cmd))
            else:
                # Execute and report on the run time of the test
                display("Total test time: {}s".format(time_command(test_cmd)))

                if rename:
                    test_name = os.path.splitext(
                        os.path.basename(test_file["py"]))[0]
                    test_logs_dir = os.path.realpath(
                        os.path.join(avocado_logs_dir, "latest"))
                    new_test_logs_dir = "{}-{}".format(
                        test_logs_dir, test_name)
                    try:
                        os.rename(test_logs_dir, new_test_logs_dir)
                        display(
                            "Renamed {} to {}".format(
                                test_logs_dir, new_test_logs_dir))
                    except OSError as error:
                        display(
                            "Error renaming {} to {}: {}".format(
                                test_logs_dir, new_test_logs_dir, error))


def main():
    """Launch the tests."""
    # Parse the command line arguments
    description = [
        "DAOS functional test launcher",
        "",
        "Launches tests by specifying a category.  For example:",
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
        "Tests can also be launched by name or all tests can be launched via "
        "the keyword 'all'",
        "",
        "You can also specify the sparse flag -s to limit output to "
        "pass/fail.",
        "\tExample command: launch.py -s pool"
    ]
    parser = argparse.ArgumentParser(
        prog="launcher.py",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="\n".join(description))
    parser.add_argument(
        "-c", "--clean",
        action="store_true",
        help="clean the daos environment on the specified hosts")
    parser.add_argument(
        "-d", "--debug",
        action="store_true",
        help="display debug messages")
    parser.add_argument(
        "-l", "--list",
        action="store_true",
        help="list the python scripts that match the specified tags; do not "
             "prepare yaml files or execute tests")
    parser.add_argument(
        "-m", "--machines",
        action="store",
        type=str,
        help="comma-separated list of machines to use in the test yaml files",
        default="")
    parser.add_argument(
        "-p", "--prepare",
        action="store_true",
        help="prepare yaml files and commands but do not execute tests")
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
        nargs="+",
        type=str,
        help="test category or file to run")
    args = parser.parse_args()
    display("Arguments: {}".format(args), args.debug)

    # Clean the daos test environment prior to running any tests
    if args.clean:
        clean_test_environment(args.machines.split(","), args.debug)

    # Process the tags argument to determine which tests to run
    tag_filter, test_list = get_test_list(args.tags)

    # Verify at least one test was requested
    if len(test_list) == 0:
        display("ERROR: No tests or tags found via {}".format(args.tags))
        exit(1)
    else:
        display(
            "Detected tests:  \n{}".format("  \n".join(test_list)),
            None if args.list else args.debug)
        if args.list:
            exit(0)

    # Map the specified list of machines to boro-[A-Z] replacement names
    machine_mapping = map_machines(args.machines.split(","))
    display("Machine name mapping: {}".format(machine_mapping), args.debug)

    # Setup the user environment
    set_test_environment()

    # Create temporary copies of each yaml file that use the specified hosts
    test_files = get_test_files(test_list, machine_mapping, args.debug)

    # Run all the tests with their yaml files
    run_tests(test_files, args.sparse, tag_filter, args.prepare, args.rename)


if __name__ == "__main__":
    main()
