#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

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

from command_utils_base import \
    CommandFailure, FormattedParameter, CommandWithParameters, YamlParameters
from command_utils import CommandWithSubCommand, YamlCommand


class DmgCommand(YamlCommand):
    """Defines a object representing a dmg command."""

    METHOD_REGEX = {
        "run": r"(.*)",
        "network_scan": r"[-]+(?:\n|\n\r)([a-z0-9-]+)(?:\n|\n\r)[-]+|NUMA\s+"
                        r"Socket\s+(\d+)|(ofi\+[a-z0-9;_]+)\s+([a-z0-9, ]+)",
        # Sample output of dmg pool list.
        # wolf-3:10001: connected
        # Pool UUID                            Svc Replicas
        # ---------                            ------------
        # b4a27b5b-688a-4d1e-8c38-363e32eb4f29 1,2,3
        # Between the first and the second group, use " +"; i.e., one or more
        # whitespaces. If we use "\s+", it'll pick up the second divider as
        # UUID since it's made up of hyphens and \s includes new line.
        "pool_list": r"(?:([0-9a-fA-F-]+) +([0-9,]+))",
        "pool_create": r"(?:UUID:|Service replicas:)\s+([A-Za-z0-9-]+)",
        "pool_query": r"(?:Pool\s+([0-9a-fA-F-]+),\s+ntarget=(\d+),"
                      r"\s+disabled=(\d+),\s+leader=(\d+),"
                      r"\s+version=(\d+)|Target\(VOS\)\s+count:\s*(\d+)|"
                      r"(?:(?:SCM:|NVMe:)\s+Total\s+size:\s+([0-9.]+\s+[A-Z]+)"
                      r"\s+Free:\s+([0-9.]+\s+[A-Z]+),\smin:([0-9.]+\s+[A-Z]+)"
                      r",\s+max:([0-9.]+\s+[A-Z]+),\s+mean:([0-9.]+\s+[A-Z]+))"
                      r"|Rebuild\s+\w+,\s+([0-9]+)\s+objs,\s+([0-9]+)"
                      r"\s+recs)",
        "system_query": r"(\d+|\[[0-9-,]+\])\s+([A-Za-z]+)",
        "system_start": r"(\d+|\[[0-9-,]+\])\s+([A-Za-z]+)\s+([A-Za-z]+)",
        "system_stop": r"(\d+|\[[0-9-,]+\])\s+([A-Za-z]+)\s+([A-Za-z]+)",
    }

    def __init__(self, path, yaml_cfg=None):
        """Create a dmg Command object.

        Args:
            path (str): path to the dmg command
            yaml_cfg (DmgYamlParameters, optional): dmg config file
                settings. Defaults to None, in which case settings
                must be supplied as command-line parameters.
        """
        super(DmgCommand, self).__init__("/run/dmg/*", "dmg", path, yaml_cfg)

        # If specified use the configuration file from the YamlParameters object
        default_yaml_file = None
        if isinstance(self.yaml, YamlParameters):
            default_yaml_file = self.yaml.filename

        self._hostlist = FormattedParameter("-l {}")
        self.hostfile = FormattedParameter("-f {}")
        self.configpath = FormattedParameter("-o {}", default_yaml_file)
        self.insecure = FormattedParameter("-i", False)
        self.debug = FormattedParameter("-d", False)
        self.json = FormattedParameter("-j", False)

    @property
    def hostlist(self):
        """Get the hostlist that was set.

        Returns a string list.
        """
        if self.yaml:
            hosts = self.yaml.hostlist.value
        else:
            hosts = self._hostlist.value.split(",")
        return hosts

    @hostlist.setter
    def hostlist(self, hostlist):
        """Set the hostlist to be used for dmg invocation.

        Args:
            hostlist (string list): list of host addresses
        """
        if self.yaml:
            if not isinstance(hostlist, list):
                hostlist = hostlist.split(",")
            self.yaml.hostlist.update(hostlist, "dmg.yaml.hostlist")
        else:
            if isinstance(hostlist, list):
                hostlist = ",".join(hostlist)
            self._hostlist.update(hostlist, "dmg._hostlist")

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
            elif self.sub_command.value == "list":
                self.sub_command_class = self.ListSubCommand()
            elif self.sub_command.value == "overwrite-acl":
                self.sub_command_class = self.OverwriteAclSubCommand()
            elif self.sub_command.value == "query":
                self.sub_command_class = self.QuerySubCommand()
            elif self.sub_command.value == "set-prop":
                self.sub_command_class = self.SetPropSubCommand()
            elif self.sub_command.value == "update-acl":
                self.sub_command_class = self.UpdateAclSubCommand()
            elif self.sub_command.value == "exclude":
                self.sub_command_class = self.ExcludeSubCommand()
            elif self.sub_command.value == "reintegrate":
                self.sub_command_class = self.ReintegrateSubCommand()
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

        class ExcludeSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool exclude command."""

            def __init__(self):
                """Create a dmg pool exclude command object."""
                super(
                    DmgCommand.PoolSubCommand.ExcludeSubCommand,
                    self).__init__(
                        "/run/dmg/pool/exclude/*", "exclude")
                self.pool = FormattedParameter("--pool={}", None)
                self.rank = FormattedParameter("--rank={}", None)
                self.tgt_idx = FormattedParameter("--target-idx={}", None)

        class ReintegrateSubCommand(CommandWithParameters):
            """Defines an object for dmg pool reintegrate command."""

            def __init__(self):
                """Create a dmg pool reintegrate command object."""
                super(
                    DmgCommand.PoolSubCommand.ReintegrateSubCommand,
                    self).__init__(
                        "/run/dmg/pool/reintegrate/*", "reintegrate")
                self.pool = FormattedParameter("--pool={}", None)
                self.rank = FormattedParameter("--rank={}", None)
                self.tgt_idx = FormattedParameter("--target-idx={}", None)

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

        class ListSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool list command."""

            def __init__(self):
                """Create a dmg pool list command object."""
                super(
                    DmgCommand.PoolSubCommand.ListSubCommand,
                    self).__init__(
                        "/run/dmg/pool/list/*", "list")

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

        class QuerySubCommand(CommandWithParameters):
            """Defines an object for the dmg pool query command."""

            def __init__(self):
                """Create a dmg pool query command object."""
                super(
                    DmgCommand.PoolSubCommand.QuerySubCommand,
                    self).__init__(
                        "/run/dmg/pool/query/*", "query")
                self.pool = FormattedParameter("--pool={}", None)

        class SetPropSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool set-prop command."""

            def __init__(self):
                """Create a dmg pool set-prop command object."""
                super(
                    DmgCommand.PoolSubCommand.SetPropSubCommand,
                    self).__init__(
                        "/run/dmg/pool/set-prop/*", "set-prop")
                self.pool = FormattedParameter("--pool={}", None)
                self.name = FormattedParameter("--name={}", None)
                self.value = FormattedParameter("--value={}", None)

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
            elif self.sub_command.value == "set":
                self.sub_command_class = self.SetSubCommand()
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
                            "/run/dmg/storage/query/smd/*",
                            "smd")
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
                self.verbose = FormattedParameter("--verbose", False)

        class SetSubCommand(CommandWithParameters):
            """Defines an object for the dmg storage set command."""

            def __init__(self):
                """Create a dmg storage set command object."""
                super(
                    DmgCommand.StorageSubCommand.SetSubCommand,
                    self).__init__(
                        "/run/dmg/storage/set/*", "set")
                self.nvme_faulty = FormattedParameter("nvme-faulty", False)

    class SystemSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg system sub command."""

        def __init__(self):
            """Create a dmg system subcommand object."""
            super(DmgCommand.SystemSubCommand, self).__init__(
                "/run/dmg/system/*", "system")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg system sub command object."""
            if self.sub_command.value == "leader-query":
                self.sub_command_class = self.LeaderQuerySubCommand()
            elif self.sub_command.value == "list-pools":
                self.sub_command_class = self.ListPoolsSubCommand()
            elif self.sub_command.value == "query":
                self.sub_command_class = self.QuerySubCommand()
            elif self.sub_command.value == "start":
                self.sub_command_class = self.StartSubCommand()
            elif self.sub_command.value == "stop":
                self.sub_command_class = self.StopSubCommand()
            else:
                self.sub_command_class = None

        class LeaderQuerySubCommand(CommandWithParameters):
            """Defines an object for the dmg system leader-query command."""

            def __init__(self):
                """Create a dmg system leader-query command object."""
                super(
                    DmgCommand.SystemSubCommand.LeaderQuerySubCommand,
                    self).__init__(
                        "/run/dmg/system/leader-query/*", "leader-query")

        class ListPoolsSubCommand(CommandWithParameters):
            """Defines an object for the dmg system list-pools command."""

            def __init__(self):
                """Create a dmg system list-pools command object."""
                super(
                    DmgCommand.SystemSubCommand.ListPoolsSubCommand,
                    self).__init__(
                        "/run/dmg/system/list-pools/*", "list-pools")

        class QuerySubCommand(CommandWithParameters):
            """Defines an object for the dmg system query command."""

            def __init__(self):
                """Create a dmg system query command object."""
                super(
                    DmgCommand.SystemSubCommand.QuerySubCommand,
                    self).__init__(
                        "/run/dmg/system/query/*", "query")
                self.rank = FormattedParameter("--rank={}")
                self.verbose = FormattedParameter("--verbose", False)

        class StartSubCommand(CommandWithParameters):
            """Defines an object for the dmg system start command."""

            def __init__(self):
                """Create a dmg system start command object."""
                super(
                    DmgCommand.SystemSubCommand.StartSubCommand,
                    self).__init__(
                        "/run/dmg/system/start/*", "start")

        class StopSubCommand(CommandWithParameters):
            """Defines an object for the dmg system stop command."""

            def __init__(self):
                """Create a dmg system stop command object."""
                super(
                    DmgCommand.SystemSubCommand.StopSubCommand,
                    self).__init__(
                        "/run/dmg/system/stop/*", "stop")
                self.force = FormattedParameter("--force", False)

    def network_scan(self, provider=None, all_devs=False):
        """Get the result of the dmg network scan command.

        Args:
            provider (str): name of network provider tied to the device
            all_devs (bool, optional): Show all devs  info. Defaults to False.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage scan command fails.

        """
        self.set_sub_command("network")
        self.sub_command_class.set_sub_command("scan")
        self.sub_command_class.sub_command_class.provider.value = provider
        self.sub_command_class.sub_command_class.all.value = all_devs
        return self._get_result()

    def storage_scan(self, verbose=False):
        """Get the result of the dmg storage scan command.

        Args:
            verbose (bool, optional): create verbose output. Defaults to False.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage scan command fails.

        """
        self.set_sub_command("storage")
        self.sub_command_class.set_sub_command("scan")
        self.sub_command_class.sub_command_class.verbose.value = verbose
        return self._get_result()

    def storage_format(self, reformat=False):
        """Get the result of the dmg storage format command.

        Args:
            reformat (bool): always reformat storage, could be destructive.
                This will create control-plane related metadata i.e. superblock
                file and reformat if the storage media is available and
                formattable.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage format command fails.

        """
        self.set_sub_command("storage")
        self.sub_command_class.set_sub_command("format")
        self.sub_command_class.sub_command_class.reformat.value = reformat
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
                    target_list=None, svcn=None, group=None, acl_file=None):
        """Create a pool with the dmg command.

        The uid and gid method arguments can be specified as either an integer
        or a string.  If an integer value is specified it will be converted into
        the corresponding user/group name string.

        Args:
            scm_size (int): SCM pool size to create.
            uid (object, optional): User ID with privileges. Defaults to None.
            gid (object, optional): Group ID with privileges. Defaults to None.
            nvme_size (str, optional): NVMe size. Defaults to None.
            target_list (list, optional): a list of storage server unique
                identifiers (ranks) for the DAOS pool
            svcn (str, optional): Number of pool service replicas. Defaults to
                None, in which case 1 is used by the dmg binary in default.
            group (str, optional): DAOS system group name in which to create the
                pool. Defaults to None, in which case "daos_server" is used by
                default.
            acl_file (str, optional): ACL file. Defaults to None.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg pool create command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("create")
        self.sub_command_class.sub_command_class.user.value = \
            getpwuid(uid).pw_name if isinstance(uid, int) else uid
        self.sub_command_class.sub_command_class.group.value = \
            getgrgid(gid).gr_name if isinstance(gid, int) else gid
        self.sub_command_class.sub_command_class.scm_size.value = scm_size
        self.sub_command_class.sub_command_class.nvme_size.value = nvme_size
        if target_list is not None:
            self.sub_command_class.sub_command_class.ranks.value = ",".join(
                [str(target) for target in target_list])
        self.sub_command_class.sub_command_class.nsvc.value = svcn
        self.sub_command_class.sub_command_class.sys.value = group
        self.sub_command_class.sub_command_class.acl_file.value = acl_file
        return self._get_result()

    def pool_query(self, pool):
        """Query a pool with the dmg command.

        Args:
            uuid (str): Pool UUID to query.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool query command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("query")
        self.sub_command_class.sub_command_class.pool.value = pool
        return self._get_result()

    def pool_destroy(self, pool, force=True):
        """Destroy a pool with the dmg command.

        Args:
            pool (str): Pool UUID to destroy.
            force (bool, optional): Force removal of pool. Defaults to True.

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

    def pool_get_acl(self, pool):
        """Get the ACL for a given pool.

        Args:
            pool (str): Pool for which to get the ACL.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool get-acl command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("get-acl")
        self.sub_command_class.sub_command_class.pool.value = pool
        return self._get_result()

    def pool_update_acl(self, pool, acl_file, entry):
        """Update the acl for a given pool.

        Args:
            pool (str): Pool for which to update the ACL.
            acl_file (str): ACL file to update
            entry (str): entry to be updated

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool update-acl command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("update-acl")
        self.sub_command_class.sub_command_class.pool.value = pool
        self.sub_command_class.sub_command_class.acl_file.value = acl_file
        self.sub_command_class.sub_command_class.entry.value = entry
        return self._get_result()

    def pool_overwrite_acl(self, pool, acl_file):
        """Overwrite the acl for a given pool.

        Args:
            pool (str): Pool for which to overwrite the ACL.
            acl_file (str): ACL file to update

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool overwrite-acl command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("overwrite-acl")
        self.sub_command_class.sub_command_class.pool.value = pool
        self.sub_command_class.sub_command_class.acl_file.value = acl_file
        return self._get_result()

    def pool_delete_acl(self, pool, principal):
        """Delete the acl for a given pool.

        Args:
            pool (str): Pool for which to delete the ACL.
            principal (str): principal to be deleted

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool delete-acl command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("delete-acl")
        self.sub_command_class.sub_command_class.pool.value = pool
        self.sub_command_class.sub_command_class.principal.value = principal
        return self._get_result()

    def pool_list(self):
        """List pools.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool delete-acl command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("list")
        return self._get_result()

    def pool_set_prop(self, pool, name, value):
        """Set property for a given Pool.

        Args:
            pool (str): Pool uuid for which property is supposed
                        to be set.
            name (str): Property name to be set
            value (str): Property value to be set

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                       information.

        Raises:
            CommandFailure: if the dmg pool set-prop command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("set-prop")
        self.sub_command_class.sub_command_class.pool.value = pool
        self.sub_command_class.sub_command_class.name.value = name
        self.sub_command_class.sub_command_class.value.value = value
        return self._get_result()

    def pool_exclude(self, pool_uuid, rank, tgt_idx=None):
        """Exclude a daos_server from the pool.

        Args:
            pool (str): Pool uuid.
            rank (int): Rank of the daos_server to exclude
            tgt_idx (int): target to be excluded from the pool

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                       information.

        Raises:
            CommandFailure: if the dmg pool exclude command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("exclude")
        self.sub_command_class.sub_command_class.pool.value = pool_uuid
        self.sub_command_class.sub_command_class.rank.value = rank
        self.sub_command_class.sub_command_class.tgt_idx.value = tgt_idx
        return self._get_result()

    def pool_reintegrate(self, pool_uuid, rank, tgt_idx=None):
        """Reintegrate a daos_server to the pool.

        Args:
            pool (str): Pool uuid.
            rank (int): Rank of the daos_server to reintegrate
            tgt_idx (int): target to be reintegrated to the pool

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                       information.

        Raises:
            CommandFailure: if the dmg pool reintegrate command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("reintegrate")
        self.sub_command_class.sub_command_class.pool.value = pool_uuid
        self.sub_command_class.sub_command_class.rank.value = rank
        self.sub_command_class.sub_command_class.tgt_idx.value = tgt_idx
        return self._get_result()

    def system_query(self, rank=None, verbose=False):
        """Query the state of the system.

        Args:
            rank (str, optional): rank to query. Defaults to None (query all).
            verbose (bool, optional): create verbose output. Defaults to False.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg system query command fails.

        """
        self.set_sub_command("system")
        self.sub_command_class.set_sub_command("query")
        self.sub_command_class.sub_command_class.rank.value = rank
        self.sub_command_class.sub_command_class.verbose.value = verbose
        return self._get_result()

    def system_start(self):
        """Start the system.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg system start command fails.

        """
        self.set_sub_command("system")
        self.sub_command_class.set_sub_command("start")
        return self._get_result()

    def system_stop(self, force=False):
        """Stop the system.

        Args:
            force (bool, optional): whether to force the stop. Defaults to
                False.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg system stop command fails.

        """
        self.set_sub_command("system")
        self.sub_command_class.set_sub_command("stop")
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


# ************************************************************************
# *** External usage should be replaced by DmgCommand.storage_format() ***
# ************************************************************************
def storage_format(path, hosts, insecure=True):
    """Execute format command through dmg tool to servers provided.

    Args:
        path (str): path to tool's binary
        hosts (list): list of servers to run format on.
        insecure (bool): toggle insecure mode

    Returns:
        Avocado CmdResult object that contains exit status, stdout information.

    """
    # Create and setup the command
    dmg = DmgCommand(path)
    dmg.insecure.value = insecure
    dmg.hostlist.value = hosts

    try:
        result = dmg.storage_format()
    except CommandFailure as details:
        print("<dmg> command failed: {}".format(details))
        return None

    return result
