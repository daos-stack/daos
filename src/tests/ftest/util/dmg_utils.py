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
import re

from command_utils import DaosCommand, CommandWithParameters, CommandFailure
from command_utils import FormattedParameter


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
        """Get the action command object based on the yaml provided value."""
        # pylint: disable=redefined-variable-type
        if self.action.value == "format":
            self.action_command = self.DmgFormatSubCommand()
        elif self.action.value == "prepare":
            self.action_command = self.DmgPrepareSubCommand()
        elif self.action.value == "create":
            self.action_command = self.DmgCreateSubCommand()
        elif self.action.value == "destroy":
            self.action_command = self.DmgDestroySubCommand()
        else:
            self.action_command = None

    class DmgFormatSubCommand(CommandWithParameters):
        """Defines an object representing a format sub dmg command."""

        def __init__(self):
            """Create a dmg Command object."""
            super(DmgCommand.DmgFormatSubCommand, self).__init__(
                "/run/dmg/format/*", "format")
            self.reformat = FormattedParameter("--reformat", False)

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

    class DmgCreateSubCommand(CommandWithParameters):
        """Defines a object representing a create sub dmg command."""

        def __init__(self):
            """Create a dmg Command object."""
            super(DmgCommand.DmgCreateSubCommand, self).__init__(
                "/run/dmg/create/*", "create")
            self.group = FormattedParameter("-g {}")
            self.user = FormattedParameter("-u {}")
            self.acl_file = FormattedParameter("-a {}")
            self.scm_size = FormattedParameter("-s {}")
            self.nvme_size = FormattedParameter("-n {}")
            self.ranks = FormattedParameter("-r {}")
            self.nsvc = FormattedParameter("-v {}")
            self.sys = FormattedParameter("-S {}")

    class DmgDestroySubCommand(CommandWithParameters):
        """Defines an object representing a destroy sub dmg command."""

        def __init__(self):
            """Create a dmg Command object."""
            super(DmgCommand.DmgDestroySubCommand, self).__init__(
                "/run/dmg/destroy/*", "destroy")
            self.pool = FormattedParameter("--pool {}")
            self.force = FormattedParameter("-f", False)

def storage_scan(path, hosts, insecure=True):
    """ Execute scan command through dmg tool to servers provided.

    Args:
        path (str): path to tool's binary
        hosts (list): list of servers to run scan on.
        insecure (bool): toggle insecure mode

    Returns:
        Avocado CmdResult object that contains exit status, stdout information.

    """
    # Create and setup the command
    dmg = DmgCommand(path)
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


def storage_format(path, hosts, insecure=True):
    """ Execute format command through dmg tool to servers provided.

    Args:
        path (str): path to tool's binary
        hosts (list): list of servers to run format on.
        insecure (bool): toggle insecure mode

    Returns:
        Avocado CmdResult object that contains exit status, stdout information.

    """
    # Create and setup the command
    dmg = DmgCommand(path)
    dmg.sudo = True
    dmg.insecure.value = insecure
    dmg.hostlist.value = hosts
    dmg.request.value = "storage"
    dmg.action.value = "format"
    dmg.get_action_command()

    try:
        result = dmg.run()
    except CommandFailure as details:
        print("<dmg> command failed: {}".format(details))
        return None

    return result


def storage_prep(path, hosts, user=None, hugepages="4096", nvme=False,
                 scm=False, insecure=True):
    """Execute prepare command through dmg tool to servers provided.

    Args:
        path (str): path to tool's binary
        hosts (list): list of servers to run prepare on.
        user (str, optional): User with privileges. Defaults to False.
        hugepages (str, optional): Hugepages to allocate. Defaults to "4096".
        nvme (bool, optional): Perform prep on nvme. Defaults to False.
        scm (bool, optional): Perform prep on scm. Defaults to False.
        insecure (bool): toggle insecure mode

    Returns:
        Avocado CmdResult object that contains exit status, stdout information.

    """
    # Create and setup the command
    dmg = DmgCommand(path)
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


def storage_reset(path, hosts, nvme=False, scm=False, user=None,
                  hugepages="4096", insecure=True):
    """Execute prepare reset command through dmg tool to servers provided.

    Args:
        path (str): path to tool's binary.
        hosts (list): list of servers to run prepare on.
        nvme (bool): if true, nvme flag will be appended to command.
        scm (bool): if true, scm flag will be appended to command.
        user (str, optional): User with privileges. Defaults to False.
        hugepages (str, optional): Hugepages to allocate. Defaults to "4096".
        insecure (bool): toggle insecure mode

    Returns:
        Avocado CmdResult object that contains exit status, stdout information.

    """
    # Create and setup the command
    dmg = DmgCommand(path)
    dmg.insecure.value = insecure
    dmg.hostlist.value = hosts
    dmg.request.value = "storage"
    dmg.action.value = "prepare"
    dmg.get_action_command()
    dmg.action_command.nvmeonly.value = nvme
    dmg.action_command.scmonly.value = scm
    dmg.action_command.targetuser.value = user
    dmg.action_command.hugepages.value = hugepages
    dmg.action_command.reset.value = True
    dmg.action_command.force.value = True

    try:
        result = dmg.run()
    except CommandFailure as details:
        print("<dmg> command failed: {}".format(details))
        return None

    return result


def pool_create(path, scm_size, host_port=None, insecure=True, group=None,
                user=None, acl_file=None, nvme_size=None, ranks=None,
                nsvc=None, sys=None):
    # pylint: disable=too-many-arguments
    """Execute pool create command through dmg tool to servers provided.

    Args:
        path (str): Path to the directory of dmg binary.
        host_port (str, optional): Comma separated list of Host:Port where
            daos_server runs. e.g., wolf-31:10001,wolf-32:10001. Use 10001 for
            the default port number. This number is defined in
            daos_avocado_test.yaml
        scm_size (str): SCM size value passed into the command.
        insecure (bool, optional): Insecure mode. Defaults to True.
        group (str, otional): Group with privileges. Defaults to None.
        user (str, optional): User with privileges. Defaults to None.
        acl_file (str, optional): Access Control List file path for DAOS pool.
            Defaults to None.
        nvme_size (str, optional): NVMe size. Defaults to None.
        ranks (str, optional): Storage server unique identifiers (ranks) for
            DAOS pool
        nsvc (str, optional): Number of pool service replicas. Defaults to
            None, in which case 1 is used by the dmg binary in default.
        sys (str, optional): DAOS system that pool is to be a part of. Defaults
            to None, in which case daos_server is used by the dmg binary in
            default.

    Returns:
        CmdResult: Object that contains exit status, stdout, and other
            information.
    """
    # Create and setup the command
    dmg = DmgCommand(path)
    dmg.insecure.value = insecure
    dmg.hostlist.value = host_port
    dmg.request.value = "pool"
    dmg.action.value = "create"
    dmg.get_action_command()
    dmg.action_command.group.value = group
    dmg.action_command.user.value = user
    dmg.action_command.acl_file.value = acl_file
    dmg.action_command.scm_size.value = scm_size
    dmg.action_command.nvme_size.value = nvme_size
    dmg.action_command.ranks.value = ranks
    dmg.action_command.nsvc.value = nsvc
    dmg.action_command.sys.value = sys

    try:
        result = dmg.run()
    except CommandFailure as details:
        print("Pool create command failed: {}".format(details))
        return None

    return result


def pool_destroy(path, pool_uuid, host_port=None, insecure=True, force=True):
    """ Execute pool destroy command through dmg tool to servers provided.

    Args:
        path (str): Path to the directory of dmg binary.
        host_port (str, optional): Comma separated list of Host:Port where
            daos_server runs. e.g., wolf-31:10001,wolf-32:10001. Use 10001 for
            the default port number. This number is defined in
            daos_avocado_test.yaml
        pool_uuid (str): Pool UUID to destroy.
        insecure (bool, optional): Insecure mode. Defaults to True.
        foce (bool, optional): Force removal of DAOS pool. Defaults to True.

    Returns:
        CmdResult: Object that contains exit status, stdout, and other
            information.
    """
    # Create and setup the command
    dmg = DmgCommand(path)
    dmg.insecure.value = insecure
    dmg.hostlist.value = host_port
    dmg.request.value = "pool"
    dmg.action.value = "destroy"
    dmg.get_action_command()
    dmg.action_command.pool.value = pool_uuid
    dmg.action_command.force.value = force

    try:
        result = dmg.run()
    except CommandFailure as details:
        print("Pool destroy command failed: {}".format(details))
        return None

    return result


def get_pool_uuid_service_replicas_from_stdout(stdout_str):
    """Get Pool UUID and Service replicas from stdout.

    stdout_str is something like:
    Active connections: [wolf-3:10001]
    Creating DAOS pool with 100MB SCM and 0B NvMe storage (1.000 ratio)
    Pool-create command SUCCEEDED: UUID: 9cf5be2d-083d-4f6b-9f3e-38d771ee313f,
    Service replicas: 0

    This method makes it easy to create a test.

    Args:
        stdout_str (str): Output of pool create command.

    Returns:
        Tuple (str, str): Tuple that contains two items; Pool UUID and Service
            replicas if found. If not found, the tuple contains None.
    """
    # Find the following with regex. One or more of whitespace after "UUID:"
    # followed by one of more of number, alphabets, or -. Use parenthesis to
    # get the returned value.
    uuid = None
    svc = None
    match = re.search(r" UUID: (.+), Service replicas: (.+)", stdout_str)
    if match:
        uuid = match.group(1)
        svc = match.group(2)
    return uuid, svc
