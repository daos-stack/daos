"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import subprocess  # nosec

import yaml


# Storage-related methods
def format_storage(host_list):
    """Call dmg storage format.

    Args:
        host_list (str): List of hosts to format.
    """
    format_cmd = ["dmg", "storage", "format", "--host-list=" + host_list]
    run_command(command=format_cmd)


def storage_query_usage(host_list):
    """Call dmg storage query usage.

    Args:
        host_list (str): List of hosts to query.
    """
    storage_query_cmd = ["dmg", "storage", "query", "usage", "--host-list=" + host_list]
    run_command(command=storage_query_cmd)


# Pool-related methods
def create_pool(pool_size, pool_label, ranks=None, nsvc=None):
    """Call dmg pool create.

    Args:
        pool_size (str): Pool size.
        pool_label (str): Pool label.
        ranks (str): Ranks to create pool. Defaults to None.
        nsvc (str): Number of service replicas. Defaults to None.
    """
    create_pool_cmd = ["dmg", "pool", "create", pool_label, "--size=" + pool_size]
    if ranks:
        create_pool_cmd.append("--ranks=" + ranks)
    if nsvc:
        create_pool_cmd.append("--nsvc=" + nsvc)
    run_command(command=create_pool_cmd)


def list_pool(verbose=False, json=False, no_query=False):
    """Call dmg pool list.

    Args:
        verbose (bool): Whether to use --verbose. Defaults to False.
        json (bool): Whether to use --json. If used, verbose value would be irrelevant.
            Defaults to False.
        no_query (bool): Whether to use --no-query. Defaults to False.

    Returns:
        str: If --json is used, return stdout. Otherwise None.

    """
    list_pool_cmd = ["dmg", "pool", "list"]
    if json:
        list_pool_cmd.append("--json")
    if verbose:
        list_pool_cmd.append("--verbose")
    if no_query:
        list_pool_cmd.append("--no-query")
    command = " ".join(list_pool_cmd)
    print(f"Command: {command}")

    if json:
        result = subprocess.run(
            list_pool_cmd, stdout=subprocess.PIPE, universal_newlines=True, check=False)
        return result.stdout

    subprocess.run(list_pool_cmd, check=False)
    return None


def pool_get_prop(pool_label, properties):
    """Call dmg pool get-prop <pool_label> <properties>

    Args:
        pool_label (str): Pool label.
        properties (str): Properties to query. Separate them with comma if there are
            multiple properties.
    """
    get_prop_cmd = ["dmg", "pool", "get-prop", pool_label, properties]
    run_command(command=get_prop_cmd)


def pool_query(pool_label):
    """Call dmg pool query

    Args:
        pool_label (str): Pool label.
    """
    pool_query_cmd = ["dmg", "pool", "query", pool_label]
    run_command(command=pool_query_cmd)


# Container-related methods
def create_container(pool_label, cont_label):
    """Call daos container create.

    Args:
        pool_label (str): Pool label.
        cont_label (str): Container label.
    """
    cont_create_cmd = ["daos", "container", "create", pool_label, cont_label]
    run_command(command=cont_create_cmd)


def cont_get_prop(pool_label, cont_label, properties=None):
    """Call daos container get-prop <pool_label> <cont_label> <properties>

    Args:
        pool_label (str): Pool label.
        cont_label (str): Container label.
        properties (str): Properties to query. Separate them with comma if there are
            multiple properties. Defaults to None.
    """
    get_prop_cmd = ["daos", "container", "get-prop", pool_label, cont_label]
    if properties:
        get_prop_cmd.append("--properties=" + properties)
    run_command(command=get_prop_cmd)


# Fault-related methods
def inject_fault_mgmt(pool_label, fault_type):
    """Call dmg faults mgmt-svc to inject fault.

    Args:
        pool_label (str): Pool label.
        fault_type (str): Fault type.
    """
    inject_fault_cmd = ["dmg", "faults", "mgmt-svc", "pool", pool_label, fault_type]
    run_command(command=inject_fault_cmd)


def inject_fault_pool(pool_label, fault_type):
    """Call dmg faults pool-svc to inject fault.

    Args:
        pool_label (str): Pool label.
        fault_type (str): Fault type.
    """
    inject_fault_cmd = ["dmg", "faults", "pool-svc", pool_label, fault_type]
    run_command(command=inject_fault_cmd)


def inject_fault_daos(pool_label, cont_label, fault_type):
    """Call daos faults to inject fault.

    Args:
        pool_label (str): Pool label.
        cont_label (str): Container label.
        fault_type (str): Fault type.
    """
    location = "--location=" + fault_type
    inject_fault_cmd = ["daos", "faults", "container", pool_label, cont_label, location]
    run_command(command=inject_fault_cmd)


# Check-related methods
def check_enable():
    """Call dmg check enable"""
    check_enable_cmd = ["dmg", "check", "enable"]
    run_command(command=check_enable_cmd)


def check_set_policy(reset_defaults=False, all_interactive=False):
    """Call dmg check set-policy with --reset-defaults or --all-interactive.

    Args:
        reset_defaults (bool): Set all policies to their default action. Defaults to
            False.
        all_interactive (bool): Set all policies to interactive. Defaults to False.
    """
    if reset_defaults != all_interactive:
        check_set_policy_cmd = ["dmg", "check", "set-policy"]
        if reset_defaults:
            check_set_policy_cmd.append("--reset-defaults")
        if all_interactive:
            check_set_policy_cmd.append("--all-interactive")
        run_command(command=check_set_policy_cmd)


def check_start(policies=None):
    """Call dmg check start

    Args:
        policies (str): Repair policies such as POOL_BAD_LABEL:CIA_INTERACT
    """
    check_start_cmd = ["dmg", "check", "start"]
    if policies:
        check_start_cmd.extend(["-p", policies])
    run_command(command=check_start_cmd)


def check_query(json=False):
    """Call dmg check query

    Args:
        json (bool): Whether to use --json. Defaults to False.

    Returns:
        str: If --json is used, return stdout. Otherwise None.

    """
    if json:
        check_query_cmd = ["dmg", "--json", "check", "query"]
    else:
        check_query_cmd = ["dmg", "check", "query"]
    command = " ".join(check_query_cmd)
    print(f"Command: {command}")

    if json:
        result = subprocess.run(
            check_query_cmd, stdout=subprocess.PIPE, universal_newlines=True, check=False)
        return result.stdout

    subprocess.run(check_query_cmd, check=False)
    return None


def check_disable():
    """Call dmg check disable"""
    check_disable_cmd = ["dmg", "check", "disable"]
    run_command(command=check_disable_cmd)


def repeat_check_query():
    """Allow user to repeatedly call dmg check query."""
    while True:
        user_input = input("Hit y to query, n to proceed to next step: ")
        if user_input == "y":
            check_query()
        elif user_input == "n":
            break
        else:
            print("Please enter y or n.")


def check_repair(sequence_num, action):
    """Call dmg check repair

    Args:
        sequence_num (str): Sequence number for repair action.
        action (str): Repair action number.
    """
    check_repair_cmd = ["dmg", "check", "repair", sequence_num, action]
    run_command(command=check_repair_cmd)


# System-related methods
def system_stop(force=False):
    """Stop servers.

    Args:
        force (bool): Whether to use --force. Defaults to None.
    """
    system_stop_cmd = ["dmg", "system", "stop"]
    if force:
        system_stop_cmd.append("--force")
    run_command(command=system_stop_cmd)


def system_start():
    """Start servers."""
    system_start_cmd = ["dmg", "system", "start"]
    run_command(command=system_start_cmd)


def system_query(json=False, verbose=False):
    """Call dmg system query

    Args:
        json (bool): Whether to use --json. Defaults to False.
        verbose (bool): Whether to use --verbose. Defaults to False.

    Returns:
        str: Command output.

    """
    if json:
        system_query_cmd = ["dmg", "--json", "system", "query"]
    else:
        system_query_cmd = ["dmg", "system", "query"]
    if verbose:
        system_query_cmd.append("--verbose")
    command = " ".join(system_query_cmd)
    print(f"Command: {command}")

    if json:
        result = subprocess.run(
            system_query_cmd, stdout=subprocess.PIPE, universal_newlines=True,
            check=False)
        return result.stdout

    subprocess.run(system_query_cmd, check=False)
    return None


# Utility methods
def create_uuid_to_seqnum():
    """Create pool UUID to sequence number mapping.

    Returns:
        dict: UUID to sequence number mapping for each pool. Sequence number will be used
            during repair.

    """
    uuid_to_seqnum = {}
    stdout = check_query(json=True)
    generated_yaml = yaml.safe_load(stdout)
    for report in generated_yaml["response"]["reports"]:
        uuid_to_seqnum[report["pool_uuid"]] = report["seq"]

    return uuid_to_seqnum


def create_label_to_uuid():
    """Create label to UUID mapping.

    Returns:
        dict: Pool label to UUID.

    """
    label_to_uuid = {}
    stdout = list_pool(json=True)
    generated_yaml = yaml.safe_load(stdout)
    for pool in generated_yaml["response"]["pools"]:
        label_to_uuid[pool["label"]] = pool["uuid"]

    return label_to_uuid


def get_current_labels():
    """Get current pool labels from MS.

    Returns:
        list: Current pool labels.

    """
    pool_labels = []
    stdout = list_pool(json=True)
    generated_yaml = yaml.safe_load(stdout)
    for pool in generated_yaml["response"]["pools"]:
        pool_labels.append(pool["label"])

    return pool_labels


def convert_list_to_str(original_list, separator):
    """Convert given list to a string with each item separated by separator.

    Args:
        original_list (list): List of items.
        separator (str): Separator to separate each item in the new string list.

    Returns:
        str: String list.

    """
    return separator.join(map(str, original_list))


def run_command(command):
    """Print given command and run.

    Args:
        command (list): List of characters that make up the command.
    """
    cmd_str = " ".join(command)
    print(f"Command: {cmd_str}")
    subprocess.run(command, check=False)
