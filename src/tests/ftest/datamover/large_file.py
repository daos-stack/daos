#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase
import os

# pylint: disable=too-many-ancestors
class DmvrPosixLargeFile(DataMoverTestBase):
    """Test class Description: Add datamover test to copy a large file amongst
                               daos containers and external file system.

    :avocado: recursive
    """

    def run_dm_large_file(self, tool):
        """
        Test Description:
            Copy a very large file between daos POSIX containers and
            an external POSIX file system.
        Use Cases:
            Create a pool
            Create POSIX type cont1.
            Run ior -a DFS on cont1.
            Create POSIX type cont2.
            Copy data from cont1 to cont2.
            Copy data from cont2 to external POSIX file system.
            Create POSIX type cont4.
            Copy data from external POSIX file system to cont4.
            Run ior -a DFS with read verify on copied directory to verify
            data in cont3.
        """
        # Set the tool to use
        self.set_tool(tool)

        # Get the number of ior processes
        self.ior_processes = self.params.get(
            self.tool.lower(), "/run/ior/client_processes/*")
        if not self.ior_processes:
            self.fail("Failed to get ior processes for {}".format(self.tool))

        # create pool and cont
        self.create_pool()
        self.create_cont(self.pool[0])

        # update and run ior on cont1
        self.run_ior_with_params(
            "DAOS", self.ior_cmd.test_file.value,
            self.pool[0], self.container[0])

        # create cont2
        self.create_cont(self.pool[0])

        # copy from daos cont1 to cont2
        self.run_datamover(
            self.test_id + " (cont1 to cont2)",
            "DAOS", "/", self.pool[0], self.container[0],
            "DAOS", "/", self.pool[0], self.container[1])

        posix_path = self.new_posix_test_path(shared=True)

        # copy from daos cont2 to posix file system
        self.run_datamover(
            self.test_id + " (cont2 to posix)",
            "DAOS", "/", self.pool[0], self.container[1],
            "POSIX", posix_path)

        # create cont3
        self.create_cont(self.pool[0])

        # copy from posix file system to daos cont3
        self.run_datamover(
            self.test_id + " (posix to cont3)",
            "POSIX", posix_path, None, None,
            "DAOS", "/", self.pool[0], self.container[2])

        # the result is that a NEW directory is created in the destination
        daos_path = "/" + os.path.basename(posix_path) + self.ior_cmd.test_file.value

        # update ior params, read back and verify data from cont3
        self.run_ior_with_params(
            "DAOS", daos_path, self.pool[0], self.container[2],
            flags="-r -R")

    def test_dm_large_file_dcp(self):
        """Jira ID: DAOS-4782.
        Test Description:
            Copy a very large file between daos POSIX containers and
            an external POSIX file system using dcp.
        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=datamover,dcp
        :avocado: tags=dm_large_file,dm_large_file_dcp
        """
        self.run_dm_large_file("DCP")

    def test_dm_large_file_fs_copy(self):
        """Jira ID: DAOS-6233.
        Test Description:
            Copy a very large file between daos POSIX containers and
            an external POSIX file system using daos filesystem copy.
        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=datamover,fs_copy
        :avocado: tags=dm_large_file,dm_large_file_fs_copy
        """
        self.run_dm_large_file("FS_COPY")
