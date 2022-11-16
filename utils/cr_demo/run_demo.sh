#!/bin/bash

dmg_pool_list="dmg pool list"
dmg_check_enable="dmg check enable"
dmg_check_start="dmg check start"
dmg_check_query="dmg check query"
dmg_check_disable="dmg check disable"
POOL_SIZE="4G"
POOL_LABEL="tank"

format_storage() {
    hostlist=$1
    dmg_cmd="dmg storage format --host-list=$hostlist"
    echo "Command: $dmg_cmd"
    eval $dmg_cmd
}

create_pool() {
    pool_size=$1
    pool_label=$2
    dmg_cmd="dmg pool create --size=$pool_size --label=$pool_label"
    echo "Command: $dmg_cmd"
    eval $dmg_cmd
}

inject_fault_mgmt() {
    pool_label=$1
    fault_type=$2
    dmg_cmd="dmg faults mgmt-svc pool $pool_label $fault_type"
    echo "Command: $dmg_cmd"
    eval $dmg_cmd
}

if [ $# == 0 ]
then
    echo "Please supply at least one hostname."
    exit
fi

# Prepare hostnames separated by comma for format, etc.
host_list=""
for hostname in $*
do
    if [ -z $host_list ]
    then
        host_list=$hostname
    else
        host_list="$host_list,$hostname"
    fi
done

# Pass 1: Pool is on ranks, but not in MS - Orphan pool
echo "1. Format storage on $host_list. Hit enter..."
read
format_storage $host_list
# Wait for the storage to be ready before creating a pool.
sleep 5

echo $'\n2. Create a 4GB pool. Hit enter...'
read
create_pool $POOL_SIZE $POOL_LABEL

echo $'\n3. Remove PS entry on MS. Hit enter...'
read
inject_fault_mgmt $POOL_LABEL "CIC_POOL_NONEXIST_ON_MS"

echo $'\n4. MS doesn\'t recognize any pool (it exists on engine). Hit enter...'
read
echo "Command: $dmg_pool_list"
eval $dmg_pool_list

echo $'\n5. Enable and start checker. Hit enter...'
read
echo "Command: $dmg_check_enable"
eval $dmg_check_enable
echo "Command: $dmg_check_start"
eval $dmg_check_start

echo $'\n6-1. Query the checker. Hit y to query, n to proceed to next step.'
while :
do
    read input
    if [ $input = "y" ]
    then
        echo "Command: $dmg_check_query"
        eval $dmg_check_query
    elif [ $input = "n" ]
    then
        break
    else
        echo "Please enter y or n."
    fi
done

echo $'\n6-2. Checker shows the inconsistency that was repaired.'

echo $'\n7. Disable the checker. Hit enter...'
read
echo "Command: $dmg_check_disable"
eval $dmg_check_disable

echo $'\n8. Verify that the missing pool was reconstructed. Hit enter...'
read
echo "Command: $dmg_pool_list"
eval $dmg_pool_list
