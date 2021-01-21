#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase
from os.path import join, sep
from apricot import skipForTicket


class CopyBasicsTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for basic Datamover validation.

    Test Class Description:
        Tests basic functionality of the datamover utility.
        Tests the following cases:
            Copying between UUIDs, UNS paths, and external POSIX systems.
            Copying between pools.
    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(CopyBasicsTest, self).setUp()

        # Get the parameters
        self.flags_write = self.params.get(
            "flags_write", "/run/ior/copy_basics/*")
        self.flags_read = self.params.get(
            "flags_read", "/run/ior/copy_basics/*")
        self.test_file = self.params.get(
            "test_file", "/run/ior/copy_basics/*")

        # Setup the directory structures
        self.posix_test_paths.append(join(self.workdir, "posix_test") + sep)
        self.posix_test_paths.append(join(self.workdir, "posix_test2") + sep)

        # Create the directories
        cmd = "mkdir -p {}".format(self.get_posix_test_path_string())
        self.execute_cmd(cmd)

    @skipForTicket("DAOS-6484")
    def test_copy_types(self):
        """
        Test Description:
            DAOS-5508: Verify copy between POSIX, UUIDs, and UNS paths
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
        :avocado: tags=all,datamover,daily_regression
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
        posix1 = [self.posix_test_paths[0], None, None]

        # Create each destination location
        p1_c2 = ["/", pool1, container2]
        p2_c3 = ["/", pool2, container3]
        posix2 = [self.posix_test_paths[1], None, None]

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
        for (test_desc, src, dst) in copy_list:
            self.set_src_location(*src)
            self.set_dst_location(*dst)
            self.run_datamover(test_desc=test_desc)
            self.read_verify_location(*dst)

    def test_copy_auto_create_dest(self):
        """
        Test Description:
            DAOS-5653: Verify auto-creation of destination container
        Use Cases:
            Create pool1 and pool2.
            Create POSIX cont1 in pool1.
            Create a single 1K file in cont1 using ior.
            Copy all data from cont1 to pool1, with a new cont UUID.
            Copy all data from cont1 to pool2, with a new cont UUID.
        :avocado: tags=all,datamover,daily_regression
        :avocado: tags=copy_basics,copy_auto_create_dest
        """
        # Create pools and src container
        pool1 = self.create_pool()
        pool2 = self.create_pool()
        container1 = self.create_cont(pool1)

        # Create the source file
        self.write_location("DAOS_UUID", "/", pool1, container1)

        # Set the source location
        self.set_src_location("DAOS_UUID", "/", pool1, container1)

        # pool1 -> pool1
        new_uuid = self.gen_uuid()
        self.set_dst_location("DAOS_UUID", "/", pool1, new_uuid)
        self.run_datamover(test_desc="copy_auto_create_dest (same pool)")
        self.read_verify_location("DAOS_UUID", "/", pool1, new_uuid)

        # pool1 -> pool2
        new_uuid = self.gen_uuid()
        self.set_dst_location("DAOS_UUID", "/", pool2, new_uuid)
        self.run_datamover(test_desc="copy_auto_create_dest (different pool)")
        self.read_verify_location("DAOS_UUID", "/", pool2, new_uuid)

    @skipForTicket("DAOS-5512")
    def test_copy_subsets(self):
        """
        Test Description:
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
        :avocado: tags=all,datamover,daily_regression
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
        sub_dir = self.gen_daos_path()
        sub_sub_dir = self.gen_daos_path(prefix=sub_dir)
        cmd = "mkdir -p '{}'".format(
            container1.path.value + sub_sub_dir)
        self.execute_cmd(cmd)

        # Create initial test files
        self.write_location("DAOS_UUID", sub_dir, pool1, container1)
        self.write_location("DAOS_UUID", sub_sub_dir, pool1, container1)

        copy_list = []

        # For each copy, use a new destination directory.
        # This ensures that the source directory is copied
        # *to* the destination, instead of *into* it.
        sub_dir2 = self.gen_daos_path()
        copy_list.append([
            "copy_subsets (uuid sub_dir to uuid sub_dir)",
            ["DAOS_UUID", sub_dir, pool1, container1],
            ["DAOS_UUID", sub_dir2, pool1, container1]])

        sub_sub_dir2 = self.gen_daos_path(prefix=sub_dir2)
        copy_list.append([
            "copy_subsets (uuid sub_sub_dir to uuid sub_sub_dir)",
            ["DAOS_UUID", sub_sub_dir, pool1, container1],
            ["DAOS_UUID", sub_sub_dir2, pool1, container1]])

        sub_dir2 = self.gen_daos_path()
        copy_list.append([
            "copy_subsets (uuid sub_dir to uns sub_dir)",
            ["DAOS_UUID", sub_dir, pool1, container1],
            ["DAOS_UNS", sub_dir2, pool1, container1]])

        sub_sub_dir2 = self.gen_daos_path(prefix=sub_dir2)
        copy_list.append([
            "copy_subsets (uuid sub_dir to uns sub_sub_dir)",
            ["DAOS_UUID", sub_dir, pool1, container1],
            ["DAOS_UNS", sub_sub_dir2, pool1, container1]])

        sub_dir2 = self.gen_daos_path()
        copy_list.append([
            "copy_subsets (uns sub_dir to uuid sub_dir)",
            ["DAOS_UNS", sub_dir, pool1, container1],
            ["DAOS_UUID", sub_dir2, pool1, container1]])

        sub_sub_dir2 = self.gen_daos_path(prefix=sub_dir2)
        copy_list.append([
            "copy_subsets (uns sub_sub_dir to uuid sub_dir)",
            ["DAOS_UNS", sub_sub_dir, pool1, container1],
            ["DAOS_UUID", sub_dir2, pool1, container1]])

        # Run and verify each copy.
        # Each src or dst is a list of params:
        #   [param_type, path, pool, cont]
        for (test_desc, src, dst) in copy_list:
            self.set_src_location(*src)
            self.set_dst_location(*dst)
            self.run_datamover(test_desc=test_desc)
            self.read_verify_location(*dst)

    def write_location(self, param_type, path, pool=None, cont=None):
        """Write the test data using ior."""
        self.set_ior_location_and_run(param_type, path, pool, cont,
                                      self.test_file, self.flags_write)

    def read_verify_location(self, param_type, path, pool=None, cont=None):
        """Read and verify the test data using ior."""
        self.set_ior_location_and_run(param_type, path, pool, cont,
                                      self.test_file, self.flags_read)
