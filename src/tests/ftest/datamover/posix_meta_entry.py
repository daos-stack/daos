#!/usr/bin/python
'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase
from os.path import join


class DmvrPosixMetaEntry(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for POSIX DataMover entry metadata validation.

    Test Class Description:
        Tests metadata preservation on POSIX entries.
        I.e. files, directories, symlinks.
    :avocado: recursive
    """

    def test_dm_posix_meta_entry_dcp(self):
        """JIRA id: DAOS-6390
        Test Description:
            Verifies that POSIX metadata is preserved for dcp.
        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_dcp,dfuse
        :avocado: tags=dm_posix_meta_entry,dm_posix_meta_entry_dcp
        :avocado: tags=test_dm_posix_meta_entry_dcp
        """
        self.run_dm_posix_meta_entry("DCP")

    def run_dm_posix_meta_entry(self, tool):
        """
        Use Cases:
            Create pool1.
            Create cont1 and cont2 in pool1.
            Create a source directory in cont1 that contains:
                1 directory, 1 file, 1 symlink.
                xattrs on the directory and file.
            Create a similar source directory in an external POSIX file system.
            Copy the DAOS source to another DAOS directory.
            Copy the DAOS source to an external POSIX file system.
            Copy the POSIX source to another DAOS directory.
            For each case, verify that permissions and owners are preserved.
            Repeat each case, but with the --preserve flag.
            For each case, verify that xattrs and timestamps are preserved.

        Args:
            tool (str): The DataMover tool to run the test with.
                Must be a valid tool in self.TOOLS.
        """
        # Set the tool to use
        self.set_tool(tool)

        # Get preserve level
        preserve_on = self.params.get("preserve", "/run/{}/*".format(self.tool.lower()))

        test_desc = self.test_id + " (preserve={})".format(str(preserve_on))

        # Start dfuse to hold all pools/containers
        self.start_dfuse(self.dfuse_hosts)

        # Create 1 pool
        pool1 = self.create_pool()

        # Create 1 source container with test data
        cont1 = self.create_cont(pool1)
        daos_src_path = self.new_daos_test_path(False)
        dfuse_src_path = "{}/{}/{}{}".format(
            self.dfuse.mount_dir.value, pool1.uuid, cont1.uuid, daos_src_path)
        self.create_data(dfuse_src_path)

        # Create 1 source posix path with test data
        posix_src_path = self.new_posix_test_path(parent=self.workdir)
        self.create_data(posix_src_path)

        # Run each variation with and without the --preserve option
        # For each case, create a new destination directory.
        # For DAOS, cont1 is used as the source and destination.
        # DAOS -> DAOS
        daos_dst_path = self.new_daos_test_path(False)
        dfuse_dst_path = "{}/{}/{}{}".format(
            self.dfuse.mount_dir.value, pool1.uuid, cont1.uuid, daos_dst_path)
        self.run_datamover(
            test_desc + "(DAOS->DAOS)",
            "DAOS", daos_src_path, pool1, cont1,
            "DAOS", daos_dst_path, pool1, cont1)
        self.compare_data(
            dfuse_src_path, dfuse_dst_path,
            cmp_times=preserve_on, cmp_xattr=preserve_on)

        # DAOS -> POSIX
        posix_dst_path = self.new_posix_test_path(create=False, parent=self.workdir)
        self.run_datamover(
            test_desc + "(DAOS->POSIX)",
            "DAOS", daos_src_path, pool1, cont1,
            "POSIX", posix_dst_path)
        self.compare_data(
            dfuse_src_path, posix_dst_path,
            cmp_times=preserve_on, cmp_xattr=preserve_on)

        # POSIX -> DAOS
        daos_dst_path = self.new_daos_test_path(False)
        dfuse_dst_path = "{}/{}/{}{}".format(
            self.dfuse.mount_dir.value, pool1.uuid, cont1.uuid, daos_dst_path)
        self.run_datamover(
            test_desc + "(POSIX->DAOS)",
            "POSIX", posix_src_path, None, None,
            "DAOS", daos_dst_path, pool1, cont1)
        self.compare_data(
            posix_src_path, dfuse_dst_path,
            cmp_times=preserve_on, cmp_xattr=preserve_on)

    def create_data(self, path):
        """Create the test data.

        Args:
            path (str): Where to create the data.
        """
        cmd_list = [
            # One directory
            "mkdir -p '{}'".format(join(path, "dir1")),
            "pushd '{}'".format(path),

            # xattrs for the directory
            "setfattr -n 'user.dir1_attr1' -v 'dir1_value1' 'dir1'",
            "setfattr -n 'user.dir1_attr2' -v 'dir1_value2' 'dir1'",

            # One file in the directory
            "echo 'test_data' > 'dir1/file1'",

            # xattrs for the file
            "setfattr -n 'user.file1_attr1' -v 'file1_value1' 'dir1/file1'",
            "setfattr -n 'user.file1_attr2' -v 'file1_value2' 'dir1/file1'",

            # One symlink in the directory
            "ln -s 'file1' 'dir1/link1'",

            "popd"
        ]
        self.execute_cmd_list(cmd_list)

    def compare_data(self, path1, path2, cmp_filetype=True,
                     cmp_perms=True, cmp_owner=True, cmp_times=False,
                     cmp_xattr=False):
        """Compare the test data.

        Args:
            path1 (str): The left-hand side to compare.
            path2 (str): The right-hand side to compare.
            cmp_filetype (bool, optional): Whether to compare the file-type.
                Default is True.
            cmp_perms (bool, optional): Whether to compare the permissions.
                Default is True.
            cmp_owner (bool, optional): Whether to compare the user and group
                ownership. Default is True.
            cmp_times (bool, optional): Whether to compare mtime.
                Default is False.
            cmp_xattr (bool, optional): Whether to compare xattrs.
                Default is False.
        """
        self.log.info("compare_data('%s', '%s')", path1, path2)

        # Generate the fields to compare
        field_printf = ""
        if cmp_filetype:
            field_printf += "File Type: %F\\n"
        if cmp_perms:
            field_printf += "Permissions: %A\\n"
        if cmp_owner:
            field_printf += "Group Name: %G\\n"
            field_printf += "User Name: %U\\n"
        if cmp_times:
            field_printf += "mtime: %Y\\n"

        # Diff the fields for each entry
        for entry in ["dir1", "dir1/file1", "dir1/link1"]:
            entry1 = join(path1, entry)
            entry2 = join(path2, entry)
            if field_printf:
                # Use stat to get perms, etc.
                stat_cmd1 = "stat --printf '{}' '{}'".format(
                    field_printf, entry1)
                stat_cmd2 = "stat --printf '{}' '{}'".format(
                    field_printf, entry2)
                diff_cmd = "diff <({} 2>&1) <({} 2>&1)".format(
                    stat_cmd1, stat_cmd2)
                self.execute_cmd(diff_cmd)
            if cmp_xattr:
                # Use getfattr to get the xattrs
                xattr_cmd1 = "getfattr -d -h '{}'".format(entry1)
                xattr_cmd2 = "getfattr -d -h '{}'".format(entry2)
                diff_cmd = "diff -I '^#' <({} 2>&1) <({} 2>&1)".format(
                    xattr_cmd1, xattr_cmd2)
                self.execute_cmd(diff_cmd)

    def execute_cmd_list(self, cmd_list):
        """Execute a list of commands, separated by &&.

        Args:
            cmd_list (list): A list of commands to execute.
        """
        cmd = " &&\n".join(cmd_list)
        self.execute_cmd(cmd)
