"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import re
import subprocess  # nosec
import time
from collections import defaultdict

import yaml
from ClusterShell.NodeSet import NodeSet
from demo_utils import (check_disable, check_enable, check_repair, check_set_policy, check_start,
                        cont_get_prop, convert_list_to_str, create_container, create_pool,
                        create_uuid_to_seqnum, format_storage, inject_fault_daos,
                        inject_fault_mgmt, inject_fault_pool, list_pool, pool_get_prop,
                        repeat_check_query, storage_query_usage, system_query, system_start,
                        system_stop)

# Run this script on Aurora node as user. e.g.,
# python3 run_demo_aurora.py -l aurora-daos-[0001-0100]

TEST_CMD = "sudo date"
test_cmd_list = TEST_CMD.split(" ")
print(f"Check sudo works by calling: {TEST_CMD}")
subprocess.run(test_cmd_list, check=False)

POOL_SIZE = "5T"
POOL_SIZE_F5 = "3T"
POOL_LABEL = "tank"
CONT_LABEL = "bucket"
# Number of seconds to wait for engines to start for 1 group setup.
FORMAT_SLEEP_SEC = 35

print("\nF1: Dangling pool")
print("F2: Lost the majority of pool service replicas")
print("F3: Orphan pool")
print("F4: Inconsistent pool label between MS and PS")
print("F5: Orphan pool shard")
print("F6: Dangling pool map")
print("F7: Orphan container")
print("F8: Inconsistent container label between CS and container property")

PARSER = argparse.ArgumentParser()
PARSER.add_argument(
    "-l", "--hostlist", required=True, help="List of hosts to run the demo")
ARGS = vars(PARSER.parse_args())

HOSTLIST = ARGS["hostlist"]

print(f"\n1. Format storage on {HOSTLIST}.")
format_storage(host_list=HOSTLIST)

print(f"\nWait for {FORMAT_SLEEP_SEC} sec for format...")
time.sleep(FORMAT_SLEEP_SEC)

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
node_set = NodeSet(HOSTLIST)
hostlist = list(node_set)
print(f"\n{len(hostlist)} nodes; {RANK_COUNT} ranks; {JOINED_COUNT} joined")

# Create rank to mount point map and host to ranks map for F2 and F5.
# 1. scp daos_control.log from all nodes to here, where this script runs. scp the local
# file as well. Add hostname to the end of the file name. The log contains rank and PID.
# Number of nodes used for F2.
NODE_COUNT = 2
for i in range(NODE_COUNT):
    scp_cmd_list = ["scp", f"{hostlist[i]}:/var/tmp/daos_testing/daos_control.log",
                    f"/var/tmp/daos_testing/daos_control_{hostlist[i]}.log"]
    subprocess.run(scp_cmd_list, check=False)

# 2. Determine the rank to PID mapping from the control logs. In addition, determine the
# host to ranks mapping for creating the pool. We need to know the four ranks for the
# first two nodes. We'll use many nodes in Aurora, but only two nodes for F2.
rank_to_pid = {}
host_to_ranks = defaultdict(list)
SEARCH_STR = r"DAOS I/O Engine.*process (\d+) started on rank (\d+)"
for i in range(NODE_COUNT):
    with open(
        f"/var/tmp/daos_testing/daos_control_{hostlist[i]}.log", "r",
            encoding="utf-8") as file:
        for line in file:
            match = re.findall(SEARCH_STR, line)
            if match:
                print(match)
                pid = int(match[0][0])
                rank = int(match[0][1])
                rank_to_pid[rank] = pid
                host_to_ranks[hostlist[i]].append(rank)

# 3. Determine the PID to mount point mapping by calling ps ax and search for daos_engine.
# Sample line:
# 84877 ?        SLl  102:04 /usr/bin/daos_engine -t 8 -x 1 -g daos_server -d
# /var/run/daos_server -T 2 -n /mnt/daos1/daos_nvme.conf -p 1 -I 1 -r 8192 -H 2 -s
# /mnt/daos1
pid_to_mount = {}
MOUNT_0 = "/mnt/daos0"
MOUNT_1 = "/mnt/daos1"
for i in range(NODE_COUNT):
    clush_ps_ax = ["clush", "-w", hostlist[i], "ps ax"]
    result = subprocess.check_output(clush_ps_ax)
    result_list = result.decode("utf-8").split("\n")
    for result in result_list:
        if "daos_engine" in result:
            print(result)
            if MOUNT_0 in result:
                pid = re.split(r"\s+", result)[1]
                pid = int(pid)
                pid_to_mount[pid] = MOUNT_0
            elif MOUNT_1 in result:
                pid = re.split(r"\s+", result)[1]
                pid = int(pid)
                pid_to_mount[pid] = MOUNT_1

# 4. Determine the four ranks in hostlist[0] and hostlist[1] to create F2 pool.
f2_ranks = []
f2_ranks.extend(host_to_ranks[hostlist[0]])
f2_ranks.extend(host_to_ranks[hostlist[1]])
# Ranks in the map are int, so convert them to string and separate them with comma.
F2_RANKS_STR = convert_list_to_str(original_list=f2_ranks, separator=",")

# 5. Determine the two ranks in hostlist[0] to create F5 pool.
f5_ranks = []
f5_ranks.extend(host_to_ranks[hostlist[0]])
# Ranks in the map are int, so convert them to string and separate them with comma.
F5_RANKS_STR = convert_list_to_str(original_list=f5_ranks, separator=",")

# Add input here to make sure all ranks are joined before starting the script.
input("\n2. Create 8 pools and containers. Hit enter...")
POOL_LABEL_1 = POOL_LABEL + "_F1"
POOL_LABEL_2 = POOL_LABEL + "_F2"
POOL_LABEL_3 = POOL_LABEL + "_F3"
POOL_LABEL_4 = POOL_LABEL + "_F4"
POOL_LABEL_5 = POOL_LABEL + "_F5"
POOL_LABEL_6 = POOL_LABEL + "_F6"
POOL_LABEL_7 = POOL_LABEL + "_F7"
POOL_LABEL_8 = POOL_LABEL + "_F8"
CONT_LABEL_7 = CONT_LABEL + "_F7"
CONT_LABEL_8 = CONT_LABEL + "_F8"

# F1. CIC_POOL_NONEXIST_ON_ENGINE - dangling pool
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL_1)
# F2. CIC_POOL_LESS_SVC_WITHOUT_QUORUM
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL_2, ranks=F2_RANKS_STR, nsvc="3")
# F3. CIC_POOL_NONEXIST_ON_MS - orphan pool
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL_3)
# F4. CIC_POOL_BAD_LABEL - inconsistent pool label between MS and PS
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL_4)
# F5. CIC_ENGINE_NONEXIST_IN_MAP - orphan pool shard
create_pool(pool_size=POOL_SIZE_F5, pool_label=POOL_LABEL_5, ranks=F5_RANKS_STR)
# F6. CIC_ENGINE_HAS_NO_STORAGE - dangling pool map
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL_6)
# F7. CIC_CONT_NONEXIST_ON_PS - orphan container
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL_7)
create_container(pool_label=POOL_LABEL_7, cont_label=CONT_LABEL_7)
print()
# F8. CIC_CONT_BAD_LABEL
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL_8)
create_container(pool_label=POOL_LABEL_8, cont_label=CONT_LABEL_8)

print("(Create label to UUID mapping and obtain service replicas for F2.)")
label_to_uuid = {}
f2_service_replicas = []
stdout = list_pool(json=True)
generated_yaml = yaml.safe_load(stdout)
for pool in generated_yaml["response"]["pools"]:
    label_to_uuid[pool["label"]] = pool["uuid"]
    # Collect service replicas for F2.
    if pool["label"] == POOL_LABEL_2:
        f2_service_replicas = pool["svc_reps"]

print(f"\n(F2 service replicas = {f2_service_replicas})")

print(f"\n3-F5. Print storage usage to show original usage of {POOL_LABEL_5}. "
      f"Pool is created on {hostlist[0]}.")
# F5 pool is created on hostlist[0] ranks, but we'll copy the pool dir from there to one
# of the ranks in hostlist[1], so show both.
f5_host_list = f"{hostlist[0]},{hostlist[1]}"
storage_query_usage(host_list=f5_host_list)

print("\n4. Inject fault with dmg for F1, F3, F4, F7, F8.")
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

input("\n5-1. Stop servers to manipulate for F2, F5, F6, F7. Hit enter...")
system_stop(force=True)

# F2: Destroy tank_2 rdb-pool on two of the three service replicas. Call them rank a and
# b. Select the first two service replicas.
svc_rep_a = f2_service_replicas[0]
svc_rep_b = f2_service_replicas[1]
rank_a_ip = rank_to_ip[svc_rep_a]
rank_b_ip = rank_to_ip[svc_rep_b]
rank_a_mount = pid_to_mount[rank_to_pid[svc_rep_a]]
rank_b_mount = pid_to_mount[rank_to_pid[svc_rep_b]]
rm_rank_a = f"sudo rm {rank_a_mount}/{label_to_uuid[POOL_LABEL_2]}/rdb-pool"
rm_rank_b = f"sudo rm {rank_b_mount}/{label_to_uuid[POOL_LABEL_2]}/rdb-pool"
clush_rm_rank_a = ["clush", "-w", rank_a_ip, rm_rank_a]
clush_rm_rank_b = ["clush", "-w", rank_b_ip, rm_rank_b]
print("(F2: Destroy tank_F2 rdb-pool on rank a and b.)")
print(f"Command for rank a: {clush_rm_rank_a}\n")
print(f"Command for rank b: {clush_rm_rank_b}\n")
subprocess.run(clush_rm_rank_a, check=False)
subprocess.run(clush_rm_rank_b, check=False)

# F5: Copy tank_5 pool directory from /mnt/daos1 in hostlist[0] to /mnt/daos0 in
# hostlist[1]. Match owner. (Mount points are arbitrary.)
# In order to copy the pool directory without password, there are two things to set up.
# 1. Since we're running rsync as user, update the mode of the source pool directory as
# below.
# Set 777 for /mnt/daos1 and /mnt/daos1/<pool_5>/* i.e.,
# chmod 777 /mnt/daos1; chmod -R 777 /mnt/daos1/<pool_5>
# 2. Update mode of the destination mount point to 777. e.g.,
# clush -w <dst_host> "sudo chmod 777 /mnt/daos0"

# Alternatively, we can generate public-private key pair for root and call scp with sudo.
# Then we don't need to do step 2 (update mode to 777).

print("(F5: Update mode of the source pool directory.)")
pool_uuid_5 = label_to_uuid[POOL_LABEL_5]
chmod_cmd = f"sudo chmod 777 /mnt/daos1; sudo chmod -R 777 /mnt/daos1/{pool_uuid_5}"
clush_chmod_cmd = ["clush", "-w", hostlist[0], chmod_cmd]
print(f"Command: {clush_chmod_cmd}\n")
subprocess.run(clush_chmod_cmd, check=False)

print("(F5: Update mode of the destination mount point.)")
CHMOD_CMD = "sudo chmod 777 /mnt/daos0"
clush_chmod_cmd = ["clush", "-w", hostlist[1], CHMOD_CMD]
print(f"Command: {clush_chmod_cmd}\n")
subprocess.run(clush_chmod_cmd, check=False)

# Since we're sending each file (vos-0 to 15 + rdb-pool) one at a time rather than the
# whole pool directory, we need to create the destination fake pool directory first.
print("(F5: Create a fake pool directory at the destination mount point.)")
mkdir_cmd = f"sudo mkdir /mnt/daos0/{pool_uuid_5}"
clush_mkdir_cmd = ["clush", "-w", hostlist[1], mkdir_cmd]
print(f"Command: {clush_mkdir_cmd}\n")
subprocess.run(clush_mkdir_cmd, check=False)

print("(F5: Update mode of the fake pool directory at destination.)")
chmod_cmd = f"sudo chmod 777 /mnt/daos0/{pool_uuid_5}"
clush_chmod_cmd = ["clush", "-w", hostlist[1], chmod_cmd]
print(f"Command: {clush_chmod_cmd}\n")
subprocess.run(clush_chmod_cmd, check=False)

# Run the following xargs + rsync command on hostlist[0] using clush:
# ls /mnt/daos1/<pool_uuid_5> | xargs --max-procs=16 -I% \
# rsync -avz /mnt/daos1/<pool_uuid_5>/% hostlist[1]:/mnt/daos0/<pool_uuid_5>

# 1. The initial ls command lists the content of the pool directory, which contains 16 vos
# files (because there are 16 targets) and rdb-pool file.
# 2. By using xargs, each item of the ls output is passed into rsync and the rsync
# commands are executed in parallel. i.e., each file is sent by separate rsync process in
# parallel.

# * We use --max-procs=16 to support at most 16 rsync processes to run in parallel.
# * -I% means replace % in the following rsync command by the output of ls. i.e., file
# name.
# * rsync -avz means archive, verbose, and compress. By using compress, we can
# significantly reduce the size of the data and the transfer time.
# * By running rsync in parallel, we can significantly reduce the transfer time. e.g., For
# a 2TB pool with 8 targets per engine, each vos file size is about 7G (rdb-pool is
# smaller). If we run a simple rsync, which runs serially, it takes 1 min 50 sec.
# However, if we run them in parallel, it's reduced to 24 sec.
print(f"(F5: Copy pool directory from {hostlist[0]} to {hostlist[1]}.)")
xargs_rsync_cmd = (f"ls /mnt/daos1/{pool_uuid_5} | xargs --max-procs=16 -I% "
                   f"rsync -avz /mnt/daos1/{pool_uuid_5}/% "
                   f"{hostlist[1]}:/mnt/daos0/{pool_uuid_5}")
clush_xargs_rsync_cmd = ["clush", "-w", hostlist[0], xargs_rsync_cmd]
print(f"Command: {clush_xargs_rsync_cmd}\n")
subprocess.run(clush_xargs_rsync_cmd, check=False)

print("(F5: Set owner for the copied dir and files to daos_server:daos_server.)")
chown_cmd = f"sudo chown -R daos_server:daos_server /mnt/daos0/{pool_uuid_5}"
clush_chown_cmd = ["clush", "-w", hostlist[1], chown_cmd]
print(f"Command: {clush_chown_cmd}\n")
subprocess.run(clush_chown_cmd, check=False)

print("(F6: Remove vos-0 from one of the nodes.)")
pool_uuid_6 = label_to_uuid[POOL_LABEL_6]
rm_cmd = f"sudo rm -rf /mnt/daos0/{pool_uuid_6}/vos-0"
# Remove vos-0 from /mnt/daos0 in rank 0 node. Note that /mnt/daos0 may not be mapped to
# rank 0. Rank 0 is mapped to either daos0 or daos1. However, we don't care for the
# purpose of testing dangling pool map.
clush_rm_cmd = ["clush", "-w", rank_to_ip[0], rm_cmd]
print(f"Command: {clush_rm_cmd}\n")
subprocess.run(clush_rm_cmd, check=False)

print("F7: Use ddb to show that the container is left in shards.")
pool_uuid_7 = label_to_uuid[POOL_LABEL_7]
# Run ddb on /mnt/daos0 of rank 0 node.
ddb_cmd = f"sudo ddb /mnt/daos0/{pool_uuid_7}/vos-0 ls"
# ddb with clush causes some authentication error. tank_F7 is created across all ranks, so
# just run ddb locally as a workaround.
ddb_cmd_list = ddb_cmd.split(" ")
print(f"Command: {ddb_cmd}")
subprocess.run(ddb_cmd_list, check=False)

# (optional) F3: Show pool directory at mount point to verify that the pool exists on
# engine.

print("\n5-2. Restart servers.")
system_start()

input("\n6. Show the faults injected for each pool/container for F1, F3, F4, F5, F8. "
      "Hit enter...")
print(f"6-F1. Show dangling pool entry for {POOL_LABEL_1}.")
# F3 part 1
print(f"6-F3. MS doesn't recognize {POOL_LABEL_3}.")
# F4 part 1
print(f"6-F4-1. Label ({POOL_LABEL_4}) in MS is corrupted with -fault added.")
list_pool(no_query=True)

# F2: (optional) Try to create a container, which will hang.

# F4 part 2
print(f"\n6-F4-2. Label ({POOL_LABEL_4}) in PS is still original.")
POOL_LABEL_4_FAULT = POOL_LABEL_4 + "-fault"
pool_get_prop(pool_label=POOL_LABEL_4_FAULT, properties="label")

# F5: Call dmg storage query usage to show that the pool is using more space.
print(f"\n6-F5. Print storage usage to show that {POOL_LABEL_5} is using more space. "
      f"Pool directory is copied to {hostlist[1]}.")
storage_query_usage(host_list=f5_host_list)

# F8: Show inconsistency by getting the container label.
print("\n6-F8. Show container label inconsistency.")
cont_get_prop(pool_label=POOL_LABEL_8, cont_label=CONT_LABEL_8)
print(f"Error because container ({CONT_LABEL_8}) doesn't exist on container service.\n")

print(f"Container ({CONT_LABEL_8}) exists on property.")
cont_get_prop(pool_label=POOL_LABEL_8, cont_label="new-label", properties="label")

input("\n7. Enable checker. Hit enter...")
system_stop(force=True)
check_enable()

input("\n8. Start checker with interactive mode. Hit enter...")
check_set_policy(all_interactive=True)
print()
check_start()
print()
repeat_check_query()

input("\n8-1. Select repair options for F1 to F4. Hit enter...")
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

# F3:2: Re-add the orphan pool back to MS [suggested].
print(f"\n{POOL_LABEL_3} - 2: Re-add the orphan pool back to MS [suggested].")
check_repair(sequence_num=SEQ_NUM_3, action="2")

# F4: 2: Trust PS pool label.
print(f"\n{POOL_LABEL_4} - 2: Trust PS pool label.")
check_repair(sequence_num=SEQ_NUM_4, action="2")

print()
# Call dmg check query until n is entered.
repeat_check_query()

input("\n8-2. Select repair options for F5 to F8. Hit enter...")
# F5: 1: Discard the orphan pool shard to release space [suggested].
print(f"\n{POOL_LABEL_5} - 1: Discard the orphan pool shard to release space "
      f"[suggested].")
check_repair(sequence_num=SEQ_NUM_5, action="1")

# F6: 1: Change pool map for the dangling map entry [suggested].
print(f"\n{POOL_LABEL_6} - 1: Change pool map for the dangling map entry as down "
      f"[suggested].")
check_repair(sequence_num=SEQ_NUM_6, action="1")

# F7: 1: Destroy the orphan container to release space [suggested].
print(f"\n{POOL_LABEL_7} - 1: Destroy the orphan container to release space [suggested].")
check_repair(sequence_num=SEQ_NUM_7, action="1")

# F8: 2: Trust the container label in container property.
print(f"\n{POOL_LABEL_8} - 2: Trust the container label in container property.")
check_repair(sequence_num=SEQ_NUM_8, action="2")

print()
# Call dmg check query until n is entered.
repeat_check_query()

print("\n9. Disable the checker.")
check_disable()
system_start()

print("\nRun show_fixed_aurora.py to show the issues fixed...")
