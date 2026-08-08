"""
  (C) Copyright 2022 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from command_utils_base import BasicParameter, CommandWithParameters, FormattedParameter
from run_utils import run_remote


class DdbCommandBase(CommandWithParameters):
    """Defines the basic structures of ddb command."""

    def __init__(self, server_host, path, verbose=True, timeout=None, sudo=True):
        """Defines the parameters for ddb.

        Args:
            server_host (NodeSet): Server host to run the command.
            path (str): path to the ddb command.
            verbose (bool, optional): Display command output in run.
                Defaults to True.
            timeout (int, optional): Command timeout (sec) used in run. Defaults to
                None.
            sudo (bool, optional): Whether to run ddb with sudo. Defaults to True.
        """
        super().__init__("/run/ddb/*", "ddb", path)

        # We need to run with sudo.
        self.sudo = sudo

        self.host = server_host

        # Write mode that's necessary for the commands that alters the data such as load.
        self.write_mode = FormattedParameter("-w", default=False, position=1)

        # Used for ddb subcommand.
        self.ddb_command = BasicParameter(None, position=2)

        # Path to the system database. Used for MD-on-SSD.
        self.db_path = FormattedParameter("--db_path {}", position=3)

        # VOS file path.
        self.vos_path = FormattedParameter("--vos_path {}", position=4)

        # Path for various ddb subcommands.
        self.path = BasicParameter(None, position=5)

        # Members needed for run().
        self.verbose = verbose
        self.timeout = timeout

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters

        """
        value = super().__str__()
        if self.sudo:
            value = " ".join(["sudo -n", value])
        return value

    def run(self):
        """Run the command.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        return run_remote(
            self.log, self.host, command=str(self), verbose=self.verbose, timeout=self.timeout)


class DdbCommand(DdbCommandBase):
    # pylint: disable=too-many-public-methods
    """ddb command class.

    Component path is needed for most of the commands. They're in the form of:
    [container]/[object]/[dkey]/[akey]

    Each component can be indexed by UUID, but indexing is usually more convenient. e.g.,
    "ls [0]/[1]" means index into the first container, second object, and list the dkeys
    in it. Note that the order we add container, object, dkey/akey may not be consistent
    with the indices, so it's better for tests to use the UUID.
    """

    def __init__(self, server_host, path, vos_path):
        """Constructor that sets the common variables for sub-commands.

        Args:
            server_host (NodeSet): Server host to run the command.
            path (str): Path to the ddb command. Pass in self.bin for our wolf/CI env.
            vos_path (str): VOS file path, e.g. /mnt/daos/<pool_uuid>/vos-0
        """
        super().__init__(server_host, path)
        self.vos_path.update(vos_path, "vos_path")

    def list_component(self, component_path=None):
        """Call ddb ls <component_path>

        ls is similar to the Linux ls command. It lists objects inside the container,
        dkeys inside the object, and so on.

        Args:
            component_path (str): Component that comes after ls. e.g., [0]/[1] for first
                container, second object. Defaults to None, in which case "ls" will be
                called.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.ddb_command.value = "ls"
        self.path.value = component_path
        self.write_mode.value = False

        return self.run()

    def value_dump(self, component_path, out_file_path):
        """Call ddb value_dump <component_path> <out_file_path>

        dump_value writes the contents to the file. e.g., if akey is specified, its data
        will be dumped.

        Args:
            component_path (str): Component that comes after dump_value. e.g.,
                [0]/[1]/[1]/[0] to dump the data of the akey.
            out_file_path (str): Path where the file is saved. Pass in self.test_dir +
                "my_out.txt" unless there's a specific reason. This will create a file in
                /var/tmp/daos_testing/<test_name>/my_out.txt

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = False
        self.ddb_command.value = "value_dump"
        self.path.value = " ".join([component_path, out_file_path])

        return self.run()

    def value_load(self, component_path, load_file_path):
        """Call ddb -w value_load <load_file_path> <component_path>

        load writes the given data into the container. e.g.,
        load new_data.txt [0]/[1]/[1]/[0]
        will write the new_data into the akey.

        Args:
            component_path (str): Component that comes after load. e.g.,
                [0]/[1]/[1]/[0] to write the data into the akey.
            load_file_path (str): Path of the file that contains the data to load.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = True
        self.ddb_command.value = "value_load"
        self.path.value = " ".join([load_file_path, component_path])

        return self.run()

    def remove_component(self, component_path):
        """Call ddb -w rm <component_path>

        Args:
            component_path (str): Component that comes after rm. e.g., [0]/[1] for first container,
                second object.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = True
        self.ddb_command.value = "rm"
        self.path.value = component_path

        return self.run()

    def ilog_dump(self, component_path):
        """Call ddb ilog_dump <component_path>

        Args:
            component_path (str): Component that comes after rm. e.g., [0]/[1]/[1] for
                first container, second object, second dkey. Needs to be object or after.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = False
        self.ddb_command.value = "ilog_dump"
        self.path.value = component_path

        return self.run()

    def ilog_commit(self, component_path):
        """Call ddb ilog_commit <component_path>

        Args:
            component_path (str): Component that comes after rm. e.g., [0]/[1]/[1] for
                first container, second object, second dkey. Needs to be object or after.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = False
        self.ddb_command.value = "ilog_commit"
        self.path.value = component_path

        return self.run()

    def ilog_clear(self, component_path):
        """Call ddb ilog_clear <component_path>

        Args:
            component_path (str): Component that comes after rm. e.g., [0]/[1]/[1] for
                first container, second object, second dkey. Needs to be object or after.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = False
        self.ddb_command.value = "ilog_clear"
        self.path.value = component_path

        return self.run()

    def superblock_dump(self, component_path):
        """Call ddb superblock_dump <component_path>

        Args:
            component_path (str): Component that comes after dump_superblock.
                e.g., [0]/[1]/[1] for first container, second object, second dkey.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = False
        self.ddb_command.value = "superblock_dump"
        self.path.value = component_path

        return self.run()

    def dtx_dump(self, component_path="[0]", committed=False, active=False):
        """Call ddb dtx_dump <component_path>

        committed and active can't be set at the same time.

        Args:
            component_path (str): Component that comes after dump_dtx. It doesn't matter
                as long as it's valid. Defaults to [0].
            committed (str): -c flag. Defaults to False.
            active (str): -a flag. Defaults to False.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = False
        self.ddb_command.value = "dtx_dump"
        commands = []
        if committed:
            commands.append("-c")
        if active:
            commands.append("-a")
        commands.append(component_path)

        self.path.value = " ".join(commands)

        return self.run()

    def dtx_cmt_clear(self, component_path="[0]"):
        """Call ddb -w dtx_cmt_clear <component_path>

        Args:
            component_path (str): Component that comes after clear_cmt_dtx. It doesn't
                matter as long as it's valid. Defaults to [0].

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = True
        self.ddb_command.value = "dtx_cmt_clear"
        self.path.value = component_path

        return self.run()

    def prov_mem(self, db_path, tmpfs_mount):
        """Call ddb --vos_path "" prov_mem <db_path> <tmpfs_mount>.

        Args:
            db_path (str): Path to the system database. e.g.,
                /var/tmp/daos_testing/control_metadata/daos_control/engine0
            tmpfs_mount (str): Path to the tmpfs mount point. Directory that needs to be created
                beforehand. e.g., /mnt/daos_load

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status
        """
        self.vos_path.value = '""'
        cmd = ["prov_mem", db_path, tmpfs_mount]
        self.path.value = " ".join(cmd)

        return self.run()

    def rm_pool(self, db_path, removing_path):
        """Call ddb rm_pool --db_path <db_path> <removing_path>

        Example:
        ddb rm_pool --db_path /var/tmp/daos_testing/control_metadata/daos_control/engine1
        /mnt/daos3/$POOL/rdb-pool

        Args:
            db_path (str): Path to the system database. e.g.,
                /var/tmp/daos_testing/control_metadata/daos_control/engine0
            removing_path (str): File path to remove.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status
        """
        self.vos_path.value = None
        self.ddb_command.value = "rm_pool"
        self.db_path.value = db_path
        self.path.value = removing_path

        return self.run()
