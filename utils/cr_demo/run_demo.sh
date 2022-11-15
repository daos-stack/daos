#!/bin/bash

dmg_pool_list=`dmg pool list`
dmg_check_enable=`dmg check enable`
dmg_check_start=`dmg check start`
dmg_check_query=`dmg check query`
dmg_check_disable=`dmg check disable`
POOL_SIZE="4G"
POOL_LABEL="tank"

format_storage() {
    hostlist=$1
    dmg_cmd=`dmg storage format --host-list=$hostlist`
    dmg_cmd
}

create_pool() {
    pool_size=$1
    pool_label=$2
    dmg_cmd=`dmg pool create --size=$pool_size --label=$pool_label`
    dmg_cmd
}

inject_fault_mgmt() {
    pool_label=$1
    fault_type=$2
    dmg_cmd=`dmg fault mgmt-svc pool $pool_label $fault_type`
    dmg_cmd
}

host1=$1
host2=$2

# Pass 1: Pool is on ranks, but not in MS - Orphan pool
echo "1. Format storage. Hit enter..."
read
hosts="$1,$2"
format_storage $hosts

echo "2. Create a 4GB pool. Hit enter..."
read
create_pool $POOL_SIZE $POOL_LABEL

echo "3. Remove PS entry on MS. Hit enter..."
read
inject_fault_mgmt $POOL_LABEL "CIC_POOL_NONEXIST_ON_MS"

echo "4. MS doesn't recognize any pool (it exists on engine). Hit enter..."
read
dmg_pool_list

echo "5. Enable and start checker. Hit enter..."
read
dmg_check_enable
dmg_check_start

echo "6-1. Query the checker. Wait for a few seconds and hit enter..."
read
dmg_check_query

echo "6-2. Checker shows the inconsistency that was repaired."

echo "7. Disable the checker. Hit enter..."
read
dmg_check_disable

echo "8. Verify that the missing pool was reconstructed. Hit enter..."
read
dmg_pool_list
