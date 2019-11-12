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

import getpass

from command_utils import DaosCommand, CommandWithParameters, CommandFailure
from command_utils import FormattedParameter
from general_utils import get_file_path


class DmgCommand(DaosCommand):
    """Defines a object representing a dmg command."""

    def __init__(self, path):
        """Create a dmg Command object."""
        super(DmgCommand, self).__init__("/run/dmg/*", "dmg", path)

        self.hostlist = FormattedParameter("-l {}")
        self.hostfile = FormattedParameter("-f {}")
        self.configpath = FormattedParameter("-o {}")
        self.insecure = FormattedParameter("-i", True)
        self.debug = FormattedParameter("-d", False)
        self.json = FormattedParameter("-j", False)

    def get_action_command(self):
        """Assign a command object for the specified request and action."""
        # pylint: disable=redefined-variable-type
        if self.action.value == "format":
            self.action_command = self.DmgFormatSubCommand()
            self.action_command.get_params()
        elif self.action.value == "prepare":
            self.action_command = self.DmgPrepareSubCommand()
            self.action_command.get_params()
        else:
            self.action_command = None

    class DmgFormatSubCommand(CommandWithParameters):
        """Defines an object representing a format sub dmg command."""

        def __init__(self):
            """Create a dmg Command object."""
            super(DmgCommand.DmgFormatSubCommand, self).__init__(
                "/run/dmg/format/*", "format")
            self.force = FormattedParameter("-f", False)

    class DmgPrepareSubCommand(CommandWithParameters):
        """Defines a object representing a prepare sub dmg command."""

        def __init__(self):
            """Create a dmg Command object."""
            super(DmgCommand.DmgPrepareSubCommand, self).__init__(
                "/run/dmg/prepare/*", "prepare")
            self.pci_wl = FormattedParameter("-w {}")
            self.hugepages = FormattedParameter("-p {}")
            self.targetuser = FormattedParameter("-u {}")
            self.nvmeonly = FormattedParameter("-n", False)
            self.scmonly = FormattedParameter("-s", False)
            self.force = FormattedParameter("-f", False)
            self.reset = FormattedParameter("--reset", False)

class DmgOldCommand(DaosCommand):
    """Defines a object representing the dmg_old command."""

    def __init__(self, path):
        """Create a dmg_old command object."""
        super(DmgOldCommand, self).__init__("/run/dmg_old/*", "dmg_old", path)

    def get_action_command(self):
        """Assign a command object for the specified request and action."""
        if self.action_old.value == "query":
            self.action_command = DmgOldQueryCommand()
        else:
            self.action_command = None

    class DmgOldQueryCommand(CommandWithParameters):
        #ExecutableCommand
        """Defines a object representing a dmg_old query sub command."""

        def __init__(self):
            """Create a dmg_old query sub command object."""
            super(DmgCommand, self).__init__("/run/dmg_old/*", "query", None)
#            self.pool = BasicParameter(None)
#            self.cont = BasicParameter(None)
#            self.oid = BasicParameter(None)





def storage_scan(hosts, insecure=True):
    """ Execute scan command through dmg tool to servers provided.

    Args:
        hosts (list): list of servers to run scan on.

    Returns:
        Avocado CmdResult object that contains exit status, stdout information.

    """
    # Create and setup the command
    dmg = DmgCommand(get_file_path("bin/dmg"))
    dmg.request.value = "storage"
    dmg.action.value = "scan"
    dmg.insecure.value = insecure
    dmg.hostlist.value = hosts

    try:
        result = dmg.run()
    except CommandFailure as details:
        print("<dmg> command failed: {}".format(details))
        return None

    return result


def storage_format(hosts, insecure=True):
    """ Execute format command through dmg tool to servers provided.

    Args:
        hosts (list): list of servers to run format on.

    Returns:
        Avocado CmdResult object that contains exit status, stdout information.

    """
    # Create and setup the command
    dmg = DmgCommand(get_file_path("bin/dmg"))
    dmg.insecure.value = insecure
    dmg.hostlist.value = hosts
    dmg.request.value = "storage"
    dmg.action.value = "format"
    dmg.get_action_command()
    dmg.action_command.force.value = True

    try:
        result = dmg.run(sudo=True)
    except CommandFailure as details:
        print("<dmg> command failed: {}".format(details))
        return None

    return result


def storage_prep(hosts, user=False, hugepages="4096", nvme=False,
                 scm=False, insecure=True):
    """Execute prepare command through dmg tool to servers provided.

    Args:
        hosts (list): list of servers to run prepare on.
        user (str, optional): User with priviledges. Defaults to False.
        hugepages (str, optional): Hugepages to allocate. Defaults to "4096".
        nvme (bool, optional): Perform prep on nvme. Defaults to False.
        scm (bool, optional): Perform prep on scm. Defaults to False.

    Returns:
        Avocado CmdResult object that contains exit status, stdout information.

    """
    # Create and setup the command
    dmg = DmgCommand(get_file_path("bin/dmg"))
    dmg.insecure.value = insecure
    dmg.hostlist.value = hosts
    dmg.request.value = "storage"
    dmg.action.value = "prepare"
    dmg.get_action_command()
    dmg.action_command.nvmeonly.value = nvme
    dmg.action_command.scmonly.value = scm
    dmg.action_command.targetuser.value = getpass.getuser() \
        if user is None else user
    dmg.action_command.hugepages.value = hugepages
    dmg.action_command.force.value = True

    try:
        result = dmg.run()
    except CommandFailure as details:
        print("<dmg> command failed: {}".format(details))
        return None

    return result


def storage_reset(hosts, user=None, hugepages="4096", insecure=True):
    """Execute prepare reset command through dmg tool to servers provided.

    Args:
        hosts (list): list of servers to run prepare on.
        user (str, optional): User with priviledges. Defaults to False.
        hugepages (str, optional): Hugepages to allocate. Defaults to "4096".

    Returns:
        Avocado CmdResult object that contains exit status, stdout information.

    """
    # Create and setup the command
    dmg = DmgCommand(get_file_path("bin/dmg"))
    dmg.insecure.value = insecure
    dmg.hostlist.value = hosts
    dmg.request.value = "storage"
    dmg.action.value = "prepare"
    dmg.get_action_command()
    dmg.action_command.nvmeonly.value = True
    dmg.action_command.targetuser.value = getpass.getuser() \
        if user is None else user
    dmg.action_command.hugepages.value = hugepages
    dmg.action_command.reset.value = True
    dmg.action_command.force.value = True

    try:
        result = dmg.run()
    except CommandFailure as details:
        print("<dmg> command failed: {}".format(details))
        return None

    return result
