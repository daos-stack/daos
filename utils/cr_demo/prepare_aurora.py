"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import subprocess


# Stop daos_server, unmount, and start daos_server for given hosts.
SYSTEMCTL_START_CMD = "sudo systemctl start daos_server; sudo systemctl start daos_agent"
SYSTEMCTL_STOP_CMD = "sudo systemctl stop daos_server; sudo systemctl stop daos_agent"
UMOUNT_CMD = "sudo umount /mnt/daos; sudo umount /mnt/daos0; sudo umount /mnt/daos1"
WIPEFS_CMD = "sudo wipefs -a /dev/pmem0; sudo wipefs -a /dev/pmem1"
# Need to clear the log files to search engine rank and PID.
CLEAR_LOGS_CMD = "sudo rm /var/tmp/daos_testing/daos_*"
prepare_cmd = (f"{SYSTEMCTL_STOP_CMD}; {UMOUNT_CMD}; {WIPEFS_CMD}; {CLEAR_LOGS_CMD}; "
               f"{SYSTEMCTL_START_CMD}")

PARSER = argparse.ArgumentParser()
PARSER.add_argument(
    "-l", "--hostlist", required=True, help="List of comma-separated hosts")
ARGS = vars(PARSER.parse_args())

HOSTLIST = ARGS["hostlist"]
remote_prepare_cmd = ["clush", "-w", HOSTLIST, prepare_cmd]
subprocess.run(remote_prepare_cmd, check=False)
