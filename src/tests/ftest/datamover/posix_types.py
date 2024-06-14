'''
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from os.path import basename, join

from data_mover_test_base import DataMoverTestBase
from dfuse_utils import get_dfuse, start_dfuse
from duns_utils import format_path, parse_path


class DmvrPosixTypesTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """POSIX Data Mover validation for varying source and destination types
       using "dcp", "dsync, and "daos filesystem copy" with POSIX containers.

    Test Class Description:
        Tests the following cases:
            Copying between UUIDs, UNS paths, and external POSIX systems.
            Copying between pools.
    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        # Get the parameters
        self.ior_flags = self.params.get("ior_flags", "/run/ior/*")
        self.test_file = self.ior_cmd.test_file.value

    def run_dm_posix_types(self, tool):
        """
        Test Description:
            Tests POSIX copies with dcp using different src and dst types.
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
        # Set the tool to use
        self.set_tool(tool)

        # Start dfuse to hold all pools/containers
        dfuse = get_dfuse(self, self.dfuse_hosts)
        start_dfuse(self, dfuse)

        # Create 2 pools
        pool1 = self.create_pool(label='pool1')
        pool2 = self.create_pool(label='pool2')

        # Create a special container to hold UNS entries
        uns_cont = self.get_container(pool1)

        # Create all other containers
        container1_path = join(dfuse.mount_dir.value, pool1.uuid, uns_cont.uuid, 'uns1')
        container1 = self.get_container(pool1, path=container1_path, label='container1')
        container2_path = join(dfuse.mount_dir.value, pool1.uuid, uns_cont.uuid, 'uns2')
        container2 = self.get_container(pool1, path=container2_path, label='container2')
        container3_path = join(dfuse.mount_dir.value, pool1.uuid, uns_cont.uuid, 'uns3')
        container3 = self.get_container(pool2, path=container3_path, label='container3')

        # Create each source location
        p1_c1_uuid = format_path(pool1.uuid, container1.uuid, '/')
        p1_c1_label = format_path(pool1.label.value, container1.label.value, '/')
        p1_c1_uns = container1_path + '/'
        posix1 = self.new_posix_test_path()

        # Create each destination location
        p1_c2_uuid = format_path(pool1.uuid, container2.uuid, '/')
        p1_c2_label = format_path(pool1.label.value, container2.label.value, '/')
        p1_c2_uns = container2_path + '/'
        p2_c3_uuid = format_path(pool2.uuid, container3.uuid, '/')
        p2_c3_uns = container3_path + '/'
        posix2 = self.new_posix_test_path()

        # Create the source files
        self.write_location(p1_c1_uuid)
        self.write_location(posix1)

        # Make a list of each test case to run
        # [[test_desc, src, dst]]
        copy_list = [
            ["UUID -> UUID (same pool)", p1_c1_uuid, p1_c2_uuid],
            ["LABEL -> LABEL (same pool)", p1_c1_label, p1_c2_label],
            ["UUID -> UUID (different pool)", p1_c1_uuid, p2_c3_uuid],
            ["UUID -> UNS (same pool)", p1_c1_uuid, p1_c2_uns],
            ["UUID -> UNS (different pool)", p1_c1_uuid, p2_c3_uns],
            ["UUID -> POSIX", p1_c1_uuid, posix2],
            ["UNS -> UUID (same pool)", p1_c1_uns, p1_c2_uuid],
            ["UNS -> UUID (different pool)", p1_c1_uns, p2_c3_uuid],
            ["UNS -> UNS (same pool)", p1_c1_uns, p1_c2_uns],
            ["UNS -> UNS (different pool)", p1_c1_uns, p2_c3_uns],
            ["UNS -> POSIX", p1_c1_uns, posix2],
            ["POSIX -> UUID", posix1, p1_c2_uuid],
            ["POSIX -> UNS", posix1, p1_c2_uuid],
            ["POSIX -> POSIX", posix1, posix2]
        ]

        # Run and verify each copy
        for (test_desc, src, dst) in copy_list:
            # dir -> dir variation
            self.run_datamover(test_desc + " (dir->dir)", src_path=src, dst_path=dst)

            if self.tool == "DSYNC":
                # The source directory is synced TO the destination.
                dst_path = dst
            else:
                # The source directory is created IN the destination
                # so append the directory name to the destination path.
                if 'daos:' in src:
                    _, _, path = parse_path(src)
                    path = path or '/'
                else:
                    path = src
                dst_path = join(dst, basename(path))
            self.read_verify_location(dst_path)

            # file -> file variation
            self.run_datamover(
                test_desc + " (file->file)",
                src_path=join(src, self.test_file),
                dst_path=join(dst, self.test_file))
            self.read_verify_location(dst)

            # file -> dir variation
            # This works because the destination dir is already created above.
            # DSYNC overwrites existing directories with the source file.
            if self.tool != "DSYNC":
                self.run_datamover(
                    test_desc + " (file->dir)",
                    src_path=join(src, self.test_file),
                    dst_path=dst)
                self.read_verify_location(dst)

    def write_location(self, path):
        """Write the test data using ior.

        Args:
            path (str): POSIX or DAOS path to write to.

        """
        if path.startswith('daos:'):
            param_type = 'DAOS'
            pool, cont, path = parse_path(path)
            path = path or '/'
        else:
            param_type = 'POSIX'
            pool = None
            cont = None
        self.run_ior_with_params(param_type, path, pool, cont, self.test_file, self.ior_flags[0])

    def read_verify_location(self, path):
        """Read and verify the test data using ior.

        Args:
            path (str): POSIX or DAOS path to verify.

        """
        if path.startswith('daos:'):
            param_type = 'DAOS'
            pool, cont, path = parse_path(path)
            path = path or '/'
        else:
            param_type = 'POSIX'
            pool = None
            cont = None
        self.run_ior_with_params(param_type, path, pool, cont, self.test_file, self.ior_flags[1])

    def test_dm_posix_types_dcp(self):
        """
        Test Description:
            Tests POSIX copies with dcp using different src and dst types.
            DAOS-5508: Verify copy between POSIX, UUIDs, and UNS paths.
            Daos-5511: Verify copy across pools.
        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_dcp,dfuse,dfs,ior
        :avocado: tags=DmvrPosixTypesTest,test_dm_posix_types_dcp
        """
        self.run_dm_posix_types("DCP")

    def test_dm_posix_types_dsync(self):
        """
        Test Description:
            Tests POSIX copies with dsync using different src and dst types.
            DAOS-6389: add basic tests for dsync posix
        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_dsync,dfuse,dfs,ior
        :avocado: tags=DmvrPosixTypesTest,test_dm_posix_types_dsync
        """
        self.run_dm_posix_types("DSYNC")

    def test_dm_posix_types_fs_copy(self):
        """
        Test Description:
            Tests POSIX copies with daos fs copy using different
            src and dst types.
            DAOS-6233: add tests for daos filesystem copy
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=datamover,daos_fs_copy,dfuse,dfs,ior,daos_cmd
        :avocado: tags=DmvrPosixTypesTest,test_dm_posix_types_fs_copy
        """
        self.run_dm_posix_types("FS_COPY")
