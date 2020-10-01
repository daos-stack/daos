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
  provided in Contract No. 8F-30005.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""

import os
import random
from general_utils import pcmd

class DaosTestError(Exception):
    """DAOS API exception class."""


def acl_entry(usergroup, name, perm, permissions=None):
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


def add_del_user(hosts, bash_cmd, user):
    """Add or delete the daos user and group on host by sudo command.

    Args:
        hosts (list): list of host.
        bash_cmd (str): linux bash command to create user or group.
        user (str): user or group name to be created or cleaned.

    """
    bash_cmd = os.path.join("/usr/sbin", bash_cmd)
    homedir = ""
    if "usermod" not in bash_cmd and "user" in bash_cmd:
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
    acl_file = open(file_name, "w+")
    acl_file.write("\n".join(permissions))
    acl_file.close()


def read_acl_file(filename):
    """Read contents of given acl file.

    Args:
        filename: name of file to be read for acl information

    Returns:
        list: list containing ACL entries

    """
    f = open(filename, 'r')
    content = f.readlines()
    f.close()

    # Parse
    acl = []
    for entry in content:
        if not entry.startswith("#"):
            acl.append(entry.strip())

    return acl

def generate_acl_file(acl_type, acl_args):
    """Creates an acl file for the specified type.

    Args:
        acl_type (str): default, invalid, valid
        acl_args (dic): Dictionary that contains the required parameters
                        to generate the acl entries, such as user, group
                        and permissions

    Returns:
        List of permissions
    """
    # First we determine the type o acl to be created
    msg = None
    acl_entries = {
        "default": ["A::OWNER@:rwdtTaAo", "A:G:GROUP@:rwtT"],
        "valid": ["A::OWNER@:rwdtTaAo",
                  acl_entry("user", acl_args["user"], "random",
                            acl_args["permissions"]),
                  "A:G:GROUP@:rwtT",
                  acl_entry("group", acl_args["group"], "random",
                            acl_args["permissions"]),
                  "A::EVERYONE@:"],
        "invalid": ["A::OWNER@:invalid", "A:G:GROUP@:rwtT"]
    }

    if acl_type in acl_entries:
        get_acl_file = "acl_" + acl_type + ".txt"
        file_name = os.path.join(acl_args["tmp_dir"], get_acl_file)
        create_acl_file(file_name, acl_entries[acl_type])
    else:
        msg = "Invalid acl_type '{}' while generating permissions".format(
            acl_type)

    if msg is not None:
        print(msg)
        raise DaosTestError(msg)

    return acl_entries[acl_type]
