"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import argparse
import subprocess


# Stop daos_server, unmount, and start daos_server for given hosts.
systemctl_start_cmd = "sudo systemctl start daos_server"
systemctl_stop_cmd = "sudo systemctl stop daos_server"
umount_cmd = "sudo umount /mnt/daos"
prepare_cmd = "{}; {}; {}".format(systemctl_stop_cmd, umount_cmd, systemctl_start_cmd)

PARSER = argparse.ArgumentParser()
PARSER.add_argument(
    "-l", "--hostlist", required=True, help="List of comma-separated hosts")
ARGS = vars(PARSER.parse_args())

HOSTLIST = ARGS["hostlist"]
remote_prepare_cmd = ["clush", "-w", HOSTLIST, prepare_cmd]
subprocess.run(remote_prepare_cmd, check=False)
