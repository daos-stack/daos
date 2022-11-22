"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import subprocess


def format_storage(host_list):
    """Call dmg storage format.

    Args:
        host_list (str): List of hosts to format.
    """
    format_cmd = ["dmg", "storage", "format", "--host-list=" + host_list]
    command = " ".join(format_cmd)
    print(f"Command: {command}")
    subprocess.run(format_cmd, check=False)

def create_pool(pool_size, pool_label):
    """Call dmg pool create.

    Args:
        pool_size (str): Pool size.
        pool_label (str): Pool label.
    """
    create_pool_cmd = ["dmg", "pool", "create", "--size=" + pool_size,
                       "--label=" + pool_label]
    command = " ".join(create_pool_cmd)
    print(f"Command: {command}")
    subprocess.run(create_pool_cmd, check=False)

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

def list_pool(verbose=False):
    """Call dmg pool list

    Args:
        verbose (bool): Whether to use --verbose. Defaults to False.
    """
    list_pool_cmd = ["dmg", "pool", "list"]
    if verbose:
        list_pool_cmd.append("--verbose")
    command = " ".join(list_pool_cmd)
    print(f"Command: {command}")
    subprocess.run(list_pool_cmd, check=False)

def enable_checker():
    """Call dmg check enable"""
    check_enable_cmd = ["dmg", "check", "enable"]
    command = " ".join(check_enable_cmd)
    print(f"Command: {command}")
    subprocess.run(check_enable_cmd, check=False)

def start_checker(policies=None):
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

def query_checker():
    """Call dmg check query"""
    check_query_cmd = ["dmg", "check", "query"]
    command = " ".join(check_query_cmd)
    print(f"Command: {command}")
    subprocess.run(check_query_cmd, check=False)

def disable_checker():
    """Call dmg check disable"""
    check_disable_cmd = ["dmg", "check", "disable"]
    command = " ".join(check_disable_cmd)
    print(f"Command: {command}")
    subprocess.run(check_disable_cmd, check=False)

def repeat_check_query():
    """Allow user to repeatedly call dmg check query."""
    while True:
        USER_INPUT = input("Hit y to query, n to proceed to next step: ")
        if USER_INPUT == "y":
            query_checker()
        elif USER_INPUT == "n":
            break
        else:
            print("Please enter y or n.")
