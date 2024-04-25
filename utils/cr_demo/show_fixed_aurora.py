"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import subprocess  # nosec

import yaml
from ClusterShell.NodeSet import NodeSet
from demo_utils import (cont_get_prop, create_container, list_pool, pool_get_prop, pool_query,
                        storage_query_usage, system_query, system_stop)

# Run this script on Aurora node as user after running run_demo_aurora.py. E.g.,
# python3 show_fixed_aurora.py -l aurora-daos-[0001-0100]

TEST_CMD = "sudo date"
test_cmd_list = TEST_CMD.split(" ")
print(f"Check sudo works by calling: {TEST_CMD}")
subprocess.run(test_cmd_list, check=False)

POOL_LABEL = "tank"
CONT_LABEL = "bucket"
TARGET_PER_RANK = 16

PARSER = argparse.ArgumentParser()
PARSER.add_argument(
    "-l", "--hostlist", required=True, help="List of hosts used for run_demo.py")
ARGS = vars(PARSER.parse_args())
HOSTLIST = ARGS["hostlist"]
node_set = NodeSet(HOSTLIST)
hostlist = list(node_set)

# Call dmg system query to obtain the IP address of necessary ranks.
rank_to_ip = {}
stdout = system_query(json=True)
# Printing system query output helps, but the output will be long if there are many ranks.
# print(f"dmg system query stdout = {stdout}")
generated_yaml = yaml.safe_load(stdout)
RANK_COUNT = 0
JOINED_COUNT = 0
for member in generated_yaml["response"]["members"]:
    rank_to_ip[member["rank"]] = member["addr"].split(":")[0]
    RANK_COUNT += 1
    if member["state"] == "joined":
        JOINED_COUNT += 1
# Print the number of ranks and joined ranks as a reference.
print(f"\n{RANK_COUNT} ranks; {JOINED_COUNT} joined")
TOTAL_TARGET = RANK_COUNT * TARGET_PER_RANK

POOL_LABEL_1 = POOL_LABEL + "_F1"
POOL_LABEL_2 = POOL_LABEL + "_F2"
POOL_LABEL_3 = POOL_LABEL + "_F3"
POOL_LABEL_4 = POOL_LABEL + "_F4"
POOL_LABEL_5 = POOL_LABEL + "_F5"
POOL_LABEL_6 = POOL_LABEL + "_F6"
POOL_LABEL_7 = POOL_LABEL + "_F7"
POOL_LABEL_8 = POOL_LABEL + "_F8"
CONT_LABEL_8 = CONT_LABEL + "_F8"

print("(Create label to UUID mapping.)")
label_to_uuid = {}
stdout = list_pool(json=True)
generated_yaml = yaml.safe_load(stdout)
for pool in generated_yaml["response"]["pools"]:
    label_to_uuid[pool["label"]] = pool["uuid"]

input("\n10. Show the issues fixed. Hit enter...")
print(f"10-F1. Dangling pool ({POOL_LABEL_1}) was removed.")
print(f"10-F3. Orphan pool ({POOL_LABEL_3}) was reconstructed.")
list_pool()

print(f"10-F2. Create a container on {POOL_LABEL_2}. Pool can be started now, so it "
      f"should succeed.")
CONT_LABEL_2 = CONT_LABEL + "_2"
create_container(pool_label=POOL_LABEL_2, cont_label=CONT_LABEL_2)
# (optional) Show that rdb-pool file in rank 0 and 2 are recovered.

print(f"\n10-F4. Label inconsistency for {POOL_LABEL_4} was resolved. "
      f"See pool list above.")
pool_get_prop(pool_label=POOL_LABEL_4, properties="label")

# F5: Call dmg storage query usage to verify the storage was reclaimed. - Not working due
# to a bug. Instead, show that pool directory on dst node (rank 3 for 4-VM) was removed.
print(f"\n10-F5-1. Print storage usage to show that storage used by {POOL_LABEL_5} is "
      f"reclaimed after pool directory is removed from {hostlist[1]}.")
f5_host_list = f"{hostlist[0]},{hostlist[1]}"
storage_query_usage(host_list=f5_host_list)

print(f"\n10-F5-2. {label_to_uuid[POOL_LABEL_5]} pool directory on {hostlist[1]} "
      f"at /mnt/daos0 was removed.")
LS_CMD = "ls /mnt/daos0"
clush_ls_cmd = ["clush", "-w", hostlist[1], LS_CMD]
print(f"Command: {clush_ls_cmd}\n")
subprocess.run(clush_ls_cmd, check=False)

EXPECTED_TARGET = TOTAL_TARGET - 1
print(
    f"\n10-F6. {POOL_LABEL_6} has one less target ({TOTAL_TARGET} -> {EXPECTED_TARGET}).")
pool_query(pool_label=POOL_LABEL_6)
# (optional) Reintegrate rank 1 on pool 6. Wait for rebuild to finish. Then verify the
# target count.

# F8: Verify that the inconsistency is fixed. The label is back to the original.
print(f"\n10-F8. Container label inconsistency for {CONT_LABEL_8} was fixed.")
cont_get_prop(pool_label=POOL_LABEL_8, cont_label=CONT_LABEL_8, properties="label")

# F7: Stop server. Call the same ddb command to verify that the container is removed from
# shard.
print(f"\n10-F7. Use ddb to verify that the container in {POOL_LABEL_7} is removed "
      f"from shards.")
system_stop(force=True)
pool_uuid_7 = label_to_uuid[POOL_LABEL_7]
ddb_cmd = f"sudo ddb /mnt/daos0/{pool_uuid_7}/vos-0 ls"
ddb_cmd_list = ddb_cmd.split(" ")
print(f"Command: {ddb_cmd}")
subprocess.run(ddb_cmd_list, check=False)
