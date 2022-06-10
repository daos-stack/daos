#!/usr/bin/python
"""
  (C) Copyright 2017-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


from general_utils import run_command, check_file_exists


def check_for_pool(host, uuid):
    """Check if pool folder exist on server.

    Args:
        host: Server host name
        uuid: Pool uuid to check if exists

    Returns:
        bool: True if pool folder exists, False otherwise

    """
    pool_dir = "/mnt/daos/{}".format(uuid)
    result = check_file_exists(host, pool_dir, directory=True, sudo=True)
    if result[0]:
        print("{} exists on {}".format(pool_dir, host))
    else:
        print("{} does not exist on {}".format(pool_dir, host))
    return result[0]


def cleanup_pools(hosts):
    """Clean up the pool and content from /mnt/daos/.

    Args:
        hosts[list]: Lists of servers name

    """
    for host in hosts:
        run_command("/usr/bin/ssh {} sudo rm -rf /mnt/daos/*".format(host))
