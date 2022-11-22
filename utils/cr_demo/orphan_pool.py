"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import subprocess
from demo_utils import format_storage, create_pool, inject_fault_mgmt, list_pool,\
    enable_checker, start_checker, disable_checker, repeat_check_query


POOL_SIZE = "4GB"
POOL_LABEL = "tank"

def list_directory(dir_path):
    """Print given directory path.

    Args:
        dir_path (str): Directory path.
    """
    ls_cmd = ["sudo", "ls", "-l", dir_path]
    command = " ".join(ls_cmd)
    print(f"Command: {command}")
    subprocess.run(ls_cmd, check=False)

print("Pass 1: Pool is on ranks, but not in MS - Trust PS")

PARSER = argparse.ArgumentParser()
PARSER.add_argument("-l", "--hostlist", required=True, help="List of hosts to format")
ARGS = vars(PARSER.parse_args())

HOSTLIST = ARGS["hostlist"]
input(f"\n1. Format storage on {HOSTLIST}. Hit enter...")
format_storage(host_list=HOSTLIST)

input("\n2. Create a 4GB pool. Hit enter...")
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL)

input("3. Remove PS entry on MS. Hit enter...")
inject_fault_mgmt(pool_label=POOL_LABEL, fault_type="CIC_POOL_NONEXIST_ON_MS")

input("\n4. MS doesn\'t recognize any pool. Hit enter...")
list_pool()

input("\n5. The pool still exists on engine. Hit enter...")
list_directory("/mnt/daos")

input("\n6. Enable and start checker. Hit enter...")
enable_checker()
start_checker()

print("\n7-1. Query the checker.")
repeat_check_query()

print("7-2. Checker shows the inconsistency that was repaired.")

input("\n8. Disable the checker. Hit enter...")
disable_checker()

input("\n9. Verify that the missing pool was reconstructed. Hit enter...")
list_pool()
