#!/usr/bin/python
'''
  (C) Copyright 2020 Intel Corporation.

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
'''
from data_mover_test_base import DataMoverTestBase
from os.path import basename, join


class CopyBasicsTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for basic POSIX DataMover validation.

    Test Class Description:
        Tests basic functionality of the POSIX DataMover utilities.
        Tests the following cases:
            Copying between UUIDs, UNS paths, and external POSIX systems.
            Copying between pools.
            Auto-creating destination POSIX containers on copy.
            Copying subsets of POSIX containers.
    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(CopyBasicsTest, self).setUp()

        # Get the parameters
        self.ior_flags = self.params.get(
            "ior_flags", "/run/ior/*")
        self.test_file = self.ior_cmd.test_file.value

    def test_copy_types(self):
        """
        Test Description:
             Tests POSIX copy with different source and destination types.
             DAOS-5508: Verify copy between POSIX, UUIDs, and UNS paths.
             Daos-5511: Verify copy across pools.
        Use Cases:
            Create pool1 and pool2.
            Create POSIX type cont1 and cont2 in pool1 with UNS paths.
            Create POSIX type cont3 in pool2 with a UNS path.
            Create a single 1K file in cont1 using ior.
            Copy all data from cont1 (UUIDs) to cont2 (UUIDs).
            Copy all data from cont1 (UUIDs) to cont2 (UNS).
            Copy all data form cont1 (UUIDs) to cont3 (UUIDs).
            Copy all data from cont1 (UUIDs) to cont3 (UNS).
            Copy all data from cont1 (UUIDs) to an external POSIX FS.
            Copy all data from cont1 (UNS) to cont2 (UUIDs).
            Copy all data from cont1 (UNS) to cont2 (UNS).
            Copy all data from cont1 (UNS) to cont3 (UUIDs).
            Copy all data from cont1 (UNS) to cont3 (UNS).
            Copy all data from cont1 (UNS) to an external POSIX FS.
            Create a single 1K file in the external POSIX using ior.
            Copy all data from POSIX to cont2 (UUIDs).
            Copy all data from POSIX to cont2 (UNS).
            Copy all data from POSIX FS to a different POSIX FS.
            Repeat the copy operations, but specify file -> file.
            Repeat the copy operations, but specify file -> dir.
        :avocado: tags=all,daily_regression
        :avocado: tags=datamover
        :avocado: tags=copy_basics,copy_types
        """
        # Start dfuse to hold all pools/containers
        self.start_dfuse(self.dfuse_hosts)

        # Create 2 pools
        pool1 = self.create_pool()
        pool2 = self.create_pool()

        # Create a special container to hold UNS entries
        uns_cont = self.create_cont(pool1)

        # Create all other containers
        container1 = self.create_cont(pool1, True, pool1, uns_cont)
        container2 = self.create_cont(pool1, True, pool1, uns_cont)
        container3 = self.create_cont(pool2, True, pool1, uns_cont)

        # Create each source location
        p1_c1 = ["/", pool1, container1]
        posix1 = [self.new_posix_test_path(), None, None]

        # Create each destination location
        p1_c2 = ["/", pool1, container2]
        p2_c3 = ["/", pool2, container3]
        posix2 = [self.new_posix_test_path(), None, None]

        # Create the source files
        self.write_location("DAOS_UUID", *p1_c1)
        self.write_location("POSIX", *posix1)

        # Make a list of each test case to run
        # [[test_desc, src, dst]]
        # Each src or dst is a list of params:
        #     [param_type, path, pool, cont]
        # Since we can only have a prefix on *either*
        # the source or destination, each path is specified
        # as a directory, and the test file is appended
        # to this path in write/read_location.
        # This allows UNS -> UNS to be performed.
        copy_list = []

        copy_list.append([
            "UUID -> UUID (same pool)",
            ["DAOS_UUID"] + p1_c1,
            ["DAOS_UUID"] + p1_c2])

        copy_list.append([
            "UUID -> UUID (different pool)",
            ["DAOS_UUID"] + p1_c1,
            ["DAOS_UUID"] + p2_c3])

        copy_list.append([
            "UUID -> UNS (same pool)",
            ["DAOS_UUID"] + p1_c1,
            ["DAOS_UNS"] + p1_c2])

        copy_list.append([
            "UUID -> UNS (different pool)",
            ["DAOS_UUID"] + p1_c1,
            ["DAOS_UNS"] + p2_c3])

        copy_list.append([
            "UUID -> POSIX",
            ["DAOS_UUID"] + p1_c1,
            ["POSIX"] + posix2])

        copy_list.append([
            "UNS -> UUID (same pool)",
            ["DAOS_UNS"] + p1_c1,
            ["DAOS_UUID"] + p1_c2])

        copy_list.append([
            "UNS -> UUID (different pool)",
            ["DAOS_UNS"] + p1_c1,
            ["DAOS_UUID"] + p2_c3])

        copy_list.append([
            "UNS -> UNS (same pool)",
            ["DAOS_UNS"] + p1_c1,
            ["DAOS_UNS"] + p1_c2])

        copy_list.append([
            "UNS -> UNS (different pool)",
            ["DAOS_UNS"] + p1_c1,
            ["DAOS_UNS"] + p2_c3])

        copy_list.append([
            "UNS -> POSIX",
            ["DAOS_UNS"] + p1_c1,
            ["POSIX"] + posix2])

        copy_list.append([
            "POSIX -> UUID",
            ["POSIX"] + posix1,
            ["DAOS_UUID"] + p1_c2])

        copy_list.append([
            "POSIX -> UNS",
            ["POSIX"] + posix1,
            ["DAOS_UNS"] + p1_c2])

        copy_list.append([
            "POSIX -> POSIX",
            ["POSIX"] + posix1,
            ["POSIX"] + posix2])

        # Run and verify each copy.
        # Each src or dst is a list of params:
        #   [param_type, path, pool, cont]
        #   where the path is a directory.
        for (test_desc, src, dst) in copy_list:
            # dir -> dir variation
            self.run_datamover(
                test_desc + " (dir->dir)",
                src[0], src[1], src[2], src[3],
                dst[0], dst[1], dst[2], dst[3])

            # The source directory is created IN the destination
            # so append the directory name to the destination path.
            self.read_verify_location(dst[0], join(dst[1], basename(src[1])),
                                      dst[2], dst[3])

            # file -> file variation
            # A UNS subset is not supported for both src and dst.
            if not (src[0] == "DAOS_UNS" and dst[0] == "DAOS_UNS"):
                self.run_datamover(
                    test_desc + " (file->file)",
                    src[0], join(src[1], self.test_file), src[2], src[3],
                    dst[0], join(dst[1], self.test_file), dst[2], dst[3])
                self.read_verify_location(dst[0], dst[1], dst[2], dst[3])

            # file -> dir variation
            # This works because the destination dir is already created above
            self.run_datamover(
                test_desc + " (file->dir)",
                src[0], join(src[1], self.test_file), src[2], src[3],
                dst[0], dst[1], dst[2], dst[3])
            self.read_verify_location(dst[0], dst[1], dst[2], dst[3])

    def test_copy_subsets(self):
        """
        Test Description:
            Tests copying POSIX container subsets.
            DAOS-5512: Verify ability to copy container subsets
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
        :avocado: tags=all,daily_regression
        :avocado: tags=datamover
        :avocado: tags=copy_basics,copy_subsets
        """
        # Start dfuse to hold all pools/containers
        self.start_dfuse(self.dfuse_hosts)

        # Create 1 pool
        pool1 = self.create_pool()

        # Create a special container to hold UNS entries
        uns_cont = self.create_cont(pool1)

        # Create a testing container
        container1 = self.create_cont(pool1, True, pool1, uns_cont)

        # Create some source directories in the container
        sub_dir = self.new_daos_test_path(False)
        sub_sub_dir = self.new_daos_test_path(True, container1, sub_dir)

        # Create initial test files
        self.write_location("DAOS_UUID", sub_dir, pool1, container1)
        self.write_location("DAOS_UUID", sub_sub_dir, pool1, container1)

        copy_list = []

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
            "copy_subsets (uns sub_sub_dir to uuid sub_dir)",
            ["DAOS_UNS", sub_sub_dir, pool1, container1],
            ["DAOS_UUID", sub_dir2, pool1, container1]])

        # Run and verify each copy.
        # Each src or dst is a list of params:
        #   [param_type, path, pool, cont]
        for (test_desc, src, dst) in copy_list:
            self.run_datamover(
                test_desc,
                src[0], src[1], src[2], src[3],
                dst[0], dst[1], dst[2], dst[3])
            self.read_verify_location(*dst)

    def write_location(self, param_type, path, pool=None, cont=None):
        """Write the test data using ior."""
        self.run_ior_with_params(param_type, path, pool, cont,
                                 self.test_file, self.ior_flags[0])

    def read_verify_location(self, param_type, path, pool=None, cont=None):
        """Read and verify the test data using ior."""
        self.run_ior_with_params(param_type, path, pool, cont,
                                 self.test_file, self.ior_flags[1])
