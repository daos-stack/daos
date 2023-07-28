"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from socket import gethostname

from ClusterShell.NodeSet import NodeSet

from command_utils_base import FormattedParameter, CommandWithParameters, BasicParameter
from command_utils import CommandWithSubCommand, YamlCommand
from general_utils import nodeset_append_suffix


class DmgCommandBase(YamlCommand):
    """Defines a base object representing a dmg command."""

    def __init__(self, path, yaml_cfg=None, hostlist_suffix=None):
        """Create a dmg Command object.

        Args:
            path (str): path to the dmg command
            yaml_cfg (DmgYamlParameters, optional): dmg config file
                settings. Defaults to None, in which case settings
                must be supplied as command-line parameters.
            hostlist_suffix (str, optional): Suffix to append to each host name. Defaults to None.
        """
        super().__init__("/run/dmg/*", "dmg", path, yaml_cfg)

        # If running dmg on remote hosts, this list needs to include those hosts
        self.temporary_file_hosts = NodeSet(gethostname().split(".")[0])

        # If specified use the configuration file from the YamlParameters object
        default_yaml_file = None
        if self.yaml is not None and hasattr(self.yaml, "filename"):
            default_yaml_file = self.yaml.filename

        self.hostlist_suffix = hostlist_suffix

        self._hostlist = FormattedParameter("-l {}")
        self.hostfile = FormattedParameter("-f {}")
        self.configpath = FormattedParameter("-o {}", default_yaml_file)
        self.insecure = FormattedParameter("-i", False)
        self.debug = FormattedParameter("-d", True)
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
            hostlist (NodeSet): list of host addresses
        """
        if self.hostlist_suffix:
            hostlist = nodeset_append_suffix(hostlist, self.hostlist_suffix)

        if self.yaml:
            if isinstance(hostlist, NodeSet):
                hostlist = list(hostlist)
            elif isinstance(hostlist, str):
                hostlist = hostlist.split(",")
            self.yaml.hostlist.update(hostlist, "dmg.yaml.hostlist")
        else:
            if isinstance(hostlist, NodeSet):
                hostlist = list(hostlist)
            if isinstance(hostlist, list):
                hostlist = ",".join(hostlist)
            self._hostlist.update(hostlist, "dmg._hostlist")

    def get_sub_command_class(self):
        # pylint: disable=redefined-variable-type
        """Get the dmg sub command object based upon the sub-command."""
        if self.sub_command.value == "check":
            self.sub_command_class = self.CheckSubCommand()
        elif self.sub_command.value == "config":
            self.sub_command_class = self.ConfigSubCommand()
        elif self.sub_command.value == "cont":
            self.sub_command_class = self.ContSubCommand()
        elif self.sub_command.value == "network":
            self.sub_command_class = self.NetworkSubCommand()
        elif self.sub_command.value == "pool":
            self.sub_command_class = self.PoolSubCommand()
        elif self.sub_command.value == "server":
            self.sub_command_class = self.ServerSubCommand()
        elif self.sub_command.value == "storage":
            self.sub_command_class = self.StorageSubCommand()
        elif self.sub_command.value == "system":
            self.sub_command_class = self.SystemSubCommand()
        elif self.sub_command.value == "telemetry":
            self.sub_command_class = self.TelemetrySubCommand()
        elif self.sub_command.value == "version":
            self.sub_command_class = self.VersionSubCommand()
        elif self.sub_command.value == "support":
            self.sub_command_class = self.SupportSubCommand()
        else:
            self.sub_command_class = None

    class CheckSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg check sub command."""

        def __init__(self):
            """Create a dmg check subcommand object."""
            super().__init__("run/dmg/check/*", "check")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg check sub command object."""
            if self.sub_command.value == "disable":
                self.sub_command_class = self.DisableSubCommand()
            elif self.sub_command.value == "enable":
                self.sub_command_class = self.EnableSubCommand()
            elif self.sub_command.value == "prop":
                self.sub_command_class = self.PropSubCommand()
            elif self.sub_command.value == "query":
                self.sub_command_class = self.QuerySubCommand()
            elif self.sub_command.value == "repair":
                self.sub_command_class = self.RepairSubCommand()
            elif self.sub_command.value == "start":
                self.sub_command_class = self.StartSubCommand()
            elif self.sub_command.value == "stop":
                self.sub_command_class = self.StopSubCommand()
            else:
                self.sub_command_class = None

        class DisableSubCommand(CommandWithParameters):
            """Defines an object for the dmg check disable command."""

            def __init__(self):
                """Create a dmg check disable object."""
                super().__init__("/run/dmg/check/disable/*", "disable")
                self.pool = BasicParameter(None, position=1)

        class EnableSubCommand(CommandWithParameters):
            """Defines an object for the dmg check enable command."""

            def __init__(self):
                """Create a dmg check enable object."""
                super().__init__("/run/dmg/check/enable/*", "enable")
                self.pool = BasicParameter(None, position=1)

        class PropSubCommand(CommandWithParameters):
            """Defines an object for the dmg check prop command."""

            def __init__(self):
                """Create a dmg check prop object."""
                super().__init__("/run/dmg/check/prop/*", "prop")
                self.pool = BasicParameter(None, position=1)

        class QuerySubCommand(CommandWithParameters):
            """Defines an object for the dmg check query command."""

            def __init__(self):
                """Create a dmg check query object."""
                super().__init__("/run/dmg/check/query/*", "query")
                self.pool = BasicParameter(None, position=1)

        class RepairSubCommand(CommandWithParameters):
            """Defines an object for the dmg check repair command."""

            def __init__(self):
                """Create a dmg check repair object."""
                super().__init__("/run/dmg/check/repair/*", "repair")
                self.seq_num = BasicParameter(None, position=1)
                self.action = BasicParameter(None, position=2)
                self.for_all = FormattedParameter("--for-all", False)

        class StartSubCommand(CommandWithParameters):
            """Defines an object for the dmg check start command."""

            def __init__(self):
                """Create a dmg check start object."""
                super().__init__("/run/dmg/check/start/*", "start")
                self.pool = BasicParameter(None, position=1)
                self.dry_run = FormattedParameter("--dry-run", False)
                self.reset = FormattedParameter("--reset", False)
                self.failout = FormattedParameter("--failout", False)
                self.auto = FormattedParameter("--auto", False)

        class StopSubCommand(CommandWithParameters):
            """Defines an object for the dmg check stop command."""

            def __init__(self):
                """Create a dmg check stop object."""
                super().__init__("/run/dmg/check/stop/*", "stop")
                self.pool = BasicParameter(None, position=1)

    class ConfigSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg config sub command."""

        def __init__(self):
            """Create a dmg config subcommand object."""
            super(DmgCommandBase.ConfigSubCommand, self).__init__(
                "run/dmg/config/*", "config")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg config sub command object."""
            if self.sub_command.value == "generate":
                self.sub_command_class = self.GenerateSubCommand()
            else:
                self.sub_command_class = None

        class GenerateSubCommand(CommandWithParameters):
            """Defines an object for the dmg config generate command."""

            def __init__(self):
                """Create a dmg config generate object."""
                super(
                    DmgCommandBase.ConfigSubCommand.GenerateSubCommand,
                    self).__init__(
                        "/run/dmg/config/generate/*", "generate")
                self.access_points = FormattedParameter(
                    "--access-points={}", None)
                self.num_engines = FormattedParameter("--num-engines={}", None)
                self.scm_only = FormattedParameter("--scm-only", False)
                self.net_class = FormattedParameter("--net-class={}", None)
                self.net_provider = FormattedParameter("--net-provider={}", None)
                self.use_tmpfs_scm = FormattedParameter("--use-tmpfs-scm", False)
                self.control_metadata_path = FormattedParameter(
                    "--control-metadata-path={}", None)

    class ContSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg cont sub command."""

        def __init__(self):
            """Create a dmg cont subcommand object."""
            super().__init__("/run/dmg/cont/*", "cont")

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
                super().__init__("/run/dmg/cont/set-owner/*", "set-owner")
                self.pool = FormattedParameter("--pool={}", None)
                self.cont = FormattedParameter("--cont={}", None)
                self.user = FormattedParameter("--user={}", None)
                self.group = FormattedParameter("--group={}", None)

    class NetworkSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg network sub command."""

        def __init__(self):
            """Create a dmg network subcommand object."""
            super().__init__("/run/dmg/network/*", "network")

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
                super().__init__("/run/dmg/network/scan/*", "scan")
                self.provider = FormattedParameter("-p {}", None)

    class PoolSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg pool sub command."""

        def __init__(self):
            """Create a dmg pool subcommand object."""
            super().__init__("/run/dmg/pool/*", "pool")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg pool sub command object."""
            if self.sub_command.value == "create":
                self.sub_command_class = self.CreateSubCommand()
            elif self.sub_command.value == "delete-acl":
                self.sub_command_class = self.DeleteAclSubCommand()
            elif self.sub_command.value == "destroy":
                self.sub_command_class = self.DestroySubCommand()
            elif self.sub_command.value == "drain":
                self.sub_command_class = self.DrainSubCommand()
            elif self.sub_command.value == "evict":
                self.sub_command_class = self.EvictSubCommand()
            elif self.sub_command.value == "exclude":
                self.sub_command_class = self.ExcludeSubCommand()
            elif self.sub_command.value == "extend":
                self.sub_command_class = self.ExtendSubCommand()
            elif self.sub_command.value == "get-acl":
                self.sub_command_class = self.GetAclSubCommand()
            elif self.sub_command.value == "get-prop":
                self.sub_command_class = self.GetPropSubCommand()
            elif self.sub_command.value == "list":
                self.sub_command_class = self.ListSubCommand()
            elif self.sub_command.value == "overwrite-acl":
                self.sub_command_class = self.OverwriteAclSubCommand()
            elif self.sub_command.value == "query":
                self.sub_command_class = self.QuerySubCommand()
            elif self.sub_command.value == "query-targets":
                self.sub_command_class = self.QueryTargetsSubCommand()
            elif self.sub_command.value == "set-prop":
                self.sub_command_class = self.SetPropSubCommand()
            elif self.sub_command.value == "update-acl":
                self.sub_command_class = self.UpdateAclSubCommand()
            elif self.sub_command.value == "upgrade":
                self.sub_command_class = self.UpgradeSubCommand()
            elif self.sub_command.value == "reintegrate":
                self.sub_command_class = self.ReintegrateSubCommand()
            else:
                self.sub_command_class = None

        class CreateSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool create command."""

            def __init__(self):
                """Create a dmg pool create command object."""
                super().__init__("/run/dmg/pool/create/*", "create")
                self.label = BasicParameter(None, position=1)
                self.group = FormattedParameter("--group={}", None)
                self.user = FormattedParameter("--user={}", None)
                self.acl_file = FormattedParameter("--acl-file={}", None)
                self.size = FormattedParameter("--size={}", None)
                self.tier_ratio = FormattedParameter("--tier-ratio={}", None)
                self.scm_size = FormattedParameter("--scm-size={}", None)
                self.nvme_size = FormattedParameter("--nvme-size={}", None)
                self.ranks = FormattedParameter("--ranks={}", None)
                self.nsvc = FormattedParameter("--nsvc={}", None)
                self.sys = FormattedParameter("--sys={}", None)
                self.properties = FormattedParameter("--properties={}", None)
                self.nranks = FormattedParameter("--nranks={}", None)

        class DeleteAclSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool delete-acl command."""

            def __init__(self):
                """Create a dmg pool delete-acl command object."""
                super().__init__("/run/dmg/pool/delete-acl/*", "delete-acl")
                self.pool = BasicParameter(None, position=1)
                self.principal = FormattedParameter("-p {}", None)

        class DestroySubCommand(CommandWithParameters):
            """Defines an object for the dmg pool destroy command."""

            def __init__(self):
                """Create a dmg pool destroy command object."""
                super().__init__("/run/dmg/pool/destroy/*", "destroy")
                self.pool = BasicParameter(None, position=1)
                self.force = FormattedParameter("--force", False)
                self.recursive = FormattedParameter("--recursive", False)

        class DrainSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool drain command."""

            def __init__(self):
                """Create a dmg pool drain command object."""
                super().__init__("/run/dmg/pool/drain/*", "drain")
                self.pool = BasicParameter(None, position=1)
                self.rank = FormattedParameter("--rank={}", None)
                self.tgt_idx = FormattedParameter("--target-idx={}", None)

        class EvictSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool evict command."""

            def __init__(self):
                """Create a dmg pool evict command object."""
                super().__init__("/run/dmg/pool/evict/*", "evict")
                self.pool = BasicParameter(None, position=1)

        class ExcludeSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool exclude command."""

            def __init__(self):
                """Create a dmg pool exclude command object."""
                super().__init__("/run/dmg/pool/exclude/*", "exclude")
                self.pool = BasicParameter(None, position=1)
                self.rank = FormattedParameter("--rank={}", None)
                self.tgt_idx = FormattedParameter("--target-idx={}", None)

        class ExtendSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool extend command."""

            def __init__(self):
                """Create a dmg pool extend command object."""
                super().__init__("/run/dmg/pool/extend/*", "extend")
                self.pool = BasicParameter(None, position=1)
                self.ranks = FormattedParameter("--ranks={}", None)

        class GetAclSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool get-acl command."""

            def __init__(self):
                """Create a dmg pool get-acl command object."""
                super().__init__("/run/dmg/pool/get-acl/*", "get-acl")
                self.pool = BasicParameter(None, position=1)
                self.outfile = FormattedParameter("--outfile={}", None)
                self.force = FormattedParameter("--force", False)
                self.verbose = FormattedParameter("--verbose", False)

        class GetPropSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool get-prop command."""

            def __init__(self):
                """Create a dmg pool get-prop command object."""
                super().__init__("/run/dmg/pool/get-prop/*", "get-prop")
                self.pool = BasicParameter(None, position=1)
                self.name = BasicParameter(None, position=2)

        class ListSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool list command."""

            def __init__(self):
                """Create a dmg pool list command object."""
                super().__init__("/run/dmg/pool/list/*", "list")
                self.no_query = FormattedParameter("--no-query", False)
                self.verbose = FormattedParameter("--verbose", False)

        class OverwriteAclSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool overwrite-acl command."""

            def __init__(self):
                """Create a dmg pool overwrite-acl command object."""
                super().__init__(
                    "/run/dmg/pool/overwrite-acl/*", "overwrite-acl")
                self.pool = BasicParameter(None, position=1)
                self.acl_file = FormattedParameter("-a {}", None)

        class QuerySubCommand(CommandWithParameters):
            """Defines an object for the dmg pool query command."""

            def __init__(self):
                """Create a dmg pool query command object."""
                super().__init__("/run/dmg/pool/query/*", "query")
                self.pool = BasicParameter(None, position=1)
                self.show_enabled = FormattedParameter("--show-enabled", False)
                self.show_disabled = FormattedParameter("--show-disabled", False)

        class QueryTargetsSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool query-targets command."""

            def __init__(self):
                """Create a dmg pool query-targets command object."""
                super().__init__("/run/dmg/pool/query-targets/*", "query-targets")
                self.pool = BasicParameter(None, position=1)
                self.rank = FormattedParameter("--rank={}", None)
                self.target_idx = FormattedParameter("--target-idx={}", None)

        class ReintegrateSubCommand(CommandWithParameters):
            """Defines an object for dmg pool reintegrate command."""

            def __init__(self):
                """Create a dmg pool reintegrate command object."""
                super().__init__("/run/dmg/pool/reintegrate/*", "reintegrate")
                self.pool = BasicParameter(None, position=1)
                self.rank = FormattedParameter("--rank={}", None)
                self.tgt_idx = FormattedParameter("--target-idx={}", None)

        class SetPropSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool set-prop command."""

            def __init__(self):
                """Create a dmg pool set-prop command object."""
                super().__init__("/run/dmg/pool/set-prop/*", "set-prop")
                self.pool = BasicParameter(None, position=1)
                self.properties = BasicParameter(None, position=2)

        class UpdateAclSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool update-acl command."""

            def __init__(self):
                """Create a dmg pool update-acl command object."""
                super().__init__("/run/dmg/pool/update-acl/*", "update-acl")
                self.pool = BasicParameter(None, position=1)
                self.acl_file = FormattedParameter("-a {}", None)
                self.entry = FormattedParameter("-e {}", None)

        class UpgradeSubCommand(CommandWithParameters):
            """Defines an object for the dmg pool upgrade command."""

            def __init__(self):
                """Create a dmg pool upgrade command object."""
                super().__init__("/run/dmg/pool/upgrade/*", "upgrade")
                self.pool = BasicParameter(None, position=1)

    class ServerSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg server sub command."""

        def __init__(self):
            """Create a dmg server subcommand object."""
            super().__init__("/run/dmg/server/*", "server")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg server sub command object."""
            if self.sub_command.value == "set-logmasks":
                self.sub_command_class = self.SetLogmasksSubCommand()
            else:
                self.sub_command_class = None

        class SetLogmasksSubCommand(CommandWithParameters):
            """Defines an object for the dmg server set-logmasks command."""

            def __init__(self):
                """Create a dmg server set-logmasks command object."""
                super().__init__("/run/dmg/server/set-logmasks/*", "set-logmasks")
                # Set log masks for a set of facilities to a given level.
                # Masks syntax is identical to the 'D_LOG_MASK' environment variable.
                self.masks = FormattedParameter("-m {}", None)
                # Streams syntax is identical to the 'DD_MASK' environment variable.
                self.streams = FormattedParameter("-d {}", None)
                # Subsystems syntax is identical to the 'DD_SUBSYS' environment variable.
                self.subsystems = FormattedParameter("-s {}", None)

    class SupportSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg support sub command."""

        def __init__(self):
            """Create a dmg support subcommand object."""
            super().__init__("/run/dmg/support/*", "support")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg support sub command object."""
            if self.sub_command.value == "collect-log":
                self.sub_command_class = self.CollectlogSubCommand()
            else:
                self.sub_command_class = None

        class CollectlogSubCommand(CommandWithParameters):
            """Defines an object for the dmg support collect-log command."""

            def __init__(self):
                """Create a dmg support collect-log command object."""
                super().__init__("/run/dmg/support/collect-log/*", "collect-log")
                self.stop_on_error = FormattedParameter("--stop-on-error", False)
                self.target_folder = FormattedParameter("--target-folder={}", None)
                self.archive = FormattedParameter("--archive", False)
                self.extra_logs_dir = FormattedParameter("--extra-logs-dir={}", None)
                self.target_host = FormattedParameter("--target-host={}", None)

    class StorageSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg storage sub command."""

        def __init__(self):
            """Create a dmg storage subcommand object."""
            super().__init__("/run/dmg/storage/*", "storage")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg storage sub command object."""
            if self.sub_command.value == "led":
                self.sub_command_class = self.LedSubCommand()
            elif self.sub_command.value == "replace":
                self.sub_command_class = self.ReplaceSubCommand()
            elif self.sub_command.value == "format":
                self.sub_command_class = self.FormatSubCommand()
            elif self.sub_command.value == "query":
                self.sub_command_class = self.QuerySubCommand()
            elif self.sub_command.value == "scan":
                self.sub_command_class = self.ScanSubCommand()
            elif self.sub_command.value == "set":
                self.sub_command_class = self.SetSubCommand()
            else:
                self.sub_command_class = None

        class ReplaceSubCommand(CommandWithSubCommand):
            """Defines an object for the dmg storage replace sub command"""

            def __init__(self):
                """Create a dmg storage replace sub command object."""
                super().__init__("/run/dmg/storage/replace/*", "replace")

            def get_sub_command_class(self):
                # pylint: disable=redefined-variable-type
                """Get the dmg storage replace sub command object."""
                if self.sub_command.value == "nvme":
                    self.sub_command_class = self.NVMESubCommand()
                else:
                    self.sub_command_class = None

            class NVMESubCommand(CommandWithParameters):
                """Get dmg storage replace sub command object"""

                def __init__(self):
                    """Create a dmg storage replace sub command object."""
                    super().__init__("/run/dmg/storage/replace/nvme/*", "nvme")
                    self.old_uuid = FormattedParameter("--old-uuid {}", None)
                    self.new_uuid = FormattedParameter("--new-uuid {}", None)
                    self.no_reint = FormattedParameter("--no-reint", False)

        class LedSubCommand(CommandWithSubCommand):
            """Defines an object for the dmg storage LED command"""

            def __init__(self):
                """Create a dmg storage led sub command object."""
                super().__init__("/run/dmg/storage/led/*", "led")

            def get_sub_command_class(self):
                # pylint: disable=redefined-variable-type
                """Get the dmg storage led sub command object."""
                if self.sub_command.value == "identify":
                    self.sub_command_class = self.IdentifySubCommand()
                elif self.sub_command.value == "check":
                    self.sub_command_class = self.CheckSubCommand()
                else:
                    self.sub_command_class = None

            class IdentifySubCommand(CommandWithParameters):
                """Get dmg storage led identify sub command object"""

                def __init__(self):
                    """Create a dmg storage led identify command object."""
                    super().__init__("/run/dmg/storage/led/identify/*", "identify")
                    self.timeout = FormattedParameter("--timeout {}", None)
                    self.reset = FormattedParameter("--reset", False)
                    self.ids = BasicParameter(None)

            class CheckSubCommand(CommandWithParameters):
                """Get dmg storage led check sub command object"""

                def __init__(self):
                    """Create a dmg storage led check command object."""
                    super().__init__("/run/dmg/storage/led/check/*", "check")
                    self.ids = BasicParameter(None)

        class FormatSubCommand(CommandWithParameters):
            """Defines an object for the dmg storage format command."""

            def __init__(self):
                """Create a dmg storage format command object."""
                super().__init__("/run/dmg/storage/format/*", "format")
                self.verbose = FormattedParameter("--verbose", False)
                self.force = FormattedParameter("--force", False)

        class QuerySubCommand(CommandWithSubCommand):
            """Defines an object for the dmg storage query command."""

            def __init__(self):
                """Create a dmg storage query command object."""
                super().__init__("/run/dmg/storage/query/*", "query")

            def get_sub_command_class(self):
                # pylint: disable=redefined-variable-type
                """Get the dmg storage query sub command object."""
                if self.sub_command.value == "device-health":
                    self.sub_command_class = self.DeviceHealthSubCommand()
                elif self.sub_command.value == "list-devices":
                    self.sub_command_class = self.ListDevicesSubCommand()
                elif self.sub_command.value == "list-pools":
                    self.sub_command_class = self.ListPoolsSubCommand()
                elif self.sub_command.value == "usage":
                    self.sub_command_class = self.UsageSubCommand()
                else:
                    self.sub_command_class = None

            class DeviceHealthSubCommand(CommandWithParameters):
                """Defines a dmg storage query device-health object."""

                def __init__(self):
                    """Create a dmg storage query device-health object."""
                    super().__init__("/run/dmg/storage/query/device-health/*", "device-health")
                    self.uuid = FormattedParameter("-u {}", None)

            class ListDevicesSubCommand(CommandWithParameters):
                """Defines a dmg storage query list-devices object."""

                def __init__(self):
                    """Create a dmg storage query list-devices object."""
                    super().__init__("/run/dmg/storage/query/list-devices/*", "list-devices")
                    self.rank = FormattedParameter("-r {}", None)
                    self.uuid = FormattedParameter("-u {}", None)
                    self.health = FormattedParameter("-b", False)

            class ListPoolsSubCommand(CommandWithParameters):
                """Defines a dmg storage query list-pools object."""

                def __init__(self):
                    """Create a dmg storage query list-pools object."""
                    super().__init__("/run/dmg/storage/query/list-pools/*", "list-pools")
                    self.rank = FormattedParameter("-r {}", None)
                    self.uuid = FormattedParameter("-u {}", None)
                    self.verbose = FormattedParameter("--verbose", False)

            class UsageSubCommand(CommandWithParameters):
                """Defines a dmg storage query usage object."""

                def __init__(self):
                    """Create a dmg storage query usage object."""
                    super().__init__("/run/dmg/storage/query/usage/*", "usage")

        class ScanSubCommand(CommandWithParameters):
            """Defines an object for the dmg storage scan command."""

            def __init__(self):
                """Create a dmg storage scan command object."""
                super().__init__("/run/dmg/storage/scan/*", "scan")
                self.nvme_health = FormattedParameter("--nvme-health", False)
                self.verbose = FormattedParameter("--verbose", False)

        class SetSubCommand(CommandWithSubCommand):
            """Defines an object for the dmg storage set command."""

            def __init__(self):
                """Create a dmg storage set command object."""
                super().__init__("/run/dmg/storage/set/*", "set")

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
                    super().__init__("/run/dmg/storage/query/device-state/*", "nvme-faulty")
                    self.uuid = FormattedParameter("-u {}", None)
                    self.force = FormattedParameter("--force", False)

    class SystemSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg system sub command."""

        def __init__(self):
            """Create a dmg system subcommand object."""
            super().__init__("/run/dmg/system/*", "system")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg system sub command object."""
            if self.sub_command.value == "cleanup":
                self.sub_command_class = self.CleanupSubCommand()
            elif self.sub_command.value == "erase":
                self.sub_command_class = self.EraseSubCommand()
            elif self.sub_command.value == "leader-query":
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

        class CleanupSubCommand(CommandWithParameters):
            """Defines an object for the dmg system cleanup command."""

            def __init__(self):
                """Create a dmg system cleanup command object."""
                super().__init__("/run/dmg/system/cleanup/*", "cleanup")
                self.machinename = FormattedParameter("{}", None)
                self.verbose = FormattedParameter("--verbose", False)

        class EraseSubCommand(CommandWithParameters):
            """Defines an object for the dmg system erase command."""

            def __init__(self):
                """Create a dmg system erase command object."""
                super().__init__(
                    "/run/dmg/system/erase/*", "erase")

        class LeaderQuerySubCommand(CommandWithParameters):
            """Defines an object for the dmg system leader-query command."""

            def __init__(self):
                """Create a dmg system leader-query command object."""
                super().__init__(
                    "/run/dmg/system/leader-query/*", "leader-query")

        class ListPoolsSubCommand(CommandWithParameters):
            """Defines an object for the dmg system list-pools command."""

            def __init__(self):
                """Create a dmg system list-pools command object."""
                super().__init__("/run/dmg/system/list-pools/*", "list-pools")

        class QuerySubCommand(CommandWithParameters):
            """Defines an object for the dmg system query command."""

            def __init__(self):
                """Create a dmg system query command object."""
                super().__init__("/run/dmg/system/query/*", "query")
                self.ranks = FormattedParameter("--ranks={}")
                self.verbose = FormattedParameter("--verbose", False)

        class StartSubCommand(CommandWithParameters):
            """Defines an object for the dmg system start command."""

            def __init__(self):
                """Create a dmg system start command object."""
                super().__init__("/run/dmg/system/start/*", "start")
                self.ranks = FormattedParameter("--ranks={}")
                self.rank_hosts = FormattedParameter("--rank-hosts={}")

        class StopSubCommand(CommandWithParameters):
            """Defines an object for the dmg system stop command."""

            def __init__(self):
                """Create a dmg system stop command object."""
                super().__init__("/run/dmg/system/stop/*", "stop")
                self.force = FormattedParameter("--force", False)
                self.ranks = FormattedParameter("--ranks={}")

    class TelemetrySubCommand(CommandWithSubCommand):
        """Defines an object for the dmg telemetry sub command."""

        def __init__(self):
            """Create a dmg telemetry subcommand object."""
            super().__init__("/run/dmg/telemetry/*", "telemetry")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg telemetry sub command object."""
            if self.sub_command.value == "metrics":
                self.sub_command_class = self.MetricsSubCommand()
            else:
                self.sub_command_class = None

        class MetricsSubCommand(CommandWithSubCommand):
            """Defines an object for the dmg telemetry metrics command."""

            def __init__(self):
                """Create a dmg telemetry metrics command object."""
                super().__init__("/run/dmg/telemetry/metrics/*", "metrics")

            def get_sub_command_class(self):
                # pylint: disable=redefined-variable-type
                """Get the dmg telemetry metrics sub command object."""
                if self.sub_command.value == "list":
                    self.sub_command_class = self.ListSubCommand()
                elif self.sub_command.value == "query":
                    self.sub_command_class = self.QuerySubCommand()
                else:
                    self.sub_command_class = None

            class ListSubCommand(CommandWithParameters):
                """Defines a dmg telemetry metrics list object."""

                def __init__(self):
                    """Create a dmg telemetry metrics list object."""
                    super().__init__(
                        "/run/dmg/telemetry/metrics/list/*", "list")
                    self.host = FormattedParameter("--host-list={}", None)
                    self.port = FormattedParameter("--port={}", None)

            class QuerySubCommand(CommandWithParameters):
                """Defines a dmg telemetry metrics query object."""

                def __init__(self):
                    """Create a dmg telemetry metrics query object."""
                    super().__init__(
                        "/run/dmg/telemetry/metrics/query/*", "query")
                    self.host = FormattedParameter("--host-list={}", None)
                    self.port = FormattedParameter("--port={}", None)
                    self.metrics = FormattedParameter("--metrics={}", None)

    class VersionSubCommand(CommandWithSubCommand):
        """Defines an object for the dmg version sub command."""

        def __init__(self):
            """Create a dmg version subcommand object."""
            super(DmgCommandBase.VersionSubCommand, self).__init__(
                "/run/dmg/version/*", "version")

    def _get_new(self):
        """Get a new object based upon this one.

        Returns:
            DmgCommandBase: a new DmgCommandBase object
        """
        return DmgCommandBase(self._path, self.yaml, self.hostlist_suffix)
