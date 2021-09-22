#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase


# pylint: disable=attribute-defined-outside-init
class LargeFileCount(MdtestBase, IorTestBase):
    """Test class Description: Runs IOR and MDTEST to create large number of files.

    :avocado: recursive
    """

    def run_largefile_count(self, rc=False):
        """Run the large file count test.

        Args:
            rc (bool, optional): If release candidate set to true. Defaults to False.
        """
        apis = self.params.get("api", "/run/largefilecount/*")

        if rc:
            # update mdtest file count
            num_of_files_dirs = self.params.get("num_of_files_dirs_rc", "/run/mdtest/*")
            self.mdtest_cmd.num_of_files_dirs.update(num_of_files_dirs)
            block_size = self.params.get("block_size_rc", "/run/ior/*")
            self.ior_cmd.block_size.update(block_size)

        for api in apis:
            self.ior_cmd.api.update(api)
            self.mdtest_cmd.api.update(api)
            # update test_dir for mdtest if api is DFS
            if api == "DFS":
                self.mdtest_cmd.test_dir.update("/")
            # create container for mdtest
            self.add_container(self.pool)
            # run mdtest and ior
            self.execute_mdtest()
            # create container for ior
            self.add_container(self.pool)
            self.update_ior_cmd_with_pool(False)
            self.run_ior_with_pool(create_pool=False)

    def test_largefilecount(self):
        """Jira ID: DAOS-3845.

        Test Description:
            Run IOR and MDTEST with 48 clients.
        Use Cases:
            Run IOR for 5 mints with DFS and POSIX and create 1OOGB file
            Run MDTEST to create 50K files with DFS and POSIX
        :avocado: tags=all,daosio,full_regression
        :avocado: tags=hw,large,dfuse
        :avocado: tags=largefilecount
        """
        # create pool
        self.add_pool(connect=False)
        self.run_largefile_count()

    def test_largefilecount_rc(self):
        """Jira ID: DAOS-3845.

        Test Description:
            Run IOR and MDTEST with 48 clients with very large file counts.
            This test is not supposed to run as part of pr or weekly runs.
            This should only be run before release as desired.
        Use Cases:
            Run IOR for 5 mints with DFS and POSIX and create 1TB file
            Run MDTEST to create 1B files with DFS and POSIX
        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=daosio,dfuse
        :avocado: tags=rc,largefilecount_rc
        """
        # create pool
        self.add_pool(connect=False)
        self.run_largefile_count(rc=True)
