#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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

import os
import re
import random
from general_utils import pcmd

def acl_entry(usergroup, name, perm, permissions):
    """Create a daos acl entry for the specified user or group and permission

    Args:
        usergroup (str): user or group.
        name (str): user or group name to be created.
        permission (str): permission to be created.

    Return:
        str: daos pool acl entry.

    """
    if perm == "random":
        perm = random.choice(permissions)
    if perm == "nonexist":
        return ""
    if "group" in usergroup:
        entry = "A:G:" + name + "@:" + perm
    else:
        entry = "A::" + name + "@:" + perm
    return entry


def acl_principal(usergroup, name):
    """Create a daos ace principal for the specified user or group.

    Args:
        usergroup (str): user or group.
        name (str): user or group name to be created.

    Return:
        str: daos acl entry.

    """
    if "group" in usergroup:
        entry = "g:" + name + "@"
    else:
        entry = "u:" + name + "@"
    return entry


def add_del_user(hosts, ba_cmd, user):
    """Add or delete the daos user and group on host by sudo command.

    Args:
        hosts (list): list of host.
        ba_cmd (str): linux bash command to create user or group.
        user (str): user or group name to be created or cleaned.

    """
    bash_cmd = os.path.join("/usr/sbin", ba_cmd)
    homedir = ""
    if "usermod" not in ba_cmd and "user" in ba_cmd:
        homedir = "-r"
    cmd = " ".join(("sudo", bash_cmd, homedir, user))
    print("     =Clients/hosts {0}, exec cmd: {1}".format(hosts, cmd))
    pcmd(hosts, cmd, False)


def create_acl_file(file_name, permissions):
    """Create a acl_file with permissions.

    Args:
        file_name (str): file name.
        permissions (str): daos acl permission list.

    """
    acl_file = open(file_name, "w")
    acl_file.write("\n".join(permissions))
    acl_file.close()


def check_uuid_format(uuid):
    """Checks a correct UUID format.

    Args:
        uuid (str): Pool or Conatiner UUID.

    Returns:
        True or False if uuid is well formed or not.
    """
    pattern = re.compile("([0-9a-f-]+)")
    return bool(len(uuid) == 36 and pattern.match(uuid))
