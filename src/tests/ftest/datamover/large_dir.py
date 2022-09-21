'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os

from data_mover_test_base import DataMoverTestBase
from duns_utils import format_path

# pylint: disable=too-many-ancestors
class DmvrLargeDir(DataMoverTestBase):
    """Test class Description: Add datamover test to copy a large directory
                               amongst DAOS containers and external file system.

    :avocado: recursive
    """

    def run_dm_large_dir(self, tool):
        """
        Test Description:
            Copy a large directory between DAOS POSIX containers and external POSIX file system.
        Use Cases:
            Create a pool.
            Create POSIX type cont1.
            Run mdtest -a DFS on cont1.
            Create POSIX type cont2.
            Copy data from cont1 to cont2.
            Copy data from cont2 to external POSIX file system.
            Create POSIX type cont4.
            Copy data from external POSIX file system to cont4.
            Run mdtest -a DFS with read verify on copied directory to verify data in cont3.
        """
        # Set the tool to use
        self.set_tool(tool)

        # Get the number of mdtest processes
        self.mdtest_np = self.params.get(self.tool.lower(), "/run/mdtest/client_processes/*")
        self.mdtest_ppn = self.params.get(self.tool.lower(), "/run/mdtest/client_ppn/*")
        if self.mdtest_np is None and self.mdtest_ppn is None:
            self.fail("Failed to get mdtest processes for {}".format(tool))

        # test params
        mdtest_flags = self.params.get("mdtest_flags", self.mdtest_cmd.namespace)
        file_size = self.params.get("bytes", self.mdtest_cmd.namespace)

        # create pool and cont1
        pool = self.create_pool()
        cont1 = self.get_container(pool)

        # run mdtest to create data in cont1
        self.mdtest_cmd.write_bytes.update(file_size)
        self.run_mdtest_with_params("DAOS", "/", pool, cont1, flags=mdtest_flags[0])

        # create cont2
        cont2 = self.get_container(pool)

        # copy from daos cont1 to cont2
        self.run_datamover(
            self.test_id + " (cont1 to cont2)",
            src_path=format_path(pool, cont1),
            dst_path=format_path(pool, cont2))

        posix_path = self.new_posix_test_path()

        # copy from daos cont2 to posix file system
        self.run_datamover(
            self.test_id + " (cont2 to posix)",
            src_path=format_path(pool, cont2),
            dst_path=posix_path)

        # create cont3
        cont3 = self.get_container(pool)

        # copy from posix file system to daos cont3
        self.run_datamover(
            self.test_id + " (posix to cont3)",
            src_path=posix_path,
            dst_path=format_path(pool, cont3))

        # the result is that a NEW directory is created in the destination
        daos_path = "/" + os.path.basename(posix_path) + self.mdtest_cmd.test_dir.value

        # update mdtest params, read back and verify data from cont3
        self.mdtest_cmd.read_bytes.update(file_size)
        self.run_mdtest_with_params(
            "DAOS", daos_path, pool, cont3,
            flags=mdtest_flags[1])

    def test_dm_large_dir_dcp(self):
        """Jira ID: DAOS-4782.
        Test Description:
            Copy a very large directory between daos POSIX containers and
            an external POSIX file system using dcp.
        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=datamover,mfu,mfu_dcp,dfs,mdtest
        :avocado: tags=dm_large_dir,dm_large_dir_dcp,test_dm_large_dir_dcp
        """
        self.run_dm_large_dir("DCP")
