#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase
from os.path import join


class DmvrCopyProcs(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for POSIX DataMover multiple processes.

    Test Class Description:
        Tests multi-process (rank) copying of the POSIX DataMover utility.
        Tests the following cases:
            Copying with varying numbers of processes (ranks).

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        # Get the parameters
        self.test_file = self.ior_cmd.test_file.value
        self.ior_flags = self.params.get("ior_flags", "/run/ior/*")

    def test_copy_procs(self):
        """
        Test Description:
            Tests POSIX copy with multiple processes.
            DAOS-5659: Verify multi-process (rank) copying.
        Use Cases:
            Create pool.
            Create POSIX cont1 and cont2 in pool.
            Create a single 100M file in cont1 using ior.
        :avocado: tags=all,daily_regression
        :avocado: tags=small,hw
        :avocado: tags=datamover,mfu,mfu_dcp,dfs,ior
        :avocado: tags=dm_copy_procs
        """
        # Create pool and containers
        pool1 = self.create_pool()
        cont1 = self.create_cont(pool1)
        cont2 = self.create_cont(pool1)

        # Get the varying number of processes
        procs_list = self.params.get("processes", "/run/dcp/copy_procs/*")

        # Generate test file paths
        src_daos = join("/", self.test_file)
        dst_daos = join("/", self.test_file)
        src_posix = join(self.new_posix_test_path(), self.test_file)
        dst_posix = join(self.new_posix_test_path(), self.test_file)

        # Create the test files
        self.run_ior_with_params("DAOS", src_daos, pool1, cont1, flags=self.ior_flags[0])
        self.run_ior_with_params("POSIX", src_posix, flags=self.ior_flags[0])

        # DAOS -> POSIX
        # Run with varying number of processes
        self.set_datamover_params(
            "DAOS_UUID", src_daos, pool1, cont1,
            "POSIX", dst_posix)
        for num_procs in procs_list:
            test_desc = "copy_procs (DAOS->POSIX with {} procs)".format(num_procs)
            self.run_datamover(test_desc=test_desc, processes=num_procs)
            self.run_ior_with_params("POSIX", dst_posix, flags=self.ior_flags[1])

        # POSIX -> DAOS
        # Run with varying number of processes
        self.set_datamover_params(
            "POSIX", src_posix, None, None,
            "DAOS_UUID", dst_daos, pool1, cont2)
        for num_procs in procs_list:
            test_desc = "copy_procs (POSIX->DAOS with {} procs)".format(num_procs)
            self.run_datamover(test_desc=test_desc, processes=num_procs)
            self.run_ior_with_params("DAOS_UUID", dst_daos, pool1, cont2, flags=self.ior_flags[1])
