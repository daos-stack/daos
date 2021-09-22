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

    def add_containers(self, path="/run/container/*"):
        """Create a list of containers that the various jobs use for storage.

        Args:
            pool: pool to create container
            oclass: object class of container


        """
        # Create a container and add it to the overall list of containers
        self.container.append(self.get_container(self.pool))
        self.container[-1].namespace = path
        self.container[-1].get_params(self)
        self.container[-1].create()

    def run_largefile_count(self, rc=False):
        """Run the large file count test.

        Args:
            rc (bool, optional): If release candidate set to true. Defaults to False.
        """
        saved_container = []
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
            # run mdtest
            self.execute_mdtest()
            # save the current container; to be destroyed later
            saved_container.append(self.container)
            # run ior
            self.run_ior_with_pool(create_pool=False)
            # save the current container
            saved_container.append(self.container)
        # copy saved containers to self.container
        self.container = saved_container

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
