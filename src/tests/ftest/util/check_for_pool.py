#!/usr/bin/python
'''
  (C) Copyright 2017-2019 Intel Corporation.

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
'''
from __future__ import print_function

import subprocess

def check_for_pool(host, uuid):
    """
    Function to check if pool folder exist on server
    Args:
        host: Server host name
        uuid: Pool uuid to check if exists
    return:
        resp: subprocess return code
    """
    cmd = "test -e /mnt/daos/" + uuid
    resp = subprocess.call(["ssh", host, cmd])
    if resp == 0:
        print ('%s exists' %uuid)
    else:
        print ('%s does not exist' %uuid)
    return resp

def cleanup_pools(hosts):
    """
    To cleanup the pool and content from /mnt/daps/
    Args:
        hosts[list]: Lists of servers name
    return"
        None
    """
    for host in hosts:
        cmd = "rm -rf /mnt/daos/*"
        subprocess.call(["ssh", host, cmd])
