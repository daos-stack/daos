#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

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

import os
import getpass

from command_utils import DaosCommand, CommandWithParameters
from command_utils import BasicParameter, FormattedParameter
from general_utils import get_file_path
from avocado.utils import process


class DmgCommand(DaosCommand):
    """Defines a object representing a dmg (or daos_shell) command."""

    def __init__(self, path):
        """Create a dmg Command object."""
        super(DmgCommand, self).__init__("/run/dmg/*", "daos_shell", path)
        self.format = self.DmgFormatSubCommand()
        self.prepare = self.DmgPrepareSubCommand()

        self.hostlist = FormattedParameter("-l {}")
        self.hostfile = FormattedParameter("-f {}")
        self.configpath = FormattedParameter("-o {}")
        self.insecure = FormattedParameter("-i", None)
        self.debug = FormattedParameter("-d", None)
        self.json = FormattedParameter("-j", None)
        self.subcommand_list = {
            "format": self.format,
            "prepare": self.prepare,
        }

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters

        """
        # Join all the parameters that have been assigned a value with the
        # command to create the command string
        params = []
        for name in self.get_param_names():
            value = str(getattr(self, name))
            if name == "action" and value in self.subcommand_list:
                params.append(str(self.subcommand_list[value]))
            elif value != "":
                params.append(value)
        return " ".join([os.path.join(self._path, self._command)] + params)

    class DmgFormatSubCommand(CommandWithParameters):
        """Defines a object representing a sub dmg (or daos_shell) command."""

        def __init__(self):
            """Create a dmg Command object."""
            super(DmgCommand.DmgFormatSubCommand, self).__init__("format")
            self.force = FormattedParameter("-f", None)

    class DmgPrepareSubCommand(CommandWithParameters):
        """Defines a object representing a sub dmg (or daos_shell) command."""

        def __init__(self):
            """Create a dmg Command object."""
            super(DmgCommand.DmgPrepareSubCommand, self).__init__("prepare")
            self.pci_wl = FormattedParameter("-w {}")
            self.hugepages = FormattedParameter("-p {}")
            self.targetuser = FormattedParameter("-u {}")
            self.nvmeonly = FormattedParameter("-n", None)
            self.scmonly = FormattedParameter("-s", None)
            self.force = FormattedParameter("-f", None)
            self.reset = FormattedParameter("--reset", None)


def storage_scan(hosts):
    """ Execute scan command through dmg tool to servers provided.

    Args:
        hosts (list): list of servers to run scan on.
        path (str, optional): Path to dmg command binary. Defaults to "".

    Returns:
        False if issue running command. True otherwise.

    """
    # Create and setup the command
    dmg = DmgCommand(get_file_path("bin/daos_shell"))
    dmg.request.value = "storage"
    dmg.action.value = "scan"
    dmg.insecure.value = True
    dmg.hostlist.value = hosts

    try:
        result = dmg.run()
    except process.CmdError as details:
        print("<dmg> command failed: {}".format(details))
        return False

    return result

def storage_format(hosts):
    """ Execute format command through dmg tool to servers provided.

    Args:
        hosts (list): list of servers to run format on.
        path (str, optional): Path to dmg command binary. Defaults to "".

    Returns:
        False if issue running command. True otherwise.

    """
    # Create and setup the command
    dmg = DmgCommand(get_file_path("bin/daos_shell"))
    dmg.insecure.value = True
    dmg.hostlist.value = hosts
    dmg.request.value = "storage"
    dmg.action.value = "format"
    dmg.format.force.value = True

    try:
        result = dmg.run(sudo=True)
    except process.CmdError as details:
        print("<dmg> command failed: {}".format(details))
        return False

    return result

def storage_prep(hosts, user=None, hugepages="4096", nvme=None,
                 scm=None):
    """ Execute prepare command through dmg tool to servers provided.

    Args:
        hosts (list): list of servers to run prepare on.
        path (str, optional): Path to dmg command binary. Defaults to "".

    Returns:
        False if issue running command. True otherwise.

    """
    # Create and setup the command
    dmg = DmgCommand(get_file_path("bin/daos_shell"))
    dmg.insecure.value = True
    dmg.hostlist.value = hosts
    dmg.request.value = "storage"
    dmg.action.value = "prepare"
    dmg.prepare.nvmeonly.value = nvme
    dmg.prepare.scmonly.value = scm
    dmg.prepare.targetuser.value = getpass.getuser() if user is None else user
    dmg.prepare.hugepages.value = hugepages
    dmg.prepare.force.value = True

    try:
        result = dmg.run()
    except process.CmdError as details:
        print("<dmg> command failed: {}".format(details))
        return False

    return result

def storage_reset(hosts, user=None, hugepages="4096"):
    """Execute prepare reset command through dmg tool to servers provided.

    Args:
        hosts ([type]): [description]
        path (str, optional): [description]. Defaults to "".
        user ([type], optional): [description]. Defaults to None.
        hugepages (str, optional): [description]. Defaults to "4096".

    Returns:
        bool: False if issue running command. True otherwise.

    """
    # Create and setup the command
    dmg = DmgCommand(get_file_path("bin/daos_shell"))
    dmg.insecure.value = True
    dmg.hostlist.value = hosts
    dmg.request.value = "storage"
    dmg.action.value = "prepare"
    dmg.prepare.nvmeonly.value = True
    dmg.prepare.targetuser.value = getpass.getuser() if user is None else user
    dmg.prepare.hugepages.value = hugepages
    dmg.prepare.reset.value = True
    dmg.prepare.force.value = True

    try:
        result = dmg.run()
    except process.CmdError as details:
        print("<dmg> command failed: {}".format(details))
        return False

    return result
