"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import time
import yaml
import subprocess
from demo_utils import format_storage, inject_fault_mgmt, list_pool, check_enable,\
    check_start, check_disable, repeat_check_query, check_repair, create_uuid_to_seqnum,\
    pool_get_prop, create_pool, inject_fault_pool, create_container, inject_fault_daos,\
    system_stop, system_query, storage_query_usage, cont_get_prop, system_start,\
    check_set_policy, pool_query

# Need to use at least "scm_size: 15" for server config to create 8 1GB-pools.
POOL_SIZE_1GB = "1GB"
POOL_SIZE_5GB = "5GB"
POOL_LABEL = "tank"
CONT_LABEL = "bucket"
# Rank to create F5 pool.
F5_RANK_ORIG = 1
# Rank to copy the F5 pool dir to.
F5_RANK_FAULT = 3

print("F1: Dangling pool")
print("F2: Lost the majority of pool service replicas")
print("F3: Orphan pool")
print("F4: Inconsistent pool label between MS and PS")
print("F5: Orphan pool shard")
print("F6: Dangling pool map")
print("F7: Orphan container")
print("F8: Inconsistent container label between CS and container property")

PARSER = argparse.ArgumentParser()
PARSER.add_argument("-l", "--hostlist", required=True, help="List of hosts to format")
ARGS = vars(PARSER.parse_args())

HOSTLIST = ARGS["hostlist"]

print(f"\n1. Format storage on {HOSTLIST}.")
format_storage(host_list=HOSTLIST)

print("\nWait for 10 sec for format...")
time.sleep(10)

# Call dmg system query to obtain the IP address of necessary ranks.
rank_to_ip = {}
stdout = system_query(json=True)
print(f"dmg system query stdout = {stdout}")
generated_yaml = yaml.safe_load(stdout)
for member in generated_yaml["response"]["members"]:
    rank_to_ip[member["rank"]] = member["addr"].split(":")[0]

# Add input here to make sure all ranks are joined before starting the script.
input("\n2. Create 8 pools and containers. Hit enter...")
POOL_LABEL_1 = POOL_LABEL + "_1"
POOL_LABEL_2 = POOL_LABEL + "_2"
POOL_LABEL_3 = POOL_LABEL + "_3"
POOL_LABEL_4 = POOL_LABEL + "_4"
POOL_LABEL_5 = POOL_LABEL + "_5"
POOL_LABEL_6 = POOL_LABEL + "_6"
POOL_LABEL_7 = POOL_LABEL + "_7"
POOL_LABEL_8 = POOL_LABEL + "_8"
CONT_LABEL_7 = CONT_LABEL + "_7"
CONT_LABEL_8 = CONT_LABEL + "_8"

# F1. CIC_POOL_NONEXIST_ON_ENGINE - dangling pool
create_pool(pool_size=POOL_SIZE_1GB, pool_label=POOL_LABEL_1)
# F2. CIC_POOL_LESS_SVC_WITHOUT_QUORUM
create_pool(pool_size=POOL_SIZE_1GB, pool_label=POOL_LABEL_2, nsvc="3")
# F3. CIC_POOL_NONEXIST_ON_MS - orphan pool
create_pool(pool_size=POOL_SIZE_1GB, pool_label=POOL_LABEL_3)
# F4. CIC_POOL_BAD_LABEL - inconsistent pool label between MS and PS
create_pool(pool_size=POOL_SIZE_1GB, pool_label=POOL_LABEL_4)
# F5. CIC_ENGINE_NONEXIST_IN_MAP - orphan pool shard
create_pool(pool_size=POOL_SIZE_1GB, pool_label=POOL_LABEL_5, ranks=str(F5_RANK_ORIG))
# F6. CIC_ENGINE_HAS_NO_STORAGE - dangling pool map
create_pool(pool_size=POOL_SIZE_1GB, pool_label=POOL_LABEL_6)
# F7. CIC_CONT_NONEXIST_ON_PS - orphan container
create_pool(pool_size=POOL_SIZE_1GB, pool_label=POOL_LABEL_7)
create_container(pool_label=POOL_LABEL_7, cont_label=CONT_LABEL_7)
print()
# F8. CIC_CONT_BAD_LABEL
create_pool(pool_size=POOL_SIZE_1GB, pool_label=POOL_LABEL_8)
create_container(pool_label=POOL_LABEL_8, cont_label=CONT_LABEL_8)

print("(Create label to UUID mapping.)")
label_to_uuid = {}
stdout = list_pool(json=True)
generated_yaml = yaml.safe_load(stdout)
for pool in generated_yaml["response"]["pools"]:
    label_to_uuid[pool["label"]] = pool["uuid"]

print(f"\n3-F5. Print storage usage to show original usage of {POOL_LABEL_5}. "
      f"Pool is created on {rank_to_ip[F5_RANK_ORIG]}.")
# F5 pool is created on rank 1, but we'll copy the pool dir from rank 1 to 3, so show
# both.
f5_host_list = f"{rank_to_ip[F5_RANK_ORIG]},{rank_to_ip[F5_RANK_FAULT]}"
storage_query_usage(host_list=f5_host_list)

####################################################################
print("\n4. Inject fault with dmg (except F5, F6).")
# F1
inject_fault_pool(pool_label=POOL_LABEL_1, fault_type="CIC_POOL_NONEXIST_ON_ENGINE")

# F3
inject_fault_mgmt(pool_label=POOL_LABEL_3, fault_type="CIC_POOL_NONEXIST_ON_MS")

# F4
inject_fault_mgmt(pool_label=POOL_LABEL_4, fault_type="CIC_POOL_BAD_LABEL")

# F7
inject_fault_daos(
    pool_label=POOL_LABEL_7, cont_label=CONT_LABEL_7, fault_type="DAOS_CHK_CONT_ORPHAN")

# F8
inject_fault_daos(
    pool_label=POOL_LABEL_8, cont_label=CONT_LABEL_8,
    fault_type="DAOS_CHK_CONT_BAD_LABEL")

####################################################################
input("\n5-1. Stop servers to manipulate for F2, F5, F6, F7. Hit enter...")
system_stop()

# F2: Destroy tank_2 rdb-pool on rank 0 and 2.
rank_0_2_ips = f"{rank_to_ip[0]},{rank_to_ip[2]}"
rm_cmd = f"sudo rm /mnt/daos/{label_to_uuid[POOL_LABEL_2]}/rdb-pool"
remove_rdb_rank_0_2 = ["clush", "-w", rank_0_2_ips, rm_cmd]
print("(F2: Destroy tank_2 rdb-pool on rank 0 and 2.)")
print(f"Command: {remove_rdb_rank_0_2}\n")
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
print("(F5: Update mode for pool directory.)")
pool_uuid_5 = label_to_uuid[POOL_LABEL_5]
chmod_cmd = f"sudo chmod 777 /mnt/daos; sudo chmod -R 777 /mnt/daos/{pool_uuid_5}"
clush_chmod_cmd = ["clush", "-w", rank_to_ip[1], chmod_cmd]
print(f"Command: {clush_chmod_cmd}\n")
subprocess.run(clush_chmod_cmd, check=False)

# sudo scp -rp /mnt/daos/<pool_uuid_5> root@<rank_3>:/mnt/daos/
print("(F5: Copy pool directory from rank 1 to 3.)")
scp_cmd = (f"scp -rp /mnt/daos/{pool_uuid_5} "
           f"root@{rank_to_ip[3]}:/mnt/daos/")
copy_pool_dir = ["clush", "-w", rank_to_ip[1], scp_cmd]
print(f"Command: {copy_pool_dir}\n")
subprocess.run(copy_pool_dir, check=False)

# Set owner for the copied dir and files to daos_server:daos_server.
print("(F5: Set owner for the copied dir and files to daos_server:daos_server.)")
chown_cmd = f"sudo chown -R daos_server:daos_server /mnt/daos/{pool_uuid_5}"
clush_chown_cmd = ["clush", "-w", rank_to_ip[3], chown_cmd]
print(f"Command: {clush_chown_cmd}\n")
subprocess.run(clush_chown_cmd, check=False)

print("(F6: Remove vos-0 from rank 0.)")
pool_uuid_6 = label_to_uuid[POOL_LABEL_6]
rm_cmd = f"sudo rm -rf /mnt/daos/{pool_uuid_6}/vos-0"
clush_rm_cmd = ["clush", "-w", rank_to_ip[0], rm_cmd]
print(f"Command: {clush_rm_cmd}\n")
subprocess.run(clush_rm_cmd, check=False)

print("F7: Use ddb to show that the container is left in shards.")
pool_uuid_7 = label_to_uuid[POOL_LABEL_7]
ddb_cmd = f"sudo ddb -R \"ls\" /mnt/daos/{pool_uuid_7}/vos-0"
clush_ddb_cmd = ["clush", "-w", rank_to_ip[0], ddb_cmd]
print(f"Command: {clush_ddb_cmd}")
subprocess.run(clush_ddb_cmd, check=False)

# (optional) F3: Show pool directory at mount point to verify that the pool exists on
# engine.

print("\n5-2. Restart servers.")
system_start()

####################################################################
input("\n6. Show the faults inserted for each pool/container except "
      "F2, F6, F7. Hit enter...")
print(f"\n6-F1. Show dangling pool entry. {POOL_LABEL_1} doesn't exist on engine.")
# F3 part 1
print(f"\n6-F3. MS doesn't recognize {POOL_LABEL_3}.")
# F4 part 1
print(f"6-F4-1. Label ({POOL_LABEL_4}) in MS are corrupted with -fault added.")
list_pool(no_query=True)

# F2: (optional) Try to create a container, which will hang.

# F4 part 2
print(f"\n6-F4-2. Label ({POOL_LABEL_4}) in PS are still original.")
POOL_LABEL_4_FAULT = POOL_LABEL_4 + "-fault"
pool_get_prop(pool_label=POOL_LABEL_4_FAULT, properties="label")

# F5: Call dmg storage query usage to show that the pool is using more space.
print(f"\n6-F5. Print storage usage to show that {POOL_LABEL_5} is using more space. "
      f"Pool directory is copied to {rank_to_ip[F5_RANK_FAULT]}.")
storage_query_usage(host_list=f5_host_list)

# F8: Show inconsistency by getting the container label.
print("\n6-F8. Show container label inconsistency.")
cont_get_prop(pool_label=POOL_LABEL_8, cont_label=CONT_LABEL_8)
print(f"Error because container ({CONT_LABEL_8}) doesn't exist on container service.\n")

print(f"Container ({CONT_LABEL_8}) exists on pool service.")
cont_get_prop(pool_label=POOL_LABEL_8, cont_label="new-label", properties="label")

####################################################################
input("\n7. Enable checker. Hit enter...")
check_enable()

input("\n8. Start checker with interactive mode. Hit enter...")
check_set_policy(all_interactive=True)
print()
check_start()
print()
repeat_check_query()

####################################################################
input("\n8. Select repair options and repair. Hit enter...")
print("(Create UUID to sequence number.)")
uuid_to_seqnum = create_uuid_to_seqnum()
SEQ_NUM_1 = str(hex(uuid_to_seqnum[label_to_uuid[POOL_LABEL_1]]))
SEQ_NUM_2 = str(hex(uuid_to_seqnum[label_to_uuid[POOL_LABEL_2]]))
SEQ_NUM_3 = str(hex(uuid_to_seqnum[label_to_uuid[POOL_LABEL_3]]))
SEQ_NUM_4 = str(hex(uuid_to_seqnum[label_to_uuid[POOL_LABEL_4]]))
SEQ_NUM_5 = str(hex(uuid_to_seqnum[label_to_uuid[POOL_LABEL_5]]))
SEQ_NUM_6 = str(hex(uuid_to_seqnum[label_to_uuid[POOL_LABEL_6]]))
SEQ_NUM_7 = str(hex(uuid_to_seqnum[label_to_uuid[POOL_LABEL_7]]))
SEQ_NUM_8 = str(hex(uuid_to_seqnum[label_to_uuid[POOL_LABEL_8]]))

# F1: 1: Discard the dangling pool entry from MS [suggested].
print(f"\n{POOL_LABEL_1} - 1: Discard the dangling pool entry from MS [suggested].")
check_repair(sequence_num=SEQ_NUM_1, action="1")

# F2: 2: Start pool service under DICTATE mode from rank 1 [suggested].
print(f"\n{POOL_LABEL_2} - 2: Start pool service under DICTATE mode from rank 1 "
      f"[suggested].")
check_repair(sequence_num=SEQ_NUM_2, action="2")

# F3:
print(f"\n{POOL_LABEL_3} - 2: Re-add the orphan pool back to MS [suggested].")
check_repair(sequence_num=SEQ_NUM_3, action="2")

# F4: 2: Trust PS pool label.
print(f"\n{POOL_LABEL_4} - 2: Trust PS pool label.")
check_repair(sequence_num=SEQ_NUM_4, action="2")

# F5: 1: Discard the orphan pool shard to release space [suggested].
print(f"\n{POOL_LABEL_5} - 1: Discard the orphan pool shard to release space "
      f"[suggested].")
check_repair(sequence_num=SEQ_NUM_5, action="1")

# F6: 1: Change pool map for the dangling map entry [suggested].
print(f"\n{POOL_LABEL_6} - 1: Change pool map for the dangling map entry [suggested].")
check_repair(sequence_num=SEQ_NUM_6, action="1")

# F7: 1: Destroy the orphan container to release space [suggested].
print(f"\n{POOL_LABEL_7} - 1: Destroy the orphan container to release space [suggested].")
check_repair(sequence_num=SEQ_NUM_7, action="1")

# F8: 2: Trust the container label in container property.
print(f"\n{POOL_LABEL_8} - 2: Trust the container label in container property.")
check_repair(sequence_num=SEQ_NUM_8, action="2")

print()
repeat_check_query()

print("\n9. Disable the checker.")
check_disable()

####################################################################
input("\n13. Show the issues fixed. Hit enter...")
print(f"13-F1. Dangling pool ({POOL_LABEL_1}) was removed.")
print(f"13-F3. Orphan pool ({POOL_LABEL_3}) was reconstructed.")
list_pool()

print(f"13-F2. Create a container on {POOL_LABEL_2}. Pool can be started now, so it "
      f"should succeed.")
CONT_LABEL_2 = CONT_LABEL + "_2"
create_container(pool_label=POOL_LABEL_2, cont_label=CONT_LABEL_2)
# (optional) Show that rdb-pool file in rank 0 and 2 are recovered.

print(f"\n13-F4. Label inconsistency for {POOL_LABEL_4} was resolved. "
      f"See pool list above.")
pool_get_prop(pool_label=POOL_LABEL_4, properties="label")

# F5: Call dmg storage query usage to verify the storage was reclaimed. - Not working due
# to a bug. Instead, show that pool directory on dst node (rank 3 for 4-VM) was removed.
print(f"\n13-F5-1. Print storage usage to show that storage used by {POOL_LABEL_5} is "
      f"reclaimed after pool directory is removed from {rank_to_ip[F5_RANK_FAULT]}.")
storage_query_usage(host_list=f5_host_list)

print(f"\n13-F5-2. {label_to_uuid[POOL_LABEL_5]} pool directory on rank 3 "
      f"({rank_to_ip[3]}) was removed.")
LS_CMD = "sudo ls /mnt/daos"
clush_ls_cmd = ["clush", "-w", rank_to_ip[3], LS_CMD]
print(f"Command: {clush_ls_cmd}\n")
subprocess.run(clush_ls_cmd, check=False)

print(f"\n13-F6. {POOL_LABEL_6} has one less ranks (4 -> 3).")
pool_query(pool_label=POOL_LABEL_6)
# (optional) Reintegrate rank 1 on pool 6. Wait for rebuild to finish. Then verify the
# target count.

# F8: Verify that the inconsistency is fixed. The label is back to the original.
print(f"\n13-F8. Show container label inconsistency for {CONT_LABEL_8} was fixed.")
cont_get_prop(pool_label=POOL_LABEL_8, cont_label=CONT_LABEL_8, properties="label")

# F7: Stop server. Call the same ddb command to verify that the container is removed from
# shard.
print(f"\n13-F7. Use ddb to verify that the container in {POOL_LABEL_8} is removed "
      f"from shards.")
system_stop()
print(f"Command: {clush_ddb_cmd}")
subprocess.run(clush_ddb_cmd, check=False)
