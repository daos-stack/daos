"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import subprocess
from demo_utils import format_storage, create_pool, inject_fault_mgmt, list_pool,\
    check_enable, check_start, check_disable, repeat_check_query


POOL_SIZE = "4GB"
POOL_LABEL = "tank"

def pool_get_prop(pool_label, properties=None):
    """Call dmg pool get-prop

    Args:
        pool_label (str): Pool label.
        properties (str): Comma-separated list of properties.
    """
    get_prop_cmd = ["dmg", "pool", "get-prop", pool_label]
    if properties:
        get_prop_cmd.append(properties)
    command = " ".join(get_prop_cmd)
    print(f"Command: {command}")
    subprocess.run(get_prop_cmd, check=False)


print("Pass 3: svcl list inconsistency")

PARSER = argparse.ArgumentParser()
PARSER.add_argument("-l", "--hostlist", required=True, help="List of hosts to format")
ARGS = vars(PARSER.parse_args())

HOSTLIST = ARGS["hostlist"]
input(f"\n1. Format storage on {HOSTLIST}. Hit enter...")
format_storage(host_list=HOSTLIST)

input("\n2. Create a 4GB pool. Hit enter...")
create_pool(pool_size=POOL_SIZE, pool_label=POOL_LABEL)

input("3. Corrupt the svcl in the MS copy of the PS metadata. Hit enter...")
inject_fault_mgmt(pool_label=POOL_LABEL, fault_type="CIC_POOL_BAD_SVCL")

input("\n4-1. svcl list in MS is corrupted while PS isn't. Hit enter...")
list_pool(verbose=True)
pool_get_prop(pool_label=POOL_LABEL, properties="svc_list")
print("\n4-2. SvcReps from MS shows 0 while PS shows [0-1].")

input("\n5. Enable checker. Hit enter...")
check_enable()

input("\n6. Start checker. Hit enter...")
check_start()

print("\n7-1. Query the checker.")
repeat_check_query()

print("\n7-2. Checker shows the svcl inconsistency was repaired.")

input("\n8. Disable the checker. Hit enter...")
check_disable()

input("\n9. Verify that the svcl in MS is fixed. Hit enter...")
list_pool(verbose=True)
