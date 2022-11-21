"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import subprocess


POOL_SIZE = "4GB"
POOL_LABEL = "tank"

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

def list_pool():
    """Call dmg pool list"""
    list_pool_cmd = ["dmg", "pool", "list"]
    command = " ".join(list_pool_cmd)
    print(f"Command: {command}")
    subprocess.run(list_pool_cmd, check=False)

def list_directory(dir_path):
    """Print given directory path.

    Args:
        dir_path (str): Directory path.
    """
    ls_cmd = ["sudo", "ls", "-l", dir_path]
    command = " ".join(ls_cmd)
    print(f"Command: {command}")
    subprocess.run(ls_cmd, check=False)

def enable_checker():
    """Call dmg check enable"""
    check_enable_cmd = ["dmg", "check", "enable"]
    command = " ".join(check_enable_cmd)
    print(f"Command: {command}")
    subprocess.run(check_enable_cmd, check=False)

def start_checker():
    """Call dmg check start"""
    check_start_cmd = ["dmg", "check", "start"]
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


print("Pass 1: Pool is on ranks, but not in MS - Trust PS")

PARSER = argparse.ArgumentParser()
PARSER.add_argument("-l", "--hostlist", required=True, help="List of hosts to format")
ARGS = vars(PARSER.parse_args())

HOSTLIST = ARGS["hostlist"]
input(f"1. Format storage on {HOSTLIST}. Hit enter...")
format_storage(host_list=HOSTLIST)

input("\n2. Create a 4GB pool. Hit enter...")
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL)

input("3. Remove PS entry on MS. Hit enter...")
inject_fault_mgmt(pool_label=POOL_LABEL, fault_type="CIC_POOL_NONEXIST_ON_MS")

input("\n4. MS doesn\'t recognize any pool. Hit enter...")
list_pool()

input("\n5. The pool still exists on engine. Hit enter...")
list_directory("/mnt/daos")

input("\n6. Enable and start checker. Hit enter...")
enable_checker()
start_checker()

print("\n7-1. Query the checker.")
while True:
    USER_INPUT = input("Hit y to query, n to proceed to next step: ")
    if USER_INPUT == "y":
        query_checker()
    elif USER_INPUT == "n":
        break
    else:
        print("Please enter y or n.")

print("7-2. Checker shows the inconsistency that was repaired.")

input("\n8. Disable the checker. Hit enter...")
disable_checker()

input("\n9. Verify that the missing pool was reconstructed. Hit enter...")
list_pool()
