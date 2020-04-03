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
from command_utils import \
    CommandWithParameters, FormattedParameter, CommandFailure, \
    CommandWithSubCommand


class DaosCommand(CommandWithSubCommand):
    """Defines a object representing a daos command."""

    def __init__(self, path):
        """Create a daos Command object.

        Args:
            path (str): path to the daos command
        """
        super(DaosCommand, self).__init__("/run/daos/*", "daos", path)

    def get_sub_command_class(self):
        # pylint: disable=redefined-variable-type
        """Get the dmg sub command object based upon the sub-command."""
        if self.sub_command.value == "pool":
            self.sub_command_class = self.PoolSubCommand()
        elif self.sub_command.value == "container":
            self.sub_command_class = self.ContainerSubCommand()
        elif self.sub_command.value == "object":
            self.sub_command_class = self.ObjectSubCommand()
        else:
            self.sub_command_class = None

    class PoolSubCommand(CommandWithSubCommand):
        """Defines an object for the daos pool sub command."""

        def __init__(self):
            """Create a daos pool subcommand object."""
            super(DaosCommand.PoolSubCommand, self).__init__(
                "/run/daos/pool/*", "pool")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg network sub command object."""
            if self.sub_command.value == "list-containers":
                self.sub_command_class = self.ListContainersSubCommand()
            elif self.sub_command.value == "query":
                self.sub_command_class = self.QuerySubCommand()
            elif self.sub_command.value == "stat":
                self.sub_command_class = self.StatSubCommand()
            elif self.sub_command.value == "list-attrs":
                self.sub_command_class = self.ListAttrsSubCommand()
            elif self.sub_command.value == "get-attr":
                self.sub_command_class = self.GetAttrSubCommand()
            else:
                self.sub_command_class = None

        class CommonPoolSubCommand(CommandWithParameters):
            """Defines an object for the common daos pool sub-command."""

            def __init__(self, sub_command):
                """Create a common daos pool sub-command object.

                Args:
                    sub_command (str): sub-command name
                """
                super(
                    DaosCommand.PoolSubCommand.CommonPoolSubCommand,
                    self).__init__(
                        "/run/daos/pool/{}/*".format(sub_command), sub_command)
                self.pool = FormattedParameter("--pool={}")
                self.sys_name = FormattedParameter("--sys-name={}")
                self.svc = FormattedParameter("--svc={}")
                self.sys = FormattedParameter("--sys={}")

        class ListContainersSubCommand(CommonPoolSubCommand):
            """Defines an object for the daos pool list-containers command."""

            def __init__(self):
                """Create a daos pool list-containers command object."""
                super(
                    DaosCommand.PoolSubCommand.ListContainersSubCommand,
                    self).__init__("list-containers")

        class QuerySubCommand(CommonPoolSubCommand):
            """Defines an object for the daos pool query command."""

            def __init__(self):
                """Create a daos pool query command object."""
                super(
                    DaosCommand.PoolSubCommand.QuerySubCommand, self).__init__(
                        "query")

        class StatSubCommand(CommonPoolSubCommand):
            """Defines an object for the daos pool stat command."""

            def __init__(self):
                """Create a daos pool stat command object."""
                super(
                    DaosCommand.PoolSubCommand.StatSubCommand, self).__init__(
                        "stat")

        class ListAttrsSubCommand(CommonPoolSubCommand):
            """Defines an object for the daos pool list-attr command."""

            def __init__(self):
                """Create a daos pool list-attr command object."""
                super(
                    DaosCommand.PoolSubCommand.ListAttrsSubCommand,
                    self).__init__("list-attrs")

        class GetAttrSubCommand(CommonPoolSubCommand):
            """Defines an object for the daos pool get-attr command."""

            def __init__(self):
                """Create a daos pool get-attr command object."""
                super(
                    DaosCommand.PoolSubCommand.GetAttrSubCommand,
                    self).__init__("get-attr")
                self.attr = FormattedParameter("--attr={}")

    class ContainerSubCommand(CommandWithSubCommand):
        """Defines an object for the daos container sub command."""

        def __init__(self):
            """Create a daos container subcommand object."""
            super(DaosCommand.ContainerSubCommand, self).__init__(
                "/run/daos/container/*", "container")

        def get_sub_command_class(self):
            # pylint: disable=redefined-variable-type
            """Get the dmg network sub command object."""
            if self.sub_command.value == "create":
                self.sub_command_class = self.CreateSubCommand()
            elif self.sub_command.value == "destroy":
                self.sub_command_class = self.DestroySubCommand()
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
                super(
                    DaosCommand.ContainerSubCommand.CommonContainerSubCommand,
                    self).__init__(
                        "/run/daos/container/{}/*".format(sub_command),
                        sub_command)
                self.pool = FormattedParameter("--pool={}")
                self.sys_name = FormattedParameter("--sys-name={}")
                self.svc = FormattedParameter("--svc={}")
                self.cont = FormattedParameter("--cont={}")
                self.path = FormattedParameter("--path={}")

        class CreateSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container create command."""

            def __init__(self):
                """Create a daos container create command object."""
                super(
                    DaosCommand.ContainerSubCommand.CreateSubCommand,
                    self).__init__("create")
                self.type = FormattedParameter("--type={}")
                self.oclass = FormattedParameter("--oclass={}")
                self.chunk_size = FormattedParameter("--chunk_size={}")

        class DestroySubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container destroy command."""

            def __init__(self):
                """Create a daos container destroy command object."""
                super(
                    DaosCommand.ContainerSubCommand.DestroySubCommand,
                    self).__init__("destroy")
                self.force = FormattedParameter("--force", False)

        class ListObjectsSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container list-objects command."""

            def __init__(self):
                """Create a daos container list-objects command object."""
                super(
                    DaosCommand.ContainerSubCommand.ListObjectsSubCommand,
                    self).__init__("list-objects")

        class QuerySubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container query command."""

            def __init__(self):
                """Create a daos container query command object."""
                super(
                    DaosCommand.ContainerSubCommand.QuerySubCommand,
                    self).__init__("query")

        class GetAclSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container get-acl command."""

            def __init__(self):
                """Create a daos container get-acl command object."""
                super(
                    DaosCommand.ContainerSubCommand.GetAclSubCommand,
                    self).__init__("get-acl")
                self.outfile = FormattedParameter("--outfile={}")
                self.verbose = FormattedParameter("--verbose", False)

        class OverwriteAclSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container overwrite-acl cmd."""

            def __init__(self):
                """Create a daos container overwrite-acl command object."""
                super(
                    DaosCommand.ContainerSubCommand.OverwriteAclSubCommand,
                    self).__init__("overwrite-acl")
                self.acl_file = FormattedParameter("--acl-file={}")

        class UpdateAclSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container update-acl command."""

            def __init__(self):
                """Create a daos container update-acl command object."""
                super(
                    DaosCommand.ContainerSubCommand.UpdateAclSubCommand,
                    self).__init__("update-acl")
                self.acl_file = FormattedParameter("--acl-file={}")
                self.entry = FormattedParameter("--entry={}")

        class DeleteAclSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container delete-acl command."""

            def __init__(self):
                """Create a daos container delete-acl command object."""
                super(
                    DaosCommand.ContainerSubCommand.DeleteAclSubCommand,
                    self).__init__("delete-acl")
                self.principal = FormattedParameter("--principal={}")

        class StatSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container stat command."""

            def __init__(self):
                """Create a daos container stat command object."""
                super(
                    DaosCommand.ContainerSubCommand.StatSubCommand,
                    self).__init__("stat")

        class ListAttrsSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container list-attrs command."""

            def __init__(self):
                """Create a daos container list-attrs command object."""
                super(
                    DaosCommand.ContainerSubCommand.ListAttrsSubCommand,
                    self).__init__("list-attrs")

        class DelAttrSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container del-attrs command."""

            def __init__(self):
                """Create a daos container del-attrs command object."""
                super(
                    DaosCommand.ContainerSubCommand.DelAttrSubCommand,
                    self).__init__("del-attrs")
                self.attr = FormattedParameter("--attr={}")

        class GetAttrSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container get-attr command."""

            def __init__(self):
                """Create a daos container get-attr command object."""
                super(
                    DaosCommand.ContainerSubCommand.GetAttrSubCommand,
                    self).__init__("get-attr")
                self.attr = FormattedParameter("--attr={}")

        class SetAttrSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container set-attr command."""

            def __init__(self):
                """Create a daos container set-attr command object."""
                super(
                    DaosCommand.ContainerSubCommand.SetAttrSubCommand,
                    self).__init__("set-attr")
                self.attr = FormattedParameter("--attr={}")
                self.value = FormattedParameter("--value={}")

        class CreateSnapSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container create-snap command."""

            def __init__(self):
                """Create a daos container create-snap command object."""
                super(
                    DaosCommand.ContainerSubCommand.CreateSnapSubCommand,
                    self).__init__("create-snap")
                self.snap = FormattedParameter("--snap={}")

        class ListSnapsSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container list-snaps command."""

            def __init__(self):
                """Create a daos container list-snaps command object."""
                super(
                    DaosCommand.ContainerSubCommand.ListSnapsSubCommand,
                    self).__init__("list-snaps")

        class DestroySnapSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container destroy-snap command."""

            def __init__(self):
                """Create a daos container destroy-snap command object."""
                super(
                    DaosCommand.ContainerSubCommand.DestroySnapSubCommand,
                    self).__init__("destroy-snap")
                self.snap = FormattedParameter("--snap={}")
                self.epc = FormattedParameter("--epc={}")
                self.eprange = FormattedParameter("--eprange={}")

        class RollbackSubCommand(CommonContainerSubCommand):
            """Defines an object for the daos container rollback command."""

            def __init__(self):
                """Create a daos container rollback command object."""
                super(
                    DaosCommand.ContainerSubCommand.RollbackSubCommand,
                    self).__init__("rollback")
                self.snap = FormattedParameter("--snap={}")
                self.epc = FormattedParameter("--epc={}")

    class ObjectSubCommand(CommandWithSubCommand):
        """Defines an object for the daos object sub command."""

        def __init__(self):
            """Create a daos object subcommand object."""
            super(DaosCommand.ObjectSubCommand, self).__init__(
                "/run/daos/object/*", "object")

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
                super(
                    DaosCommand.ObjectSubCommand.CommonObjectSubCommand,
                    self).__init__(
                        "/run/daos/object/{}/*".format(sub_command),
                        sub_command)
                self.pool = FormattedParameter("--pool={}")
                self.sys_name = FormattedParameter("--sys-name={}")
                self.svc = FormattedParameter("--svc={}")
                self.cont = FormattedParameter("--cont={}")
                self.oid = FormattedParameter("--oid={}")

        class QuerySubCommand(CommonObjectSubCommand):
            """Defines an object for the daos object query command."""

            def __init__(self):
                """Create a daos object query command object."""
                super(
                    DaosCommand.ObjectSubCommand.QuerySubCommand,
                    self).__init__("query")

        class ListKeysSubCommand(CommonObjectSubCommand):
            """Defines an object for the daos object list-keys command."""

            def __init__(self):
                """Create a daos object list-keys command object."""
                super(
                    DaosCommand.ObjectSubCommand.ListKeysSubCommand,
                    self).__init__("list-keys")

        class DumpSubCommand(CommonObjectSubCommand):
            """Defines an object for the daos object dump command."""

            def __init__(self):
                """Create a daos object dump command object."""
                super(
                    DaosCommand.ObjectSubCommand.DumpSubCommand,
                    self).__init__("dump")

    def _get_result(self):
        """Get the result from running the configured daos command.

        Returns:
            CmdResult: an avocado CmdResult object containing the daos command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the daos command fails.

        """
        result = None
        try:
            result = self.run()
        except CommandFailure as error:
            raise CommandFailure("<daos> command failed: {}".format(error))

        return result

    def pool_query(self, pool, sys_name=None, svc=None, sys=None, env=None):
        """Query a pool.

        Args:
            pool ([type]): [description]
            sys_name ([type], optional): [description]. Defaults to None.
            svc ([type], optional): [description]. Defaults to None.
            sys ([type], optional): [description]. Defaults to None.
            env (dict, optional): dictionary of environment variable names and
                values (EnvironmentVariables). Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the doas pool query command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("query")
        self.sub_command_class.sub_command_class.pool.value = pool
        self.sub_command_class.sub_command_class.sys_name.value = sys_name
        self.sub_command_class.sub_command_class.svc.value = svc
        self.sub_command_class.sub_command_class.sys.value = sys
        self.env = env
        return self._get_result()

    def container_create(self, pool, sys_name=None, svc=None, cont=None,
                         path=None, cont_type=None, oclass=None,
                         chunk_size=None, env=None):
        """Create a container.

        Args:
            pool (str): UUID of the pool in which to create the container
            sys_name (str, optional): [description]. Defaults to None.
            svc (str, optional): the pool service replicas, e.g. '1,2,3'.
                Defaults to None.
            cont (str, optional): [description]. Defaults to None.
            path (str, optional): [description]. Defaults to None.
            cont_type (str, optional): the type of container to create. Defaults
                to None.
            oclass (str, optional): object class. Defaults to None.
            chunk_size ([type], optional): [description]. Defaults to None.
            env (dict, optional): dictionary of environment variable names and
                values (EnvironmentVariables). Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the doas container create command fails.

        """
        self.set_sub_command("container")
        self.sub_command_class.set_sub_command("create")
        self.sub_command_class.sub_command_class.pool.value = pool
        self.sub_command_class.sub_command_class.sys_name.value = sys_name
        self.sub_command_class.sub_command_class.svc.value = svc
        self.sub_command_class.sub_command_class.cont.value = cont
        self.sub_command_class.sub_command_class.path.value = path
        self.sub_command_class.sub_command_class.type.value = cont_type
        self.sub_command_class.sub_command_class.oclass.value = oclass
        self.sub_command_class.sub_command_class.chunk_size.value = chunk_size
        self.env = env
        return self._get_result()
