#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from __future__ import print_function

from command_utils_base import \
    FormattedParameter, CommandWithParameters, YamlParameters
from command_utils import CommandWithSubCommand, YamlCommand


class DmgCommandBase(YamlCommand):
    """Defines a base object representing a dmg command."""

    def __init__(self, path, yaml_cfg=None):
        """Create a dmg Command object.

        Args:
            path (str): path to the dmg command
            yaml_cfg (DmgYamlParameters, optional): dmg config file
                settings. Defaults to None, in which case settings
                must be supplied as command-line parameters.
        """
        super(DmgCommandBase, self).__init__(
            "/run/dmg/*", "dmg", path, yaml_cfg)

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
        elif self.sub_command.value == "cont":
            self.sub_command_class = self.ContSubCommand()
        else:
            self.sub_command_class = None

    class ContSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg cont sub command."""
        def __init__(self):
            """Create a dmg cont subcommand object."""
            super(DmgCommandBase.ContSubCommand, self).__init__(
                "/run/dmg/cont/*", "cont")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg cont sub command object."""
            if self.sub_command.value == "set-owner":
                self.sub_command_class = self.SetownerSubCommand()
            else:
                self.sub_command_class = None

        class SetownerSubCommand(CommandWithParameters):
            """Defines an object for the dmg cont set-owner command."""

            def __init__(self):
                """Create a dmg cont set-owner command object."""
                super(
                    DmgCommandBase.ContSubCommand.SetownerSubCommand,
                    self).__init__(
                        "/run/dmg/cont/set-owner/*", "set-owner")
                self.pool = FormattedParameter("--pool={}", None)
                self.cont = FormattedParameter("--cont={}", None)
                self.user = FormattedParameter("--user={}", None)
                self.group = FormattedParameter("--group={}", None)

    class NetworkSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg network sub command."""

        def __init__(self):
            """Create a dmg network subcommand object."""
            super(DmgCommandBase.NetworkSubCommand, self).__init__(
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
                    DmgCommandBase.NetworkSubCommand.ScanSubCommand,
                    self).__init__(
                        "/run/dmg/network/scan/*", "scan")
                self.provider = FormattedParameter("-p {}", None)
                self.all = FormattedParameter("-a", False)

    class PoolSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg pool sub command."""

        def __init__(self):
            """Create a dmg pool subcommand object."""
            super(DmgCommandBase.PoolSubCommand, self).__init__(
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
            elif self.sub_command.value == "extend":
                self.sub_command_class = self.ExtendSubCommand()
            elif self.sub_command.value == "drain":
                self.sub_command_class = self.DrainSubCommand()
            elif self.sub_command.value == "reintegrate":
                self.sub_command_class = self.ReintegrateSubCommand()
            elif self.sub_command.value == "evict":
                self.sub_command_class = self.EvictSubCommand()
            else:
                self.sub_command_class = None

        class CreateSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool create command."""

            def __init__(self):
                """Create a dmg pool create command object."""
                super(
                    DmgCommandBase.PoolSubCommand.CreateSubCommand,
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
                    DmgCommandBase.PoolSubCommand.ExcludeSubCommand,
                    self).__init__(
                        "/run/dmg/pool/exclude/*", "exclude")
                self.pool = FormattedParameter("--pool={}", None)
                self.rank = FormattedParameter("--rank={}", None)
                self.tgt_idx = FormattedParameter("--target-idx={}", None)

        class ExtendSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool extend command."""

            def __init__(self):
                """Create a dmg pool extend command object."""
                super(
                    DmgCommandBase.PoolSubCommand.ExtendSubCommand,
                    self).__init__(
                        "/run/dmg/pool/extend/*", "extend")
                self.pool = FormattedParameter("--pool={}", None)
                self.ranks = FormattedParameter("--ranks={}", None)
                self.scm_size = FormattedParameter("--scm-size={}", None)
                self.nvme_size = FormattedParameter("--nvme-size={}", None)

        class DrainSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool drain command."""

            def __init__(self):
                """Create a dmg pool drain command object."""
                super(
                    DmgCommandBase.PoolSubCommand.DrainSubCommand,
                    self).__init__(
                        "/run/dmg/pool/drain/*", "drain")
                self.pool = FormattedParameter("--pool={}", None)
                self.rank = FormattedParameter("--rank={}", None)
                self.tgt_idx = FormattedParameter("--target-idx={}", None)

        class ReintegrateSubCommand(CommandWithParameters):
            """Defines an object for dmg pool reintegrate command."""

            def __init__(self):
                """Create a dmg pool reintegrate command object."""
                super(
                    DmgCommandBase.PoolSubCommand.ReintegrateSubCommand,
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
                    DmgCommandBase.PoolSubCommand.DeleteAclSubCommand,
                    self).__init__(
                        "/run/dmg/pool/delete-acl/*", "delete-acl")
                self.pool = FormattedParameter("--pool={}", None)
                self.principal = FormattedParameter("-p {}", None)

        class DestroySubCommand(CommandWithParameters):
            """Defines an object for the dmg pool destroy command."""

            def __init__(self):
                """Create a dmg pool destroy command object."""
                super(
                    DmgCommandBase.PoolSubCommand.DestroySubCommand,
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
                    DmgCommandBase.PoolSubCommand.GetAclSubCommand,
                    self).__init__(
                        "/run/dmg/pool/get-acl/*", "get-acl")
                self.pool = FormattedParameter("--pool={}", None)

        class ListSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool list command."""

            def __init__(self):
                """Create a dmg pool list command object."""
                super(
                    DmgCommandBase.PoolSubCommand.ListSubCommand,
                    self).__init__(
                        "/run/dmg/pool/list/*", "list")

        class OverwriteAclSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool overwrite-acl command."""

            def __init__(self):
                """Create a dmg pool overwrite-acl command object."""
                super(
                    DmgCommandBase.PoolSubCommand.OverwriteAclSubCommand,
                    self).__init__(
                        "/run/dmg/pool/overwrite-acl/*", "overwrite-acl")
                self.pool = FormattedParameter("--pool={}", None)
                self.acl_file = FormattedParameter("-a {}", None)

        class QuerySubCommand(CommandWithParameters):
            """Defines an object for the dmg pool query command."""

            def __init__(self):
                """Create a dmg pool query command object."""
                super(
                    DmgCommandBase.PoolSubCommand.QuerySubCommand,
                    self).__init__(
                        "/run/dmg/pool/query/*", "query")
                self.pool = FormattedParameter("--pool={}", None)

        class SetPropSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool set-prop command."""

            def __init__(self):
                """Create a dmg pool set-prop command object."""
                super(
                    DmgCommandBase.PoolSubCommand.SetPropSubCommand,
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
                    DmgCommandBase.PoolSubCommand.UpdateAclSubCommand,
                    self).__init__(
                        "/run/dmg/pool/update-acl/*", "update-acl")
                self.pool = FormattedParameter("--pool={}", None)
                self.acl_file = FormattedParameter("-a {}", None)
                self.entry = FormattedParameter("-e {}", None)

        class EvictSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool evict command."""

            def __init__(self):
                """Create a dmg pool evict command object."""
                super(
                    DmgCommandBase.PoolSubCommand.EvictSubCommand,
                    self).__init__(
                        "/run/dmg/pool/evict/*", "evict")
                self.pool = FormattedParameter("--pool={}", None)
                self.sys = FormattedParameter("--sys={}", None)

    class StorageSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg storage sub command."""

        def __init__(self):
            """Create a dmg storage subcommand object."""
            super(DmgCommandBase.StorageSubCommand, self).__init__(
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
                    DmgCommandBase.StorageSubCommand.FormatSubCommand,
                    self).__init__(
                        "/run/dmg/storage/format/*", "format")
                self.verbose = FormattedParameter("--verbose", False)
                self.reformat = FormattedParameter("--reformat", False)

        class PrepareSubCommand(CommandWithParameters):
            """Defines an object for the dmg storage format command."""

            def __init__(self):
                """Create a dmg storage prepare command object."""
                super(
                    DmgCommandBase.StorageSubCommand.PrepareSubCommand,
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
            """Defines an object for the dmg storage query command."""

            def __init__(self):
                """Create a dmg storage query command object."""
                super(
                    DmgCommandBase.StorageSubCommand.QuerySubCommand,
                    self).__init__(
                        "/run/dmg/storage/query/*", "query")

            def get_sub_command_class(self):
                # pylint: disable=redefined-variable-type
                """Get the dmg storage query sub command object."""
                if self.sub_command.value == "target-health":
                    self.sub_command_class = self.TargetHealthSubCommand()
                elif self.sub_command.value == "device-health":
                    self.sub_command_class = self.DeviceHealthSubCommand()
                elif self.sub_command.value == "list-devices":
                    self.sub_command_class = self.ListDevicesSubCommand()
                elif self.sub_command.value == "list-pools":
                    self.sub_command_class = self.ListPoolsSubCommand()
                else:
                    self.sub_command_class = None

            class TargetHealthSubCommand(CommandWithParameters):
                """Defines a dmg storage query target-health object."""

                def __init__(self):
                    """Create a dmg storage query target-health object."""
                    super(
                        DmgCommandBase.StorageSubCommand.QuerySubCommand.
                        TargetHealthSubCommand,
                        self).__init__(
                            "/run/dmg/storage/query/target-health/*",
                            "target-health")
                    self.rank = FormattedParameter("-r {}", None)
                    self.tgtid = FormattedParameter("-t {}", None)

            class DeviceHealthSubCommand(CommandWithParameters):
                """Defines a dmg storage query device-health object."""

                def __init__(self):
                    """Create a dmg storage query device-health object."""
                    super(
                        DmgCommandBase.StorageSubCommand.QuerySubCommand.
                        DeviceHealthSubCommand,
                        self).__init__(
                            "/run/dmg/storage/query/device-health/*",
                            "device-health")
                    self.uuid = FormattedParameter("-u {}", None)

            class ListDevicesSubCommand(CommandWithParameters):
                """Defines a dmg storage query list-devices object."""

                def __init__(self):
                    """Create a dmg storage query list-devices object."""
                    super(
                        DmgCommandBase.StorageSubCommand.QuerySubCommand.
                        ListDevicesSubCommand,
                        self).__init__(
                            "/run/dmg/storage/query/list-devices/*",
                            "list-devices")
                    self.rank = FormattedParameter("-r {}", None)
                    self.uuid = FormattedParameter("-u {}", None)
                    self.health = FormattedParameter("-b", False)

            class ListPoolsSubCommand(CommandWithParameters):
                """Defines a dmg storage query list-pools object."""

                def __init__(self):
                    """Create a dmg storage query list-pools object."""
                    super(
                        DmgCommandBase.StorageSubCommand.QuerySubCommand.
                        ListPoolsSubCommand,
                        self).__init__(
                            "/run/dmg/storage/query/list-pools/*",
                            "list-pools")
                    self.rank = FormattedParameter("-r {}", None)
                    self.uuid = FormattedParameter("-u {}", None)
                    self.verbose = FormattedParameter("--verbose", False)

        class ScanSubCommand(CommandWithParameters):
            """Defines an object for the dmg storage scan command."""

            def __init__(self):
                """Create a dmg storage scan command object."""
                super(
                    DmgCommandBase.StorageSubCommand.ScanSubCommand,
                    self).__init__(
                        "/run/dmg/storage/scan/*", "scan")
                self.nvme_health = FormattedParameter("--nvme-health", False)
                self.verbose = FormattedParameter("--verbose", False)

        class SetSubCommand(CommandWithSubCommand):
            """Defines an object for the dmg storage set command."""

            def __init__(self):
                """Create a dmg storage set command object."""
                super(
                    DmgCommandBase.StorageSubCommand.SetSubCommand,
                    self).__init__(
                        "/run/dmg/storage/set/*", "set")

            def get_sub_command_class(self):
                # pylint: disable=redefined-variable-type
                """Get the dmg set sub command object."""
                if self.sub_command.value == "nvme-faulty":
                    self.sub_command_class = self.NvmeFaultySubCommand()
                else:
                    self.sub_command_class = None

            class NvmeFaultySubCommand(CommandWithParameters):
                """Defines a dmg storage set nvme-faulty object."""

                def __init__(self):
                    """Create a dmg storage set nvme-faulty object."""
                    super(
                        DmgCommandBase.StorageSubCommand.SetSubCommand.
                        NvmeFaultySubCommand,
                        self).__init__(
                            "/run/dmg/storage/query/device-state/*",
                            "nvme-faulty")
                    self.uuid = FormattedParameter("-u {}", None)
                    self.force = FormattedParameter("--force", False)

    class SystemSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg system sub command."""

        def __init__(self):
            """Create a dmg system subcommand object."""
            super(DmgCommandBase.SystemSubCommand, self).__init__(
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
                    DmgCommandBase.SystemSubCommand.LeaderQuerySubCommand,
                    self).__init__(
                        "/run/dmg/system/leader-query/*", "leader-query")

        class ListPoolsSubCommand(CommandWithParameters):
            """Defines an object for the dmg system list-pools command."""

            def __init__(self):
                """Create a dmg system list-pools command object."""
                super(
                    DmgCommandBase.SystemSubCommand.ListPoolsSubCommand,
                    self).__init__(
                        "/run/dmg/system/list-pools/*", "list-pools")

        class QuerySubCommand(CommandWithParameters):
            """Defines an object for the dmg system query command."""

            def __init__(self):
                """Create a dmg system query command object."""
                super(
                    DmgCommandBase.SystemSubCommand.QuerySubCommand,
                    self).__init__(
                        "/run/dmg/system/query/*", "query")
                self.ranks = FormattedParameter("--ranks={}")
                self.verbose = FormattedParameter("--verbose", False)

        class StartSubCommand(CommandWithParameters):
            """Defines an object for the dmg system start command."""

            def __init__(self):
                """Create a dmg system start command object."""
                super(
                    DmgCommandBase.SystemSubCommand.StartSubCommand,
                    self).__init__(
                        "/run/dmg/system/start/*", "start")
                self.ranks = FormattedParameter("--ranks={}")
                self.rank_hosts = FormattedParameter("--rank-hosts={}")

        class StopSubCommand(CommandWithParameters):
            """Defines an object for the dmg system stop command."""

            def __init__(self):
                """Create a dmg system stop command object."""
                super(
                    DmgCommandBase.SystemSubCommand.StopSubCommand,
                    self).__init__(
                        "/run/dmg/system/stop/*", "stop")
                self.force = FormattedParameter("--force", False)
                self.ranks = FormattedParameter("--ranks={}")
