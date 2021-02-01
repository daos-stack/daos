#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase

# pylint: disable=too-many-ancestors
class CopyLargeMdtest(DataMoverTestBase):
    """Test class Description: Add datamover test to copy large data amongst
                               daos containers and external file system.

    :avocado: recursive
    """

    def test_daoscont1_to_daoscont2(self):
        """Jira ID: DAOS-4782.
        Test Description:
            Copy data from daos cont1 to daos cont2
        Use Cases:
            Create a pool.
            Create POSIX type container.
            Run mdtest -a DFS to create 50K files of size 4K
            Create second container
            Copy data from cont1 to cont2
            Run mdtest again, but this time on cont2 and read the files back.
        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=datamover
        :avocado: tags=copy_large,copy_large_mdtest,daoscont1_to_daoscont2
        """

        # test params
        mdtest_flags = self.params.get("mdtest_flags", "/run/mdtest/*")
        file_size = self.params.get("bytes", "/run/mdtest/*")
        # create pool and cont
        self.create_pool()
        self.create_cont(self.pool[0])

        # run mdtest to create data in cont1
        self.mdtest_cmd.write_bytes.update(file_size)
        self.run_mdtest_with_params(
            "DAOS", "/", self.pool[0], self.container[0],
            flags=mdtest_flags[0])

        # create second container
        self.create_cont(self.pool[0])

        # copy data from cont1 to cont2
        self.run_datamover(
            "daoscont1_to_daoscont2 (cont1 to cont2)",
            "DAOS", "/", self.pool[0], self.container[0],
            "DAOS", "/", self.pool[0], self.container[1])

        # run mdtest read on cont2
        self.mdtest_cmd.read_bytes.update(file_size)
        self.run_mdtest_with_params(
            "DAOS", "/", self.pool[0], self.container[1],
            flags=mdtest_flags[1])
