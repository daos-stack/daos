"""
  (C) Copyright 2020-2022 Intel Corporation.

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
    print("Command: {}".format(" ".join(format_cmd)))
    subprocess.run(format_cmd, check=False)

def create_pool(pool_size, pool_label):
    """Call dmg pool create.

    Args:
        pool_size (str): Pool size.
        pool_label (str): Pool label.
    """
    create_pool_cmd = ["dmg", "pool", "create", "--size=" + pool_size,
                       "--label=" + pool_label]
    print("Command: {}".format(" ".join(create_pool_cmd)))
    subprocess.run(create_pool_cmd, check=False)

def inject_fault_mgmt(pool_label, fault_type):
    """Call dmg faults mgmt-svc to inject fault.

    Args:
        pool_label (str): Pool label.
        fault_type (str): Fault type.
    """
    inject_fault_cmd = ["dmg", "faults", "mgmt-svc", "pool", pool_label, fault_type]
    print("Command: {}".format(" ".join(inject_fault_cmd)))
    subprocess.run(inject_fault_cmd, check=False)

def list_pool():
    """Call dmg pool list"""
    list_pool_cmd = ["dmg", "pool", "list"]
    print("Command: {}".format(" ".join(list_pool_cmd)))
    subprocess.run(list_pool_cmd, check=False)

def enable_checker():
    """Call dmg check enable"""
    check_enable_cmd = ["dmg", "check", "enable"]
    print("Command: {}".format(" ".join(check_enable_cmd)))
    subprocess.run(check_enable_cmd, check=False)

def start_checker():
    """Call dmg check start"""
    check_start_cmd = ["dmg", "check", "start"]
    print("Command: {}".format(" ".join(check_start_cmd)))
    subprocess.run(check_start_cmd, check=False)

def query_checker():
    """Call dmg check query"""
    check_query_cmd = ["dmg", "check", "query"]
    print("Command: {}".format(" ".join(check_query_cmd)))
    subprocess.run(check_query_cmd, check=False)

def disable_checker():
    """Call dmg check disable"""
    check_disable_cmd = ["dmg", "check", "disable"]
    print("Command: {}".format(" ".join(check_disable_cmd)))
    subprocess.run(check_disable_cmd, check=False)

PARSER = argparse.ArgumentParser()
PARSER.add_argument("-l", "--hostlist", required=True, help="List of hosts to format")
ARGS = vars(PARSER.parse_args())

HOSTLIST = ARGS["hostlist"]
input("1. Format storage on {}. Hit enter...".format(HOSTLIST))
format_storage(host_list=HOSTLIST)

input("\n2. Create a 4GB pool. Hit enter...")
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL)

input("3. Remove PS entry on MS. Hit enter...")
inject_fault_mgmt(pool_label=POOL_LABEL, fault_type="CIC_POOL_NONEXIST_ON_MS")

input("\n4. MS doesn\'t recognize any pool (it exists on engine). Hit enter...")
list_pool()

input("\n5. Enable and start checker. Hit enter...")
enable_checker()
start_checker()

print("\n6-1. Query the checker.")
while True:
    USER_INPUT = input("Hit y to query, n to proceed to next step: ")
    if USER_INPUT == "y":
        query_checker()
    elif USER_INPUT == "n":
        break
    else:
        print("Please enter y or n.")

print("6-2. Checker shows the inconsistency that was repaired.")

input("\n7. Disable the checker. Hit enter...")
disable_checker()

input("\n8. Verify that the missing pool was reconstructed. Hit enter...")
list_pool()
