#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from command_utils_base import FormattedParameter, CommandWithParameters,\
    CommandWithPositionalParameters, PositionalParameter
from command_utils import CommandWithSubCommand


class DaosCommandBase(CommandWithSubCommand):
    """Defines a object representing a daos command."""

    def __init__(self, path):
        """Create a daos Command object.

        Args:
            path (str): path to the daos command
        """
        super().__init__("/run/daos/*", "daos", path)

        self.json = FormattedParameter("-j", False)

    def get_sub_command_class(self):
        # pylint: disable=redefined-variable-type
        """Get the dmg sub command object based upon the sub-command."""
        if self.sub_command.value == "pool":
            self.sub_command_class = self.PoolSubCommand()
        elif self.sub_command.value == "container":
            self.sub_command_class = self.ContainerSubCommand()
        elif self.sub_command.value == "object":
            self.sub_command_class = self.ObjectSubCommand()
        elif self.sub_command.value == "filesystem":
            self.sub_command_class = self.FilesystemSubCommand()
        else:
            self.sub_command_class = None

    class PoolSubCommand(CommandWithSubCommand):
        """Defines an object for the daos pool sub command."""

        def __init__(self):
            """Create a daos pool subcommand object."""
            super().__init__(
                "/run/daos/pool/*", "pool")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg network sub command object."""
            if ((self.sub_command.value == "list-containers") or
                (self.sub_command.value == "list")):
                self.sub_command_class = self.ListContainersSubCommand()
            elif self.sub_command.value == "query":
                self.sub_command_class = self.QuerySubCommand()
            elif self.sub_command.value == "stat":
                self.sub_command_class = self.StatSubCommand()
            elif self.sub_command.value == "list-attrs":
                self.sub_command_class = self.ListAttrsSubCommand()
            elif self.sub_command.value == "get-attr":
                self.sub_command_class = self.GetAttrSubCommand()
            elif self.sub_command.value == "set-attr":
                self.sub_command_class = self.SetAttrSubCommand()
            elif self.sub_command.value == "autotest":
                self.sub_command_class = self.AutotestSubCommand()
            else:
                self.sub_command_class = None

        class CommonPoolSubCommand(CommandWithPositionalParameters):
            """Defines an object for the common daos pool sub-command.

            Use PositionalParameter for positional parameter subcommands. The
            value passed in defines the position. "pool" comes first, so it gets
            1. Other subcommands get 2 or later. For example set-attr's attr and
            value gets 2 and 3 because the order is "daos pool set-attr <attr>
            <value>".
            """

            def __init__(self, sub_command):
                """Create a common daos pool sub-command object.

                Args:
                    sub_command (str): sub-command name
                """
                super().__init__(
                    "/run/daos/pool/{}/*".format(sub_command), sub_command)
                self.pool = PositionalParameter(1)
                self.sys_name = FormattedParameter("--sys-name={}")
                self.sys = FormattedParameter("--sys={}")

        class ListContainersSubCommand(CommonPoolSubCommand):
            """Defines an object for the daos pool list-containers command."""

            def __init__(self):
                """Create a daos pool list-containers command object."""
                super().__init__("list-containers")

        class QuerySubCommand(CommonPoolSubCommand):
            """Defines an object for the daos pool query command."""

            def __init__(self):
                """Create a daos pool query command object."""
                super().__init__("query")

        class StatSubCommand(CommonPoolSubCommand):
            """Defines an object for the daos pool stat command."""

            def __init__(self):
                """Create a daos pool stat command object."""
                super().__init__("stat")

        class ListAttrsSubCommand(CommonPoolSubCommand):
            """Defines an object for the daos pool list-attr command."""

            def __init__(self):
                """Create a daos pool list-attr command object."""
                super().__init__("list-attrs")
                self.sys_name = FormattedParameter("--sys-name={}")
                self.verbose = FormattedParameter("--verbose", False)

        class GetAttrSubCommand(CommonPoolSubCommand):
            """Defines an object for the daos pool get-attr command."""

            def __init__(self):
                """Create a daos pool get-attr command object."""
                super().__init__("get-attr")
                self.attr = PositionalParameter(2)
                self.sys_name = FormattedParameter("--sys-name={}")

        class SetAttrSubCommand(CommonPoolSubCommand):
            """Defines an object for the daos pool set-attr command."""

            def __init__(self):
                """Create a daos pool set-attr command object."""
                super().__init__("set-attr")
                self.attr = PositionalParameter(2)
                self.value = PositionalParameter(3)
                self.sys_name = FormattedParameter("--sys-name={}")

        class AutotestSubCommand(CommonPoolSubCommand):
            """Defines an object for the daos pool autotest command."""

            def __init__(self):
                """Create a daos pool autotest command object."""
                super().__init__("autotest")

    class ContainerSubCommand(CommandWithSubCommand):
        """Defines an object for the daos container sub command."""

        def __init__(self):
            """Create a daos container subcommand object."""
            super().__init__("/run/daos/container/*", "container")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg network sub command object."""
            if self.sub_command.value == "create":
                self.sub_command_class = self.CreateSubCommand()
            elif self.sub_command.value == "clone":
                self.sub_command_class = self.CloneSubCommand()
            elif self.sub_command.value == "destroy":
                self.sub_command_class = self.DestroySubCommand()
            elif self.sub_command.value == "check":
                self.sub_command_class = self.CheckSubCommand()
            elif self.sub_command.value == "list-objects":
                self.sub_command_class = self.ListObjectsSubCommand()
            elif self.sub_command.value == "query":
                self.sub_command_class = self.QuerySubCommand()
            elif self.sub_command.value == "get-acl":
                self.sub_command_class = self.GetAclSubCommand()
            elif self.sub_command.value == "overwrite-acl":
                self.sub_command_class = self.OverwriteAclSubCommand()
            elif self.sub_command.value == "update-acl":
                self.sub_command_class = self.UpdateAclSubCommand()
            elif self.sub_command.value == "delete-acl":
                self.sub_command_class = self.DeleteAclSubCommand()
            elif self.sub_command.value == "stat":
                self.sub_command_class = self.StatSubCommand()
            elif self.sub_command.value == "list-attrs":
                self.sub_command_class = self.ListAttrsSubCommand()
            elif self.sub_command.value == "del-attrs":
                self.sub_command_class = self.DelAttrSubCommand()
            elif self.sub_command.value == "get-attr":
                self.sub_command_class = self.GetAttrSubCommand()
            elif self.sub_command.value == "set-attr":
                self.sub_command_class = self.SetAttrSubCommand()
            elif self.sub_command.value == "get-prop":
                self.sub_command_class = self.GetPropSubCommand()
            elif self.sub_command.value == "set-prop":
                self.sub_command_class = self.SetPropSubCommand()
            elif self.sub_command.value == "set-owner":
                self.sub_command_class = self.SetOwnerSubCommand()
            elif self.sub_command.value == "create-snap":
                self.sub_command_class = self.CreateSnapSubCommand()
            elif self.sub_command.value == "list-snaps":
                self.sub_command_class = self.ListSnapsSubCommand()
            elif self.sub_command.value == "destroy-snap":
                self.sub_command_class = self.DestroySnapSubCommand()
            elif self.sub_command.value == "rollback":
                self.sub_command_class = self.RollbackSubCommand()
            else:
                self.sub_command_class = None

        class CommonContainerSubCommand(CommandWithParameters):
            """Defines an object for the common daos container sub-command."""

            def __init__(self, sub_command):
                """Create a common daos container sub-command object.

                Args:
                    sub_command (str): sub-command name
                """
                super().__init__(
                        "/run/daos/container/{}/*".format(sub_command),
                        sub_command)
                self.pool = FormattedParameter("--pool={}")
                self.sys_name = FormattedParameter("--sys-name={}")
                self.cont = FormattedParameter("--cont={}")
                self.path = FormattedParameter("--path={}")

        class CreateSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container create command."""

            def __init__(self):
                """Create a daos container create command object."""
                super().__init__("create")
                # Additional daos container create parameters:
                #   --type=<type>
                #           container type (HDF5, POSIX)
                self.type = FormattedParameter("--type={}")
                #   --oclass=<object_class>
                #           container object class:
                #               S1, S2, S4, SX, RP_2G1, RP_2G2, RP_2GX, RP_3G1,
                #               RP_3G2, RP_3GX, RP_4G1, RP_4G2, RP_4GX, RP_XSF,
                #               S1_ECHO, RP_2G1_ECHO, RP_3G1_ECHO, RP_4G1_ECHO,
                #               RP_3G1_SR, RP_2G1_SR, S1_SR, EC_2P1G1, EC_2P2G1,
                #               EC_8P2G1
                self.oclass = FormattedParameter("--oclass={}")
                #   --chunk_size=BYTES
                #           chunk size of files created. Supports suffixes:
                #               K (KB), M (MB), G (GB), T (TB), P (PB), E (EB)
                self.chunk_size = FormattedParameter("--chunk_size={}")
                #   --properties=<name>:<value>[,<name>:<value>,...]
                #           - supported names:
                #               label, cksum, cksum_size, srv_cksum, rf
                #           - supported values:
                #               label:      <any string>
                #               cksum:      off, crc[16,32,64], sha1
                #               cksum_size: <any size>
                #               srv_cksum:  on, off
                #               rf:         [0-4]
                self.properties = FormattedParameter("--properties={}")
                #   --acl-file=PATH
                #           input file containing ACL
                self.acl_file = FormattedParameter("--acl-file={}", None)

        class CloneSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container clone command."""

            def __init__(self):
                """Create a daos container clone command object."""
                super().__init__("clone")
                self.src = FormattedParameter("--src={}")
                self.dst = FormattedParameter("--dst={}")

        class DestroySubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container destroy command."""

            def __init__(self):
                """Create a daos container destroy command object."""
                super().__init__("destroy")
                self.force = FormattedParameter("--force", False)

        class ListObjectsSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container list-objects command."""

            def __init__(self):
                """Create a daos container list-objects command object."""
                super().__init__("list-objects")

        class QuerySubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container query command."""

            def __init__(self):
                """Create a daos container query command object."""
                super().__init__("query")

        class CheckSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container check command."""

            def __init__(self):
                """Create a daos container check command object."""
                super(
                    DaosCommandBase.ContainerSubCommand.CheckSubCommand,
                    self).__init__("check")

        class GetAclSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container get-acl command."""

            def __init__(self):
                """Create a daos container get-acl command object."""
                super().__init__("get-acl")
                # Additional daos container create parameters:
                #   --verbose
                #           verbose mode (get-acl)
                self.verbose = FormattedParameter("--verbose", False)
                #   --outfile=PATH
                #           write ACL to file (get-acl)
                self.outfile = FormattedParameter("--outfile={}")

        class OverwriteAclSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container overwrite-acl cmd."""

            def __init__(self):
                """Create a daos container overwrite-acl command object."""
                super().__init__("overwrite-acl")
                self.acl_file = FormattedParameter("--acl-file={}")

        class UpdateAclSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container update-acl command."""

            def __init__(self):
                """Create a daos container update-acl command object."""
                super().__init__("update-acl")
                self.acl_file = FormattedParameter("--acl-file={}")
                self.entry = FormattedParameter("--entry={}")

        class DeleteAclSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container delete-acl command."""

            def __init__(self):
                """Create a daos container delete-acl command object."""
                super().__init__("delete-acl")
                self.principal = FormattedParameter("--principal={}")

        class StatSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container stat command."""

            def __init__(self):
                """Create a daos container stat command object."""
                super().__init__("stat")

        class ListAttrsSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container list-attrs command."""

            def __init__(self):
                """Create a daos container list-attrs command object."""
                super().__init__("list-attrs")

        class DelAttrSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container del-attrs command."""

            def __init__(self):
                """Create a daos container del-attrs command object."""
                super().__init__("del-attrs")
                self.attr = FormattedParameter("--attr={}")

        class GetAttrSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container get-attr command."""

            def __init__(self):
                """Create a daos container get-attr command object."""
                super().__init__("get-attr")
                self.attr = FormattedParameter("--attr={}")

        class SetAttrSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container set-attr command."""

            def __init__(self):
                """Create a daos container set-attr command object."""
                super().__init__("set-attr")
                self.attr = FormattedParameter("--attr={}")
                self.value = FormattedParameter("--value={}")

        class GetPropSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container get-prop command."""

            def __init__(self):
                """Create a daos container get-prop command object."""
                super().__init__("get-prop")
                self.prop = FormattedParameter("--prop={}")

        class SetPropSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container set-prop command."""

            def __init__(self):
                """Create a daos container set-prop command object."""
                super().__init__("set-prop")
                self.prop = FormattedParameter("--properties={}")

        class SetOwnerSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container set-owner command."""

            def __init__(self):
                """Create a daos container set-owner command object."""
                super().__init__("set-owner")
                self.user = FormattedParameter("--user={}")
                self.group = FormattedParameter("--group={}")

        class CreateSnapSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container create-snap command."""

            def __init__(self):
                """Create a daos container create-snap command object."""
                super().__init__("create-snap")
                self.snap = FormattedParameter("--snap={}")
                self.epc = FormattedParameter("--epc={}")
                self.epcrange = FormattedParameter("--epcrange={}")

        class ListSnapsSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container list-snaps command."""

            def __init__(self):
                """Create a daos container list-snaps command object."""
                super().__init__("list-snaps")

        class DestroySnapSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container destroy-snap command."""

            def __init__(self):
                """Create a daos container destroy-snap command object."""
                super().__init__("destroy-snap")
                self.snap = FormattedParameter("--snap={}")
                self.epc = FormattedParameter("--epc={}")
                self.epcrange = FormattedParameter("--epcrange={}")

        class RollbackSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container rollback command."""

            def __init__(self):
                """Create a daos container rollback command object."""
                super().__init__("rollback")
                self.snap = FormattedParameter("--snap={}")
                self.epc = FormattedParameter("--epc={}")

    class ObjectSubCommand(CommandWithSubCommand):
        """Defines an object for the daos object sub command."""

        def __init__(self):
            """Create a daos object subcommand object."""
            super().__init__("/run/daos/object/*", "object")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg network sub command object."""
            if self.sub_command.value == "query":
                self.sub_command_class = self.QuerySubCommand()
            elif self.sub_command.value == "list-keys":
                self.sub_command_class = self.ListKeysSubCommand()
            elif self.sub_command.value == "dump":
                self.sub_command_class = self.DumpSubCommand()
            else:
                self.sub_command_class = None

        class CommonObjectSubCommand(CommandWithParameters):
            """Defines an object for the common daos object sub-command."""

            def __init__(self, sub_command):
                """Create a common daos object sub-command object.

                Args:
                    sub_command (str): sub-command name
                """
                super().__init__(
                        "/run/daos/object/{}/*".format(sub_command),
                        sub_command)
                self.pool = FormattedParameter("--pool={}")
                self.sys_name = FormattedParameter("--sys-name={}")
                self.cont = FormattedParameter("--cont={}")
                self.oid = FormattedParameter("--oid={}")

        class QuerySubCommand(CommonObjectSubCommand):
            """Defines an object for the daos object query command."""

            def __init__(self):
                """Create a daos object query command object."""
                super().__init__("query")

        class ListKeysSubCommand(CommonObjectSubCommand):
            """Defines an object for the daos object list-keys command."""

            def __init__(self):
                """Create a daos object list-keys command object."""
                super().__init__("list-keys")

        class DumpSubCommand(CommonObjectSubCommand):
            """Defines an object for the daos object dump command."""

            def __init__(self):
                """Create a daos object dump command object."""
                super().__init__("dump")

    class FilesystemSubCommand(CommandWithSubCommand):
        """Defines an object for the daos filesystem sub command."""

        def __init__(self):
            """Create a daos filesystem subcommand object."""
            super().__init__("/run/daos/filesystem/*", "filesystem")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the daos filesystem sub command object."""
            if self.sub_command.value == "copy":
                self.sub_command_class = self.CopySubCommand()
            else:
                self.sub_command_class = None

        class CommonFilesystemSubCommand(CommandWithParameters):
            """Defines an object for the common daos filesystem sub-command."""

            def __init__(self, sub_command):
                """Create a common daos filesystem sub-command object.

                Args:
                    sub_command (str): sub-command name
                """
                super().__init__(
                    "/run/daos/filesystem/{}/*".format(
                        sub_command), sub_command)

        class CopySubCommand(CommonFilesystemSubCommand):
            """Defines an object for the daos filesystem copy command."""

            def __init__(self):
                """Create a daos filesystem copy command object."""
                super().__init__("copy")
                #   --src=<type>:<pool/cont | path>
                #   supported types are daos, posix
                self.src = FormattedParameter("--src={}")
                #   --src=<type>:<pool/cont | path>
                #   supported types are daos, posix
                self.dst = FormattedParameter("--dst={}")
