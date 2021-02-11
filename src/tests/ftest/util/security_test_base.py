#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
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

def get_user_type(test_user):
    """Get test user's user type for ACE access control entry.

    Args:
        test_user (str): test user name.

    Return:
        str: test user type user/group for ACE.

    """
    user_type = "user"
    if "group" in test_user.lower():
        user_type = "group"
    return user_type

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
