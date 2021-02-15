#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase
from os.path import basename

# pylint: disable=too-many-ancestors
class DmLargeDir(DataMoverTestBase):
    """Test class Description: Add datamover test to copy a large directory
                               amongst daos containers and external file
                               system.

    :avocado: recursive
    """

    def run_dm_large_dir(self, tool):
        """
        Test Description:
            Copy a very large directory between daos POSIX containers and
            an external POSIX file system.
        Use Cases:
            Create a pool
            Create POSIX type cont1.
            Run mdtest -a DFS on cont1.
            Create POSIX type cont2.
            Copy data from cont1 to cont2.
            Copy data from cont2 to external POSIX file system.
            (Assuming dfuse mount as external POSIX FS).
            Create POSIX type cont4.
            Copy data from external POSIX file system to cont4.
            Run mdtest -a DFS with read verify on copied directory to verify
            data in cont3.
        """
        # Set the tool to use
        self.set_tool(tool)

        # Get the number of mdtest processes
        self.mdtest_processes = self.params.get(
            self.tool.lower(), "/run/mdtest/client_processes/*")
        if not self.mdtest_processes:
            self.fail("Failed to get mdtest processes for {}".format(tool))

        # test params
        mdtest_flags = self.params.get("mdtest_flags", "/run/mdtest/*")
        file_size = self.params.get("bytes", "/run/mdtest/*")

        # create pool and cont1
        self.create_pool()
        self.create_cont(self.pool[0])

        # run mdtest to create data in cont1
        self.mdtest_cmd.write_bytes.update(file_size)
        self.run_mdtest_with_params(
            "DAOS", "/", self.pool[0], self.container[0],
            flags=mdtest_flags[0])

        # create cont2
        self.create_cont(self.pool[0])

        # copy from daos cont1 to cont2
        self.run_datamover(
            self.test_id + " (cont1 to cont2)",
            "DAOS", "/", self.pool[0], self.container[0],
            "DAOS", "/", self.pool[0], self.container[1])

        # create cont3 for dfuse (posix)
        self.create_cont(self.pool[0])

        # start dfuse on cont3
        self.start_dfuse(self.dfuse_hosts, self.pool[0], self.container[2])

        # dcp treats a trailing slash on the source as /*
        # so strip trailing slash from posix path so dcp
        # behaves similar to "cp"
        posix_path = self.dfuse.mount_dir.value.rstrip("/")

        # copy from daos cont2 to posix file system (dfuse)
        self.run_datamover(
            self.test_id + " (cont2 to posix)",
            "DAOS", "/", self.pool[0], self.container[1],
            "POSIX", posix_path)

        # create cont4
        self.create_cont(self.pool[0])

        # copy from posix file system to daos cont4
        self.run_datamover(
            self.test_id + " (posix to cont4)",
            "POSIX", posix_path, None, None,
            "DAOS", "/", self.pool[0], self.container[3])

        # the result is that a NEW directory is created in the destination
        daos_path = "/" + basename(posix_path) + self.mdtest_cmd.test_dir.value

        # update mdtest params, read back and verify data from cont4
        self.mdtest_cmd.read_bytes.update(file_size)
        self.run_mdtest_with_params(
            "DAOS", daos_path, self.pool[0], self.container[3],
            flags=mdtest_flags[1])

    def test_dm_large_dir_dcp(self):
        """Jira ID: DAOS-4782.
        Test Description:
            Copy a very large directory between daos POSIX containers and
            an external POSIX file system using dcp.
        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=datamover,dcp
        :avocado: tags=dm_large_dir,dm_large_dir_dcp
        """
        self.run_dm_large_dir("DCP")
