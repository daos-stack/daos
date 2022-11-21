"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import subprocess
import yaml


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

def repair_checker(sequence_num, action):
    """Call dmg check repair

    Args:
        sequence_num (str): Sequence number for repair action.
        action (str): Repair action number.
    """
    check_repair_cmd = ["dmg", "check", "repair", sequence_num, action]
    command = " ".join(check_repair_cmd)
    print(f"Command: {command}")
    subprocess.run(check_repair_cmd, check=False)

def query_checker():
    """Call dmg check query"""
    check_query_cmd = ["dmg", "check", "query"]
    command = " ".join(check_query_cmd)
    print(f"Command: {command}")
    subprocess.run(check_query_cmd, check=False)

def get_query_result():
    """Call dmg check query with --json and return the output. """
    check_query_cmd = ["dmg", "--json", "check", "query"]
    command = " ".join(check_query_cmd)
    print(f"Calling {command}")
    result = subprocess.run(
        check_query_cmd, stdout=subprocess.PIPE, universal_newlines=True, check=False)
    return result.stdout

def disable_checker():
    """Call dmg check disable"""
    check_disable_cmd = ["dmg", "check", "disable"]
    command = " ".join(check_disable_cmd)
    print(f"Command: {command}")
    subprocess.run(check_disable_cmd, check=False)


print("Pass 3: Corrupt label in MS")

PARSER = argparse.ArgumentParser()
PARSER.add_argument("-l", "--hostlist", required=True, help="List of hosts to format")
ARGS = vars(PARSER.parse_args())

HOSTLIST = ARGS["hostlist"]
input(f"\n1. Format storage on {HOSTLIST}. Hit enter...")
format_storage(host_list=HOSTLIST)

input("\n2. Create a 4GB pool. Hit enter...")
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL)

input("3. Corrupt label in MS. Hit enter...")
inject_fault_mgmt(pool_label=POOL_LABEL, fault_type="CIC_POOL_BAD_LABEL")

input("\n4. Label in MS is corrupted with -fault added. Hit enter...")
list_pool()

input("\n5. Enable checker. Hit enter...")
enable_checker()

input("\n6. Start checker with interactive mode. Hit enter...")
start_checker(policies="POOL_BAD_LABEL:CIA_INTERACT")

input("\n6-1. Call dmg check query to obtain the seq-num. Hit enter...")
# Print the query result.
query_checker()
# To obtain the seq-num from JSON output. Don't print the output.
stdout = get_query_result()
generated_yaml = yaml.safe_load(stdout)
seq_num = generated_yaml["response"]["reports"][0]["seq"]
print("Sequence Number: {}".format(seq_num))

input("\n7. Repair checker with option 2, trust PS pool entry. Hit enter...")
repair_checker(sequence_num=str(seq_num), action="2")

print("\n8-1. Query the checker.")
while True:
    USER_INPUT = input("Hit y to query, n to proceed to next step: ")
    if USER_INPUT == "y":
        query_checker()
    elif USER_INPUT == "n":
        break
    else:
        print("Please enter y or n.")

print("8-2. Checker shows the label inconsistency that was repaired.")

input("\n9. Disable the checker. Hit enter...")
disable_checker()

input("\n10. Verify that the original pool label was retrieved. Hit enter...")
list_pool()
