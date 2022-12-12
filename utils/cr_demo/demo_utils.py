"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import subprocess
import yaml


# Storage-related methods
def format_storage(host_list):
    """Call dmg storage format.

    Args:
        host_list (str): List of hosts to format.
    """
    format_cmd = ["dmg", "storage", "format", "--host-list=" + host_list]
    command = " ".join(format_cmd)
    print(f"Command: {command}")
    subprocess.run(format_cmd, check=False)

def storage_query_usage(host_list):
    """Call dmg storage query usage.

    Args:
        host_list (str): List of hosts to query.
    """
    format_cmd = ["dmg", "storage", "query", "usage", "--host-list=" + host_list]
    command = " ".join(format_cmd)
    print(f"Command: {command}")
    subprocess.run(format_cmd, check=False)

# Pool-related methods
def create_pool(pool_size, pool_label, ranks=None, nsvc=None):
    """Call dmg pool create.

    Args:
        pool_size (str): Pool size.
        pool_label (str): Pool label.
        ranks (str): Ranks to create pool. Defaults to None.
        nsvc (str): Number of service replicas. Defaults to None.
    """
    create_pool_cmd = ["dmg", "pool", "create", "--size=" + pool_size,
                       "--label=" + pool_label]
    if ranks:
        create_pool_cmd.append("--ranks=" + ranks)
    if nsvc:
        create_pool_cmd.append("--nsvc=" + nsvc)
    command = " ".join(create_pool_cmd)
    print(f"Command: {command}")
    subprocess.run(create_pool_cmd, check=False)

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
    if json:
        list_pool_cmd = ["dmg", "--json", "pool", "list"]
    else:
        list_pool_cmd = ["dmg", "pool", "list"]
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
    command = " ".join(get_prop_cmd)
    print(f"Command: {command}")
    subprocess.run(get_prop_cmd, check=False)

def list_containers(pool_label):
    """Call daos pool list-containers <pool_label>

    Args:
        pool_label (str): Pool label.
    """
    list_containers_cmd = ["daos", "pool", "label", pool_label]
    command = " ".join(list_containers_cmd)
    print(f"Command: {command}")
    subprocess.run(list_containers_cmd, check=False)

# Container-related methods
def create_container(pool_label, cont_label):
    """Call daos container create.

    Args:
        pool_label (str): Pool label.
        cont_label (str): Container label.
    """
    cont_create_cmd = ["daos", "container", "create", pool_label, cont_label]
    command = " ".join(cont_create_cmd)
    print(f"Command: {command}")
    subprocess.run(cont_create_cmd, check=False)

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
    command = " ".join(get_prop_cmd)
    print(f"Command: {command}")
    subprocess.run(get_prop_cmd, check=False)

# Fault-related methods
def inject_fault_mgmt(pool_label, fault_type):
    """Call dmg faults mgmt-svc to inject fault.

    Args:
        pool_label (str): Pool label.
        fault_type (str): Fault type.
    """
    inject_fault_cmd = ["dmg", "faults", "mgmt-svc", "pool", pool_label, fault_type]
    command = " ".join(inject_fault_cmd)
    print(f"Command: {command}")
    subprocess.run(inject_fault_cmd, check=False)

def inject_fault_pool(pool_label, fault_type):
    """Call dmg faults pool-svc to inject fault.

    Args:
        pool_label (str): Pool label.
        fault_type (str): Fault type.
    """
    inject_fault_cmd = ["dmg", "faults", "pool-svc", pool_label, fault_type]
    command = " ".join(inject_fault_cmd)
    print(f"Command: {command}")
    subprocess.run(inject_fault_cmd, check=False)

def inject_fault_daos(pool_label, cont_label, fault_type):
    """Call daos faults to inject fault.

    Args:
        fault_type (str): Fault type.
    """
    location = "--location=" + fault_type
    inject_fault_cmd = ["daos", "faults", "container", pool_label, cont_label, location]
    command = " ".join(inject_fault_cmd)
    print(f"Command: {command}")
    subprocess.run(inject_fault_cmd, check=False)

# Check-related methods
def check_enable():
    """Call dmg check enable"""
    check_enable_cmd = ["dmg", "check", "enable"]
    command = " ".join(check_enable_cmd)
    print(f"Command: {command}")
    subprocess.run(check_enable_cmd, check=False)

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
        command = " ".join(check_set_policy_cmd)
        print(f"Command: {command}")
        subprocess.run(check_set_policy_cmd, check=False)

def check_start(policies=None):
    """Call dmg check start

    Args:
        policies (str): Repair policies such as POOL_BAD_LABEL:CIA_INTERACT
    """
    check_start_cmd = ["dmg", "check", "start"]
    if policies:
        check_start_cmd.extend(["-p", policies])
    command = " ".join(check_start_cmd)
    print(f"Command: {command}")
    subprocess.run(check_start_cmd, check=False)

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
    command = " ".join(check_disable_cmd)
    print(f"Command: {command}")
    subprocess.run(check_disable_cmd, check=False)

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
    command = " ".join(check_repair_cmd)
    print(f"Command: {command}")
    subprocess.run(check_repair_cmd, check=False)

# System-related methods
def system_stop():
    """Stop servers."""
    system_stop_cmd = ["dmg", "system", "stop"]
    command = " ".join(system_stop_cmd)
    print(f"Command: {command}")
    subprocess.run(system_stop_cmd, check=False)

def system_start():
    """Start servers."""
    system_start_cmd = ["dmg", "system", "start"]
    command = " ".join(system_start_cmd)
    print(f"Command: {command}")
    subprocess.run(system_start_cmd, check=False)

def system_query(json=False, verbose=False):
    """Call dmg system query"""
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

def create_three_pools(pool_label, pool_size):
    """Create three pools with consecutive number appended to given label.

    Args:
        pool_label (str): Base pool label.
        pool_size (str): Pool size.

    Returns:
        str: Three pool labels.
    """
    pool_label_1 = f"{pool_label}_1"
    pool_label_2 = f"{pool_label}_2"
    pool_label_3 = f"{pool_label}_3"
    create_pool(pool_size=pool_size, pool_label=pool_label_1)
    create_pool(pool_size=pool_size, pool_label=pool_label_2)
    create_pool(pool_size=pool_size, pool_label=pool_label_3)
    return [pool_label_1, pool_label_2, pool_label_3]
