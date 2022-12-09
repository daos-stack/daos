"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import time
import yaml
import subprocess
from demo_utils import format_storage, inject_fault_mgmt, list_pool, check_enable,\
    check_start, check_query, check_disable, repeat_check_query, check_repair,\
    create_uuid_to_seqnum, create_three_pools, create_label_to_uuid, get_current_labels,\
    pool_get_prop, create_pool, inject_fault_pool, create_container, inject_fault_daos,\
    system_stop, cont_set_prop, system_query, storage_query_usage, cont_get_prop,\
    system_start, check_set_policy

# Need to use at least "scm_size: 10" for server config to create 3 1GB-pools.
POOL_SIZE_1GB = "1GB"
POOL_SIZE_5GB = "5GB"
POOL_LABEL = "tank"
CONT_LABEL = "bucket"

print("Test all CR features")

PARSER = argparse.ArgumentParser()
PARSER.add_argument("-l", "--hostlist", required=True, help="List of hosts to format")
ARGS = vars(PARSER.parse_args())

HOSTLIST = ARGS["hostlist"]

input(f"Prepare environment on {HOSTLIST}. Hit enter...")
print(f"\n1. Format storage on {HOSTLIST}.")
format_storage(host_list=HOSTLIST)

print("\nWait for 10 sec for format...")
time.sleep(10)

# Call dmg system query to obtain the IP address of necessary ranks.
rank_0_ip = None
rank_1_ip = None
rank_2_ip = None
rank_3_ip = None
stdout = system_query(json=True)
print("## dmg system query stdout = {}".format(stdout))
generated_yaml = yaml.safe_load(stdout)
for member in generated_yaml["response"]["members"]:
    if member["rank"] == 0:
        rank_0_ip = member["addr"].split(":")[0]
    elif member["rank"] == 1:
        rank_1_ip = member["addr"].split(":")[0]
    elif member["rank"] == 2:
        rank_2_ip = member["addr"].split(":")[0]
    elif member["rank"] == 3:
        rank_3_ip = member["addr"].split(":")[0]

# print("## Rank 0 IP = {}; Rank 2 IP = {}".format(rank_0_ip, rank_2_ip))

# Add input here to make sure all ranks are joined before starting the script.
input(f"\n2. Create 7 pools and containers. Hit enter...")
pool_label_1 = POOL_LABEL + "_1"
pool_label_2 = POOL_LABEL + "_2"
pool_label_4 = POOL_LABEL + "_4"
pool_label_5 = POOL_LABEL + "_5"
pool_label_6 = POOL_LABEL + "_6"
pool_label_7 = POOL_LABEL + "_7"
pool_label_8 = POOL_LABEL + "_8"
cont_label_7 = CONT_LABEL + "_7"
cont_label_8 = CONT_LABEL + "_8"

# F1. CIC_POOL_NONEXIST_ON_ENGINE - dangling pool
create_pool(pool_size=POOL_SIZE_1GB, pool_label=pool_label_1)
# F2. CIC_POOL_LESS_SVC_WITHOUT_QUORUM
create_pool(pool_size=POOL_SIZE_1GB, pool_label=pool_label_2, nsvc="3")
# F4. CIC_POOL_BAD_LABEL - inconsistent pool label between MS and PS
create_pool(pool_size=POOL_SIZE_1GB, pool_label=pool_label_4)
# F5. CIC_ENGINE_NONEXIST_IN_MAP - orphan pool shard
create_pool(pool_size=POOL_SIZE_1GB, pool_label=pool_label_5, ranks="1")
# F6. CIC_ENGINE_HAS_NO_STORAGE - dangling pool map
create_pool(pool_size=POOL_SIZE_1GB, pool_label=pool_label_6, ranks="0,1")
# F7. CIC_CONT_NONEXIST_ON_PS - orphan container
create_pool(pool_size=POOL_SIZE_1GB, pool_label=pool_label_7)
create_container(pool_label=pool_label_7, cont_label=cont_label_7)
# F8. CIC_CONT_BAD_LABEL
create_pool(pool_size=POOL_SIZE_1GB, pool_label=pool_label_8)
create_container(pool_label=pool_label_8, cont_label=cont_label_8)

print(f"\n3-F5. Print storage usage to show that original usage of {pool_label_5}.")
# We'll copy the pool dir from rank 1 to 3.
f5_host_list = "{},{}".format(rank_1_ip, rank_3_ip)
storage_query_usage(host_list=f5_host_list)

####################################################################
print("\n4. Inject fault with dmg (except F5, F6).")
# F1
inject_fault_pool(pool_label=pool_label_1, fault_type="CIC_POOL_NONEXIST_ON_ENGINE")
# F4
inject_fault_mgmt(pool_label=pool_label_4, fault_type="CIC_POOL_BAD_LABEL")

# F7
inject_fault_daos(
    pool_label=pool_label_7, cont_label=cont_label_7, fault_type="DAOS_CHK_CONT_ORPHAN")

# F8
inject_fault_daos(
    pool_label=pool_label_8, cont_label=cont_label_8,
    fault_type="DAOS_CHK_CONT_BAD_LABEL")
# Also update label to bucket_8-fault.
cont_label_8_new = cont_label_8 + "-fault"
properties = "label:" + cont_label_8_new
# This fails: DAOS-12215
# cont_set_prop(pool_label=pool_label_8, cont_label=cont_label_8, properties=properties)

print("(Create label to UUID mapping.)")
label_to_uuid = {}
stdout = list_pool(json=True)
generated_yaml = yaml.safe_load(stdout)
for pool in generated_yaml["response"]["pools"]:
    label_to_uuid[pool["label"]] = pool["uuid"]

####################################################################
input("\n5-1. Stop servers to manipulate for F2, F5, F6, F7. Hit enter...")
system_stop()

# F2: Destroy tank_2 rdb-pool on rank 0 and 2.
rank_0_2_ips = "{},{}".format(rank_0_ip, rank_2_ip)
rm_cmd = f"sudo rm /mnt/daos/{label_to_uuid[pool_label_2]}/rdb-pool"
remove_rdb_rank_0_2 = ["clush", "-w", rank_0_2_ips, rm_cmd]
print("## Command to remove rdb-pool = {}".format(remove_rdb_rank_0_2))
subprocess.run(remove_rdb_rank_0_2, check=False)

# F5: Copy tank_5 pool directory from rank 1 to rank 3. Match owner.
# In order to scp the pool directory without password, there are two things to set up.
# 1. Add user's public key to authorized_key of root.
# 2. Since we're running scp as user, update the mode of the pool directory as below.
# Set 777 for /mnt/daos and /mnt/daos/<pool_5>/* i.e.,
# chmod 777 /mnt/daos; chmod -R 777 /mnt/daos/<pool_5>

# Alternatively, we can generate public-private key pair for root and call scp with sudo.
# Then we don't need to do step 2 (update mode to 777).

# Update the mode.
pool_uuid_5 = label_to_uuid[pool_label_5]
chmod_cmd = f"sudo chmod 777 /mnt/daos; sudo chmod -R 777 /mnt/daos/{pool_uuid_5}"
print("## chmod command = {}".format(chmod_cmd))
clush_chmod_cmd = ["clush", "-w", rank_1_ip, chmod_cmd]
print("## clush chmod command = {}".format(clush_chmod_cmd))
subprocess.run(clush_chmod_cmd, check=False)

# sudo scp -rp /mnt/daos/<pool_uuid_5> root@<rank_3>:/mnt/daos/
scp_cmd = (f"scp -rp /mnt/daos/{pool_uuid_5} "
           f"root@{rank_3_ip}:/mnt/daos/")
print("## scp command = {}".format(scp_cmd))
copy_pool_dir = ["clush", "-w", rank_1_ip, scp_cmd]
print("## Command to copy tank_5 from 1 to 3 = {}".format(copy_pool_dir))
subprocess.run(copy_pool_dir, check=False)

# Set owner for the copied dir and files to daos_server:daos_server.
chown_cmd = f"sudo chown -R daos_server:daos_server /mnt/daos/{pool_uuid_5}"
clush_chown_cmd = ["clush", "-w", rank_3_ip, chown_cmd]
print("## clush chown command = {}".format(clush_chown_cmd))
subprocess.run(clush_chown_cmd, check=False)

# F6: Remove pool directory from rank 0.
pool_uuid_6 = label_to_uuid[pool_label_6]
rm_cmd = f"sudo rm -rf /mnt/daos/{pool_uuid_6}"
clush_rm_cmd = ["clush", "-w", rank_0_ip, rm_cmd]
print("## clush rm command = {}".format(clush_rm_cmd))
subprocess.run(clush_rm_cmd, check=False)

# F7: Use ddb to verify that the container is left in shards.
pool_uuid_7 = label_to_uuid[pool_label_7]
# ddb -R "ls" /mnt/daos/<pool_uuid_7>/vos-0
ddb_cmd = f"sudo ddb -R \"ls\" /mnt/daos/{pool_uuid_7}/vos-0"
clush_ddb_cmd = ["clush", "-w", rank_0_ip, ddb_cmd]
print("## clush ddb command = {}".format(clush_ddb_cmd))
subprocess.run(clush_ddb_cmd, check=False)

print("\n5-2. Restart servers.")
system_start()

print("## Show system query 1")
system_query(verbose=True)

####################################################################
input("\n6. Show the faults inserted for each pool/container except "
      "F2, F6, F7. Hit enter...")
# F1: Show dangling pool entry
print("\n6-F1. Show dangling pool entry.")
# F4 part 1
print("6-F4-1. Labels in MS are corrupted with -fault added.")
list_pool(no_query=True)

# 2: (optional) Try to create a container, which will hang.

# F4 part 2
print("\n6-F4-2. Labels in PS are still original.")
pool_label_4_fault = pool_label_4 + "-fault"
pool_get_prop(pool_label=pool_label_4_fault, properties="label")

# F5: Call dmg storage query usage to see that the pool is using more space.
print(f"\n6-F5. Print storage usage to show that the {pool_label_5} is using more space.")
storage_query_usage(host_list=f5_host_list)

# F8: Show inconsistency by getting the container label.
print("\n6-F8. Show container label inconsistency.")
# daos container get-prop tank_8 bucket_8
# It should show error because the label has been changed.
cont_get_prop(pool_label=pool_label_8, cont_label=cont_label_8)
# daos container get-prop --properties=label tank_8 new-label
# It should show bucket_8
cont_get_prop(pool_label=pool_label_8, cont_label="new-label", properties="label")

print("## Show system query 2")
system_query(verbose=True)

input("\n7. Enable checker. Hit enter...")
check_enable()

input("\n8. Start checker with interactive mode. Hit enter...")
check_set_policy(all_interactive=True)
check_start()

# input("\n8. Select suggested repair option for all faults. Hit enter...")
# check_repair(...)

# print("\n8. Query the checker.")
# repeat_check_query()

# print("\n11. Disable the checker.")
# check_disable()

# input("\n12. Show the issues fixed. Hit enter...")
# 1: Verify that the dangling pool was removed.

# list_pool()

# 2: Try creating a container. It should succeed.
# (optional) Show that rdb-pool file in rank 0 and 2 are recovered.

# 4: Show repaired labels in MS and PS.

# 5: Call dmg storage query usage to verify the storage was reclaimed. - Not working due
# to a bug. Instead, show that pool directory on rank 1 was removed.

# 6: Verify that the pool directory on rank 1 is retrieved.
# (optional) Reintegrate rank 1 on pool 6. Wait for rebuild to finish. Then verify the
# target count.

# 7: Stop server. Call the same ddb command to verify that the container is removed from
# shard.

# 8: Verify that the inconsistency is fixed (checker used the new label).
# Call: daos container get-prop tank_8 bucket_8-fault --properties=label
