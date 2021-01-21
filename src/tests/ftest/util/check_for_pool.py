#!/usr/bin/python
"""
  (C) Copyright 2017-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from __future__ import print_function

from general_utils import run_command


def check_for_pool(host, uuid):
    """Check if pool folder exist on server.

    Args:
        host: Server host name
        uuid: Pool uuid to check if exists

    Returns:
        int: subprocess return code

    """
    cmd = "/usr/bin/ssh {} test -e /mnt/daos/{}".format(host, uuid)
    result = run_command(cmd, raise_exception=False)
    if result.exit_status == 0:
        print("{} exists".format(uuid))
    else:
        print("{} does not exist".format(uuid))
    return result.exit_status


def cleanup_pools(hosts):
    """Clean up the pool and content from /mnt/daos/.

    Args:
        hosts[list]: Lists of servers name

    """
    for host in hosts:
        run_command("/usr/bin/ssh {} rm -rf /mnt/daos/*".format(host))
