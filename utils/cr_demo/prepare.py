"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import subprocess


# Stop daos_server, unmount, and start daos_server for given hosts.
SYSTEMCTL_START_CMD = "sudo systemctl start daos_server"
SYSTEMCTL_STOP_CMD = "sudo systemctl stop daos_server"
UMOUNT_CMD = "sudo umount /mnt/daos"
prepare_cmd = f"{SYSTEMCTL_STOP_CMD}; {UMOUNT_CMD}; {SYSTEMCTL_START_CMD}"

PARSER = argparse.ArgumentParser()
PARSER.add_argument(
    "-l", "--hostlist", required=True, help="List of comma-separated hosts")
ARGS = vars(PARSER.parse_args())

HOSTLIST = ARGS["hostlist"]
remote_prepare_cmd = ["clush", "-w", HOSTLIST, prepare_cmd]
subprocess.run(remote_prepare_cmd, check=False)
