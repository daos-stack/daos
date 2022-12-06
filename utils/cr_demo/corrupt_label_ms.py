"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import time
from demo_utils import format_storage, inject_fault_mgmt, list_pool, check_enable,\
    check_start, check_query, check_disable, repeat_check_query, check_repair,\
    create_uuid_to_seqnum, create_three_pools, create_label_to_uuid, get_current_labels,\
    pool_get_prop

# Need to use at least "scm_size: 10" for server config to create 3 1GB-pools.
POOL_SIZE = "1GB"
POOL_LABEL = "tank"

print("Pass 3: Corrupt label in MS - trust MS, trust PS, ignore")

PARSER = argparse.ArgumentParser()
PARSER.add_argument("-l", "--hostlist", required=True, help="List of hosts to format")
ARGS = vars(PARSER.parse_args())

HOSTLIST = ARGS["hostlist"]
input(f"\n1. Format storage on {HOSTLIST}. Hit enter...")
format_storage(host_list=HOSTLIST)

print("\nWait for 5 sec before creating pools...")
time.sleep(5)

input(f"\n2. Create three {POOL_SIZE} pools. Hit enter...")
orig_pool_labels = create_three_pools(pool_label=POOL_LABEL, pool_size=POOL_SIZE)

print("(Create label to UUID mapping.)")
label_to_uuid = create_label_to_uuid()

input("\n3. Corrupt label in MS. Hit enter...")
inject_fault_mgmt(pool_label=orig_pool_labels[0], fault_type="CIC_POOL_BAD_LABEL")
inject_fault_mgmt(pool_label=orig_pool_labels[1], fault_type="CIC_POOL_BAD_LABEL")
inject_fault_mgmt(pool_label=orig_pool_labels[2], fault_type="CIC_POOL_BAD_LABEL")

input("\n4. Labels in MS are corrupted with -fault added. Hit enter...")
list_pool()

input("\n5. Labels in PS are still original. Hit enter...")
pool_labels = get_current_labels()
for label in pool_labels:
    pool_get_prop(pool_label=label, properties="label")
    print()

input("\n6. Enable checker. Hit enter...")
check_enable()

input("\n7. Start interactive mode. Hit enter...")
check_start(policies="POOL_BAD_LABEL:CIA_INTERACT")

input("\n8. Show repair options. Hit enter...")
check_query()

print("(Create UUID to sequence number mapping.)")
uuid_to_seqnum = create_uuid_to_seqnum()

# Obtain sequence number for each pool.
SEQ_NUM_1 = str(uuid_to_seqnum[label_to_uuid[orig_pool_labels[0]]])
SEQ_NUM_2 = str(uuid_to_seqnum[label_to_uuid[orig_pool_labels[1]]])
SEQ_NUM_3 = str(uuid_to_seqnum[label_to_uuid[orig_pool_labels[2]]])

input(f"\n9-1. Select 0 (Ignore) for {orig_pool_labels[0]}. Hit enter...")
check_repair(sequence_num=SEQ_NUM_1, action="0")
input(f"\n9-2. Select 1 (Trust MS) for {orig_pool_labels[1]}. Hit enter...")
check_repair(sequence_num=SEQ_NUM_2, action="1")
input(f"\n9-3. Select 2 (Trust PS) for {orig_pool_labels[2]}. Hit enter...")
check_repair(sequence_num=SEQ_NUM_3, action="2")

print("\n10-1. Query the checker.")
repeat_check_query()

print("10-2. Checker shows the repair result for each pool.")

input("\n11. Disable the checker. Hit enter...")
check_disable()

input("\n12. Show repaired labels in MS and PS. Hit enter...")
print("(Get current labels.)")
pool_labels = get_current_labels()

list_pool()
for label in pool_labels:
    pool_get_prop(pool_label=label, properties="label")
    print()
