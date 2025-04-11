"""
  (C) Copyright 2022 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

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
        self.write_mode = FormattedParameter("-w", default=False)

        # Command to run on the VOS file that contains container, object info, etc.
        self.single_command = BasicParameter(None, position=2)

        # VOS file path.
        self.vos_path = BasicParameter(None, position=1)

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

    def __init__(self, server_host, path, mount_point, pool_uuid, vos_file):
        """Constructor that sets the common variables for sub-commands.

        Args:
            server_host (NodeSet): Server host to run the command.
            path (str): Path to the ddb command. Pass in self.bin for our wolf/CI env.
            mount_point (str): DAOS mount point where pool directory is created. e.g.,
                /mnt/daos, /mnt/daos0.
            pool_uuid (str): Pool UUID.
            vos_file (str): VOS file name that's located in /mnt/daos/<pool_uuid>. It's
                usually in the form of vos-0, vos-1, and so on.
        """
        super().__init__(server_host, path)

        # Construct the VOS file path where ddb will inject the command.
        self.update_vos_path(mount_point, pool_uuid, vos_file)

    def update_vos_path(self, mount_point, pool_uuid, vos_file):
        """Update the vos_path ddb command argument.

        Args:
            mount_point (str): DAOS mount point where pool directory is created. e.g.,
                /mnt/daos, /mnt/daos0.
            pool_uuid (str): Pool UUID.
            vos_file (str): VOS file name that's located in /mnt/daos/<pool_uuid>. It's
                usually in the form of vos-0, vos-1, and so on.
        """
        vos_path = os.path.join(mount_point, pool_uuid.lower(), vos_file)
        self.vos_path.update(vos_path, "vos_path")

    def list_component(self, component_path=None):
        """Call ddb -R "ls <component_path>"

        ls is similar to the Linux ls command. It lists objects inside the container,
        dkeys inside the object, and so on.

        Args:
            component_path (str): Component that comes after ls. e.g., [0]/[1] for first
                container, second object. Defaults to None, in which case "ls" will be
                called.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        cmd = ["ls"]
        if component_path:
            cmd.append(component_path)
        self.write_mode.value = False
        self.single_command.value = " ".join(cmd)

        return self.run()

    def value_dump(self, component_path, out_file_path):
        """Call ddb -R "value_dump <component_path> <out_file_path>"

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
        self.single_command.value = " ".join(
            ["value_dump", component_path, out_file_path])

        return self.run()

    def value_load(self, component_path, load_file_path):
        """Call ddb -w -R "value_load <load_file_path> <component_path>"

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
        self.single_command.value = " ".join(
            ["value_load", load_file_path, component_path])

        return self.run()

    def remove_component(self, component_path):
        """Call ddb -w -R "rm <component_path>"

        Args:
            component_path (str): Component that comes after rm. e.g., [0]/[1] for first
                container, second object.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = True
        self.single_command.value = " ".join(["rm", component_path])

        return self.run()

    def ilog_dump(self, component_path):
        """Call ddb -R "ilog_dump <component_path>"

        Args:
            component_path (str): Component that comes after rm. e.g., [0]/[1]/[1] for
                first container, second object, second dkey. Needs to be object or after.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = False
        self.single_command.value = " ".join(["ilog_dump", component_path])

        return self.run()

    def ilog_commit(self, component_path):
        """Call ddb -R "ilog_commit <component_path>"

        Args:
            component_path (str): Component that comes after rm. e.g., [0]/[1]/[1] for
                first container, second object, second dkey. Needs to be object or after.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = False
        self.single_command.value = " ".join(["ilog_commit", component_path])

        return self.run()

    def ilog_clear(self, component_path):
        """Call ddb -R "ilog_clear <component_path>"

        Args:
            component_path (str): Component that comes after rm. e.g., [0]/[1]/[1] for
                first container, second object, second dkey. Needs to be object or after.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = False
        self.single_command.value = " ".join(["ilog_clear", component_path])

        return self.run()

    def superblock_dump(self, component_path):
        """Call ddb -R "superblock_dump <component_path>"

        Args:
            component_path (str): Component that comes after dump_superblock.
                e.g., [0]/[1]/[1] for first container, second object, second dkey.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = False
        self.single_command.value = " ".join(["superblock_dump", component_path])

        return self.run()

    def dtx_dump(self, component_path="[0]", committed=False, active=False):
        """Call ddb -R "dtx_dump <component_path>"

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

        commands = ["dtx_dump"]
        if committed:
            commands.append("-c")
        if active:
            commands.append("-a")
        commands.append(component_path)

        self.single_command.value = " ".join(commands)

        return self.run()

    def dtx_cmt_clear(self, component_path="[0]"):
        """Call ddb -R "dtx_cmt_clear <component_path>"

        Args:
            component_path (str): Component that comes after clear_cmt_dtx. It doesn't
                matter as long as it's valid. Defaults to [0].

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status

        """
        self.write_mode.value = True
        self.single_command.value = " ".join(["dtx_cmt_clear", component_path])

        return self.run()
