"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import subprocess
import time
import yaml
from demo_utils import format_storage, create_pool, inject_fault_mgmt, list_pool,\
    enable_checker, start_checker, disable_checker, repeat_check_query, repair_checker,\
    query_checker


POOL_SIZE = "1GB"
POOL_LABEL = "tank"
DIR_PATH = "/mnt/daos"

def list_directory(dir_path):
    """Print given directory path.

    Args:
        dir_path (str): Directory path.
    """
    ls_cmd = ["sudo", "ls", "-l", dir_path]
    command = " ".join(ls_cmd)
    print(f"Command: {command}")
    subprocess.run(ls_cmd, check=False)


print("Pass 1: Pool is on ranks, but not in MS - trust PS, trust MS, ignore")

PARSER = argparse.ArgumentParser()
PARSER.add_argument("-l", "--hostlist", required=True, help="List of hosts to format")
ARGS = vars(PARSER.parse_args())

HOSTLIST = ARGS["hostlist"]
input(f"\n1. Format storage on {HOSTLIST}. Hit enter...")
format_storage(host_list=HOSTLIST)

print("\nWait for 5 sec before creating pools...")
time.sleep(5)

input(f"\n2. Create three {POOL_SIZE} pools. Hit enter...")
POOL_LABEL_1 = f"{POOL_LABEL}_1"
POOL_LABEL_2 = f"{POOL_LABEL}_2"
POOL_LABEL_3 = f"{POOL_LABEL}_3"
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL_1)
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL_2)
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL_3)

print("(Create label to UUID mapping.)")
label_to_uuid = {}
stdout = list_pool(json=True)
generated_yaml = yaml.safe_load(stdout)
for pool in generated_yaml["response"]["pools"]:
    label_to_uuid[pool["label"]] = pool["uuid"]

input("3. Remove PS entry on MS. Hit enter...")
inject_fault_mgmt(pool_label=POOL_LABEL_1, fault_type="CIC_POOL_NONEXIST_ON_MS")
inject_fault_mgmt(pool_label=POOL_LABEL_2, fault_type="CIC_POOL_NONEXIST_ON_MS")
inject_fault_mgmt(pool_label=POOL_LABEL_3, fault_type="CIC_POOL_NONEXIST_ON_MS")

input("\n4. MS doesn\'t recognize any pool. Hit enter...")
list_pool()

input("\n5. The pools still exist on engine. Hit enter...")
list_directory("/mnt/daos")

input("\n6. Enable the checker. Hit enter...")
enable_checker()

input("\n7. Start interactive mode. Hit enter...")
start_checker(policies="POOL_NONEXIST_ON_MS:CIA_INTERACT")

input("\n8. Show repair options. Hit enter...")
query_checker()

print("(Create UUID to sequence number mapping.)")
uuid_to_seqnum = {}
stdout = query_checker(json=True)
generated_yaml = yaml.safe_load(stdout)
for report in generated_yaml["response"]["reports"]:
    uuid_to_seqnum[report["pool_uuid"]] = report["seq"]

input(f"\n8-1. Select 0 (Ignore) for {POOL_LABEL_1}. Hit enter...")
SEQ_NUM_1 = str(uuid_to_seqnum[label_to_uuid[POOL_LABEL_1]])
SEQ_NUM_2 = str(uuid_to_seqnum[label_to_uuid[POOL_LABEL_2]])
SEQ_NUM_3 = str(uuid_to_seqnum[label_to_uuid[POOL_LABEL_3]])
repair_checker(sequence_num=SEQ_NUM_1, action="0")
input(f"\n8-2. Select 1 (Discard pool) for {POOL_LABEL_2}. Hit enter...")
repair_checker(sequence_num=SEQ_NUM_2, action="1")
input(f"\n8-3. Select 2 (Re-add) for {POOL_LABEL_3}. Hit enter...")
repair_checker(sequence_num=SEQ_NUM_3, action="2")

print("\n9. Query the checker.")
repeat_check_query()

print("9-1. Checker shows the result for each pool.")

input("\n10. Disable the checker. Hit enter...")
disable_checker()

input("\n11. Verify the checker results for each pool. Hit enter...")
list_pool()
list_directory(dir_path=DIR_PATH)
