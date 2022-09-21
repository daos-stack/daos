#!/usr/bin/python
'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase
from os.path import join
import re

class DmvrPosixSubsets(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """POSIX Data Mover validation for container subsets using
       "dcp" and "daos filesystem copy" with POSIX containers.

    Test Class Description:
        Tests the following cases:
            Copying POSIX container subsets.
    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        # Get the parameters
        self.ior_flags = self.params.get(
            "ior_flags", "/run/ior/*")
        self.test_file = self.ior_cmd.test_file.value

    def run_dm_posix_subsets(self, tool):
        """
        Test Description:
            Tests copying POSIX container subsets.
        Use Cases:
            Create pool1.
            Create POSIX cont1 in pool1.
            Create a directory structure of depth 2 in the container.
            Create a single file of size 1K at depth 1 and depth 2 using ior.
            Copy depth 1 to a new directory of depth 1, using UUIDs.
            Copy depth 1 to a new directory of depth 2, using UUIDs.
            Copy depth 2 to a new directory of depth 1, using UUIDS.
            Copy depth 2 to a new directory of depth 2, using UUIDS.
            Repeat, but with UNS paths.
        """
        # Set the tool to use
        self.set_tool(tool)

        # Start dfuse to hold all pools/containers
        self.start_dfuse(self.dfuse_hosts)

        # Create 1 pool
        pool1 = self.create_pool()

        # create dfuse containers to test copying to dfuse subdirectories
        dfuse_cont1 = self.get_container(pool1)
        dfuse_cont2 = self.get_container(pool1)
        dfuse_cont1_dir = join(self.dfuse.mount_dir.value, pool1.uuid, dfuse_cont1.uuid)
        # destination directory should be created by program
        dfuse_cont2_dir = self.new_posix_test_path(create=False,
            parent=join(self.dfuse.mount_dir.value, pool1.uuid, dfuse_cont2.uuid))
        # Create a special container to hold UNS entries
        uns_cont = self.get_container(pool1)

        # Create a testing container
        container1_path = join(self.dfuse.mount_dir.value, pool1.uuid, uns_cont.uuid, 'uns1')
        container1 = self.get_container(pool1, path=container1_path)

        # Create some source directories in the container
        sub_dir = self.new_daos_test_path(False)
        sub_sub_dir = self.new_daos_test_path(True, container1, sub_dir)

        # Create initial test files
        self.write_location("DAOS_UUID", sub_dir, pool1, container1)
        self.write_location("DAOS_UUID", sub_sub_dir, pool1, container1)
        self.write_location("POSIX", dfuse_cont1_dir)

        copy_list = []

        if self.tool == "FS_COPY":
            copy_list.append(["dfuse copy (dfuse cont1 dir to dfuse cont2 dir that doesn't exist)",
                ["POSIX", dfuse_cont1_dir, None, None],
                ["POSIX", dfuse_cont2_dir, None, None]])

        # For each copy, use a new destination directory.
        # This ensures that the source directory is copied
        # *to* the destination, instead of *into* it.
        sub_dir2 = self.new_daos_test_path(False)
        copy_list.append([
            "copy_subsets (uuid sub_dir to uuid sub_dir)",
            ["DAOS_UUID", sub_dir, pool1, container1],
            ["DAOS_UUID", sub_dir2, pool1, container1]])

        sub_sub_dir2 = self.new_daos_test_path(False, parent=sub_dir2)
        copy_list.append([
            "copy_subsets (uuid sub_sub_dir to uuid sub_sub_dir)",
            ["DAOS_UUID", sub_sub_dir, pool1, container1],
            ["DAOS_UUID", sub_sub_dir2, pool1, container1]])

        # FS_COPY does not yet support UNS subsets
        if self.tool != "FS_COPY":
            sub_dir2 = self.new_daos_test_path(False)
            copy_list.append([
                "copy_subsets (uuid sub_dir to uns sub_dir)",
                ["DAOS_UUID", sub_dir, pool1, container1],
                ["DAOS_UNS", sub_dir2, pool1, container1]])

            sub_sub_dir2 = self.new_daos_test_path(False, parent=sub_dir2)
            copy_list.append([
                "copy_subsets (uuid sub_dir to uns sub_sub_dir)",
                ["DAOS_UUID", sub_dir, pool1, container1],
                ["DAOS_UNS", sub_sub_dir2, pool1, container1]])

            sub_dir2 = self.new_daos_test_path(False)
            copy_list.append([
                "copy_subsets (uns sub_dir to uuid sub_dir)",
                ["DAOS_UNS", sub_dir, pool1, container1],
                ["DAOS_UUID", sub_dir2, pool1, container1]])

            sub_sub_dir2 = self.new_daos_test_path(False, parent=sub_dir2)
            copy_list.append([
                "copy_subsets (uns sub_sub_dir to uuid sub_sub_dir)",
                ["DAOS_UNS", sub_sub_dir, pool1, container1],
                ["DAOS_UUID", sub_sub_dir2, pool1, container1]])

        # Run and verify each copy.
        # Each src or dst is a list of params:
        #   [param_type, path, pool, cont]
        for (test_desc, src, dst) in copy_list:
            result = self.run_datamover(
                test_desc,
                src[0], src[1], src[2], src[3],
                dst[0], dst[1], dst[2], dst[3])
            self.read_verify_location(*dst)
            if self.tool == "FS_COPY":
                if not re.search(r"Successfully copied to DAOS", result.stdout_text):
                    self.fail("Failed to copy to DAOS")

    def write_location(self, param_type, path, pool=None, cont=None):
        """Write the test data using ior."""
        self.run_ior_with_params(param_type, path, pool, cont,
                                 self.test_file, self.ior_flags[0])

    def read_verify_location(self, param_type, path, pool=None, cont=None):
        """Read and verify the test data using ior."""
        self.run_ior_with_params(param_type, path, pool, cont,
                                 self.test_file, self.ior_flags[1])

    def test_dm_posix_subsets_dcp(self):
        """
        Test Description:
            Tests copying POSIX container subsets with dcp.
            DAOS-5512: Verify ability to copy container subsets
        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_dcp,dfuse,dfs,ior
        :avocado: tags=dm_posix_subsets,dm_posix_subsets_dcp
        :avocado: tags=test_dm_posix_subsets_dcp
        """
        self.run_dm_posix_subsets("DCP")

    def test_dm_posix_subsets_fs_copy(self):
        """
        Test Description:
        Tests copying POSIX container subsets with fs copy.
            DAOS-6752: daos fs copy improvements
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=datamover,daos_fs_copy,dfuse,dfs,ior
        :avocado: tags=dm_posix_subsets,dm_posix_subsets_fs_copy
        :avocado: tags=test_dm_posix_subsets_fs_copy
        """
        self.run_dm_posix_subsets("FS_COPY")
