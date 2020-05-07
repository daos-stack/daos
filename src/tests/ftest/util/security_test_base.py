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
# To remove the following import once the PR1484 has landed.
from avocado.utils import process

def acl_entry(usergroup, name, perm, permissions):
    """Create a daos acl entry for the specified user or group and permission.

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


# To remove the following function once the PR1484 has landed.
def run_command(command, timeout=60, verbose=True, raise_exception=True,
                output_check="combined", env=None):
    """Run the command on the local host.
    This method uses the avocado.utils.process.run() method to run the specified
    command string on the local host using subprocess.Popen(). Even though the
    command is specified as a string, since shell=False is passed to process.run
    it will use shlex.spit() to break up the command into a list before it is
    passed to subprocess.Popen. The shell=False is forced for security.
    As a result typically any command containing ";", "|", "&&", etc. will fail.
    This can be avoided in command strings like "for x in a b; echo $x; done"
    by using "/usr/bin/bash -c 'for x in a b; echo $x; done'".

    Args:
        command (str): command to run.
        timeout (int, optional): command timeout. Defaults to 60 seconds.
        verbose (bool, optional): whether to log the command run and
            stdout/stderr. Defaults to True.
        raise_exception (bool, optional): whether to raise an exception if the
            command returns a non-zero exit status. Defaults to True.
        output_check (str, optional): whether to record the output from the
            command (from stdout and stderr) in the test output record files.
            Valid values:
                "stdout"    - standard output *only*
                "stderr"    - standard error *only*
                "both"      - both standard output and error in separate files
                "combined"  - standard output and error in a single file
                "none"      - disable all recording
            Defaults to "combined".
        env (dict, optional): dictionary of environment variable names and
            values to set when running the command. Defaults to None.

    Raises:
        DaosTestError: if there is an error running the command

    Returns:
        CmdResult: an avocado.utils.process CmdResult object containing the
            result of the command execution.  A CmdResult object has the
            following properties:
                command         - command string
                exit_status     - exit_status of the command
                stdout          - the stdout
                stderr          - the stderr
                duration        - command execution time
                interrupted     - whether the command completed within timeout
                pid             - command's pid

    """
    kwargs = {
        "cmd": command,
        "timeout": timeout,
        "verbose": verbose,
        "ignore_status": not raise_exception,
        "allow_output_check": output_check,
        "shell": False,
        "env": env,
    }
    try:
        # Block until the command is complete or times out
        return process.run(**kwargs)

    except process.CmdError as error:
        # Command failed or possibly timed out
        msg = "Error occurred running '{}': {}".format(" ".join(command), error)
        print(msg)
        raise DaosTestError(msg)
