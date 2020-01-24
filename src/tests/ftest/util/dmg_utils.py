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

from getpass import getuser
from grp import getgrgid
from pwd import getpwuid
import re

from command_utils import CommandWithSubCommand, CommandWithParameters
from command_utils import FormattedParameter, CommandFailure


class DmgCommand(CommandWithSubCommand):
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

    def set_hostlist(self, manager):
        """Set the dmg hostlist parameter with the daos server/agent info.

        Use the daos server/agent access points port and list of hosts to define
        the dmg --hostlist command line parameter.

        Args:
            manager (SubprocessManager): daos server/agent process manager
        """
        self.hostlist.update(
            manager.get_config_value("access_points"), "dmg.hostlist")

    def get_sub_command_class(self):
        # pylint: disable=redefined-variable-type
        """Get the dmg sub command object based upon the sub-command."""
        if self.sub_command.value == "network":
            self.sub_command_class = self.NetworkSubCommand()
        elif self.sub_command.value == "pool":
            self.sub_command_class = self.PoolSubCommand()
        elif self.sub_command.value == "storage":
            self.sub_command_class = self.StorageSubCommand()
        elif self.sub_command.value == "system":
            self.sub_command_class = self.SystemSubCommand()
        else:
            self.sub_command_class = None

    class NetworkSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg network sub command."""

        def __init__(self):
            """Create a dmg network subcommand object."""
            super(DmgCommand.NetworkSubCommand, self).__init__(
                "/run/dmg/network/*", "network")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg network sub command object."""
            if self.sub_command.value == "scan":
                self.sub_command_class = self.ScanSubCommand()
            else:
                self.sub_command_class = None

        class ScanSubCommand(CommandWithParameters):
            """Defines an object for the dmg network scan command."""

            def __init__(self):
                """Create a dmg network scan command object."""
                super(
                    DmgCommand.NetworkSubCommand.ScanSubCommand, self).__init__(
                        "/run/dmg/network/scan/*", "scan")
                self.provider = FormattedParameter("-p {}", None)
                self.all = FormattedParameter("-a", False)

    class PoolSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg pool sub command."""

        def __init__(self):
            """Create a dmg pool subcommand object."""
            super(DmgCommand.PoolSubCommand, self).__init__(
                "/run/dmg/pool/*", "pool")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg pool sub command object."""
            if self.sub_command.value == "create":
                self.sub_command_class = self.CreateSubCommand()
            elif self.sub_command.value == "delete-acl":
                self.sub_command_class = self.DeleteAclSubCommand()
            elif self.sub_command.value == "destroy":
                self.sub_command_class = self.DestroySubCommand()
            elif self.sub_command.value == "get-acl":
                self.sub_command_class = self.GetAclSubCommand()
            elif self.sub_command.value == "overwrite-acl":
                self.sub_command_class = self.OverwriteAclSubCommand()
            elif self.sub_command.value == "update-acl":
                self.sub_command_class = self.UpdateAclSubCommand()
            else:
                self.sub_command_class = None

        class CreateSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool create command."""

            def __init__(self):
                """Create a dmg pool create command object."""
                super(
                    DmgCommand.PoolSubCommand.CreateSubCommand,
                    self).__init__(
                        "/run/dmg/pool/create/*", "create")
                self.group = FormattedParameter("--group={}", None)
                self.user = FormattedParameter("--user={}", None)
                self.acl_file = FormattedParameter("--acl-file={}", None)
                self.scm_size = FormattedParameter("--scm-size={}", None)
                self.nvme_size = FormattedParameter("--nvme-size={}", None)
                self.ranks = FormattedParameter("--ranks={}", None)
                self.nsvc = FormattedParameter("--nsvc={}", None)
                self.sys = FormattedParameter("--sys={}", None)

        class DeleteAclSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool delete-acl command."""

            def __init__(self):
                """Create a dmg pool delete-acl command object."""
                super(
                    DmgCommand.PoolSubCommand.DeleteAclSubCommand,
                    self).__init__(
                        "/run/dmg/pool/delete-acl/*", "delete-acl")
                self.pool = FormattedParameter("--pool={}", None)
                self.principal = FormattedParameter("-p {}", None)

        class DestroySubCommand(CommandWithParameters):
            """Defines an object for the dmg pool destroy command."""

            def __init__(self):
                """Create a dmg pool destroy command object."""
                super(
                    DmgCommand.PoolSubCommand.DestroySubCommand,
                    self).__init__(
                        "/run/dmg/pool/destroy/*", "destroy")
                self.pool = FormattedParameter("--pool={}", None)
                self.sys_name = FormattedParameter("--sys-name={}", None)
                self.force = FormattedParameter("--force", False)

        class GetAclSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool get-acl command."""

            def __init__(self):
                """Create a dmg pool get-acl command object."""
                super(
                    DmgCommand.PoolSubCommand.GetAclSubCommand,
                    self).__init__(
                        "/run/dmg/pool/get-acl/*", "get-acl")
                self.pool = FormattedParameter("--pool={}", None)

        class OverwriteAclSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool overwrite-acl command."""

            def __init__(self):
                """Create a dmg pool overwrite-acl command object."""
                super(
                    DmgCommand.PoolSubCommand.OverwriteAclSubCommand,
                    self).__init__(
                        "/run/dmg/pool/overwrite-acl/*", "overwrite-acl")
                self.pool = FormattedParameter("--pool={}", None)
                self.acl_file = FormattedParameter("-a {}", None)

        class UpdateAclSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool update-acl command."""

            def __init__(self):
                """Create a dmg pool update-acl command object."""
                super(
                    DmgCommand.PoolSubCommand.UpdateAclSubCommand,
                    self).__init__(
                        "/run/dmg/pool/update-acl/*", "update-acl")
                self.pool = FormattedParameter("--pool={}", None)
                self.acl_file = FormattedParameter("-a {}", None)
                self.entry = FormattedParameter("-e {}", None)

    class StorageSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg storage sub command."""

        def __init__(self):
            """Create a dmg storage subcommand object."""
            super(DmgCommand.StorageSubCommand, self).__init__(
                "/run/dmg/storage/*", "storage")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg storage sub command object."""
            if self.sub_command.value == "format":
                self.sub_command_class = self.FormatSubCommand()
            elif self.sub_command.value == "prepare":
                self.sub_command_class = self.PrepareSubCommand()
            elif self.sub_command.value == "query":
                self.sub_command_class = self.QuerySubCommand()
            elif self.sub_command.value == "scan":
                self.sub_command_class = self.ScanSubCommand()
            else:
                self.sub_command_class = None

        class FormatSubCommand(CommandWithParameters):
            """Defines an object for the dmg storage format command."""

            def __init__(self):
                """Create a dmg storage format command object."""
                super(
                    DmgCommand.StorageSubCommand.FormatSubCommand,
                    self).__init__(
                        "/run/dmg/storage/format/*", "format")
                self.reformat = FormattedParameter("--reformat", False)

        class PrepareSubCommand(CommandWithParameters):
            """Defines an object for the dmg storage format command."""

            def __init__(self):
                """Create a dmg storage prepare command object."""
                super(
                    DmgCommand.StorageSubCommand.PrepareSubCommand,
                    self).__init__(
                        "/run/dmg/storage/prepare/*", "prepare")
                self.pci_whitelist = FormattedParameter("-w {}", None)
                self.hugepages = FormattedParameter("-p {}", None)
                self.target_user = FormattedParameter("-u {}", None)
                self.nvme_only = FormattedParameter("-n", False)
                self.scm_only = FormattedParameter("-s", False)
                self.reset = FormattedParameter("--reset", False)
                self.force = FormattedParameter("-f", False)

        class QuerySubCommand(CommandWithSubCommand):
            """Defines an object for the dmg query format command."""

            def __init__(self):
                """Create a dmg storage query command object."""
                super(
                    DmgCommand.StorageSubCommand.QuerySubCommand,
                    self).__init__(
                        "/run/dmg/storage/query/*", "query")

            def get_sub_command_class(self):
                # pylint: disable=redefined-variable-type
                """Get the dmg pool sub command object."""
                if self.sub_command.value == "blobstore-health":
                    self.sub_command_class = self.BlobstoreHealthSubCommand()
                elif self.sub_command.value == "smd":
                    self.sub_command_class = self.SmdSubCommand()
                else:
                    self.sub_command_class = None

            class BlobstoreHealthSubCommand(CommandWithParameters):
                """Defines a dmg storage query blobstore-health object."""

                def __init__(self):
                    """Create a dmg storage query blobstore-health object."""
                    super(
                        DmgCommand.StorageSubCommand.QuerySubCommand.
                        BlobstoreHealthSubCommand,
                        self).__init__(
                            "/run/dmg/storage/query/blobstore-health/*",
                            "blobstore-health")
                    self.devuuid = FormattedParameter("-u {}", None)
                    self.tgtid = FormattedParameter("-t {}", None)

            class SmdSubCommand(CommandWithParameters):
                """Defines a dmg storage query smd object."""

                def __init__(self):
                    """Create a dmg storage query smd object."""
                    super(
                        DmgCommand.StorageSubCommand.QuerySubCommand.
                        SmdSubCommand,
                        self).__init__(
                            "/run/dmg/storage/query/nvme-health/*",
                            "nvme-health")
                    self.devices = FormattedParameter("-d", False)
                    self.pools = FormattedParameter("-p", False)

        class ScanSubCommand(CommandWithParameters):
            """Defines an object for the dmg storage scan command."""

            def __init__(self):
                """Create a dmg storage scan command object."""
                super(
                    DmgCommand.StorageSubCommand.ScanSubCommand,
                    self).__init__(
                        "/run/dmg/storage/scan/*", "scan")
                self.summary = FormattedParameter("-m", False)

    class SystemSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg system sub command."""

        def __init__(self):
            """Create a dmg system subcommand object."""
            super(DmgCommand.SystemSubCommand, self).__init__(
                "/run/dmg/system/*", "system")

    def _get_result(self):
        """Get the result from running the configured dmg command.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg command fails.

        """
        result = None
        try:
            result = self.run()
        except CommandFailure as details:
            raise CommandFailure("<dmg> command failed: %s", str(details))

        return result

    def storage_scan(self):
        """Get the result of the dmg storage scan command.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage scan command fails.

        """
        self.set_sub_command("storage")
        self.sub_command_class.set_sub_command("scan")
        return self._get_result()

    def storage_format(self):
        """Get the result of the dmg storage format command.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage format command fails.

        """
        self.set_sub_command("storage")
        self.sub_command_class.set_sub_command("format")
        return self._get_result()

    def storage_prepare(self, user=None, hugepages="4096", nvme=False,
                        scm=False, reset=False, force=True):
        """Get the result of the dmg storage format command.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage prepare command fails.

        """
        self.set_sub_command("storage")
        self.sub_command_class.set_sub_command("prepare")
        self.sub_command_class.sub_command_class.nvme_only.value = nvme
        self.sub_command_class.sub_command_class.scm_only.value = scm
        self.sub_command_class.sub_command_class.target_user.value = \
            getuser() if user is None else user
        self.sub_command_class.sub_command_class.hugepages.value = hugepages
        self.sub_command_class.sub_command_class.reset.value = reset
        self.sub_command_class.sub_command_class.force.value = force
        return self._get_result()

    def pool_create(self, scm_size, uid=None, gid=None, nvme_size=None,
                    target_list=None, svcn=None, group=None):
        """Create a pool with the dmg command.

        Args:
            scm_size (int): SCM pool size to create.
            uid (int, optional): User ID with privileges. Defaults to None.
            gid (int, otional): Group ID with privileges. Defaults to None.
            nvme_size (str, optional): NVMe size. Defaults to None.
            target_list (list, optional): a list of storage server unique
                identifiers (ranks) for the DAOS pool
            svcn (str, optional): Number of pool service replicas. Defaults to
                None, in which case 1 is used by the dmg binary in default.
            group (str, optional): DAOS system group name in which to create the
                pool. Defaults to None, in which case "daos_server" is used by
                default.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg pool create command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("create")
        self.sub_command_class.sub_command_class.user.value = \
            getpwuid(uid).pw_name if uid is not None else uid
        self.sub_command_class.sub_command_class.group.value = \
            getgrgid(gid).gr_name if gid is not None else gid
        self.sub_command_class.sub_command_class.scm_size.value = scm_size
        self.sub_command_class.sub_command_class.nvme_size.value = nvme_size
        if target_list is not None:
            self.sub_command_class.sub_command_class.ranks.value = ":".join(
                [str(target) for target in target_list])
        self.sub_command_class.sub_command_class.nsvc.value = svcn
        self.sub_command_class.sub_command_class.sys.value = group
        return self._get_result()

    def pool_destroy(self, pool, force=True):
        """Destroy a  pool with the dmg command.

        Args:
            pool (str): Pool UUID to destroy.
            foce (bool, optional): Force removal of DAOS pool. Defaults to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool destroy command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("destroy")
        self.sub_command_class.sub_command_class.pool.value = pool
        self.sub_command_class.sub_command_class.force.value = force
        return self._get_result()


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
