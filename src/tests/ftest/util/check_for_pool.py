#!/usr/bin/python
"""
  (C) Copyright 2017-2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from __future__ import print_function

from general_utils import run_command


def check_for_pool(host, uuid):
    """Check if pool folder exist on server.

    Args:
        host (str): Server host name
        uuid (str): Pool uuid to check if exists

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
    """Cleanup the pool and content from /mnt/daos/.

    Args:
        hosts[list]: Lists of servers name
    return"
        None
    """
    for host in hosts:
        run_command("/usr/bin/ssh {} rm -rf /mnt/daos/*".format(host))
