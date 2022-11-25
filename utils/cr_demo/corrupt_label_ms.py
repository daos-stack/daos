"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import subprocess
import time
import yaml
from demo_utils import format_storage, create_pool, inject_fault_mgmt, list_pool,\
    check_enable, check_start, check_query, check_disable, repeat_check_query,\
    check_repair

POOL_SIZE = "1GB"
POOL_LABEL = "tank"


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

input("\n3. Corrupt label in MS. Hit enter...")
inject_fault_mgmt(pool_label=POOL_LABEL_1, fault_type="CIC_POOL_BAD_LABEL")
inject_fault_mgmt(pool_label=POOL_LABEL_2, fault_type="CIC_POOL_BAD_LABEL")
inject_fault_mgmt(pool_label=POOL_LABEL_3, fault_type="CIC_POOL_BAD_LABEL")

input("\n4. Labels in MS are corrupted with -fault added. Hit enter...")
list_pool()

input("\n5. Enable checker. Hit enter...")
check_enable()

input("\n6. Start interactive mode. Hit enter...")
check_start(policies="POOL_BAD_LABEL:CIA_INTERACT")

input("\n7. Show repair options. Hit enter...")
check_query()

print("(Create UUID to sequence number mapping.)")
uuid_to_seqnum = {}
stdout = check_query(json=True)
generated_yaml = yaml.safe_load(stdout)
for report in generated_yaml["response"]["reports"]:
    uuid_to_seqnum[report["pool_uuid"]] = report["seq"]

input(f"\n8-1. Select 0 (Ignore) for {POOL_LABEL_1}. Hit enter...")
SEQ_NUM_1 = str(uuid_to_seqnum[label_to_uuid[POOL_LABEL_1]])
SEQ_NUM_2 = str(uuid_to_seqnum[label_to_uuid[POOL_LABEL_2]])
SEQ_NUM_3 = str(uuid_to_seqnum[label_to_uuid[POOL_LABEL_3]])
check_repair(sequence_num=SEQ_NUM_1, action="0")
input(f"\n8-2. Select 1 (Trust MS) for {POOL_LABEL_2}. Hit enter...")
check_repair(sequence_num=SEQ_NUM_2, action="1")
input(f"\n8-3. Select 2 (Trust PS) for {POOL_LABEL_3}. Hit enter...")
check_repair(sequence_num=SEQ_NUM_3, action="2")

print("\n9-1. Query the checker.")
repeat_check_query()

print("9-2. Checker shows the repair result for each pool.")

input("\n10. Disable the checker. Hit enter...")
check_disable()

input("\n11. Show repaired labels in MS and PS. Hit enter...")
print("(Get current labels.)")
pool_labels = []
stdout = list_pool(json=True)
generated_yaml = yaml.safe_load(stdout)
for pool in generated_yaml["response"]["pools"]:
    pool_labels.append(pool["label"])

list_pool()
for label in pool_labels:
    pool_get_prop(pool_label=label, properties="label")
