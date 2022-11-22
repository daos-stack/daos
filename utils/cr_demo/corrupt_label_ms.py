"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import subprocess
import yaml
from demo_utils import format_storage, create_pool, inject_fault_mgmt, list_pool,\
    enable_checker, start_checker, query_checker, disable_checker, repeat_check_query


POOL_SIZE = "4GB"
POOL_LABEL = "tank"

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

def get_query_result():
    """Call dmg check query with --json and return the output. """
    check_query_cmd = ["dmg", "--json", "check", "query"]
    command = " ".join(check_query_cmd)
    print(f"Calling {command}")
    result = subprocess.run(
        check_query_cmd, stdout=subprocess.PIPE, universal_newlines=True, check=False)
    return result.stdout


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
repeat_check_query()

print("8-2. Checker shows the label inconsistency that was repaired.")

input("\n9. Disable the checker. Hit enter...")
disable_checker()

input("\n10. Verify that the original pool label was retrieved. Hit enter...")
list_pool()
