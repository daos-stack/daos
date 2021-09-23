#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase


def get_rf(oclass):
    """Return redundancy factor based on the oclass.

    Args:
        oclass(string): object class.

    return:
        redundancy factor(int) from object type
    """
    rf = 0
    if "EC" in oclass:
        tmp = re.findall(r'\d+', oclass)
        if tmp:
            rf = int(tmp[1])
    elif "RP" in oclass:
        tmp = re.findall(r'\d+', oclass)
        if tmp:
            rf = int(tmp[0]) - 1
    else:
        rf = 0
    return rf


# pylint: disable=attribute-defined-outside-init
class LargeFileCount(MdtestBase, IorTestBase):
    """Test class Description: Runs IOR and MDTEST to create large number of files.

    :avocado: recursive
    """

    def add_containers(self, oclass=None):
        """Create a list of containers that the various jobs use for storage.

        Args:
            pool: pool to create container
            oclass: object class of container


        """
        # Create a container and add it to the overall list of containers
        container = self.get_container(self.pool)
        # don't include oclass in daos cont cmd; include rf based on the class
        if oclass:
            container.oclass.update(oclass)
            redundancy_factor = get_rf(oclass)
            rf = 'rf:{}'.format(str(redundancy_factor))
        properties = container.properties.value
        cont_properties = (",").join(filter(None, [properties, rf]))
        if cont_properties is not None:
            container.properties.update(cont_properties)
        container.create()
        return container

    def run_largefile_count(self, rc=False):
        """Run the large file count test.

        Args:
            rc (bool, optional): If release candidate set to true. Defaults to False.
        """
        saved_container = []
        apis = self.params.get("api", "/run/largefilecount/*")
        object_class = self.params.get("object_class", '/run/largefilecount/*')
        # create pool
        self.add_pool(connect=False)

        if rc:
            # update mdtest file count
            num_of_files_dirs = self.params.get("num_of_files_dirs_rc", "/run/mdtest/*")
            self.mdtest_cmd.num_of_files_dirs.update(num_of_files_dirs)
            block_size = self.params.get("block_size_rc", "/run/ior/*")
            self.ior_cmd.block_size.update(block_size)
        for oclass in object_class:
            self.ior_cmd.dfs_oclass.update(oclass)
            self.mdtest_cmd.dfs_oclass.update(oclass)
            for api in apis:
                self.ior_cmd.api.update(api)
                self.mdtest_cmd.api.update(api)
                # update test_dir for mdtest if api is DFS
                if api == "DFS":
                    self.mdtest_cmd.test_dir.update("/")
                # run mdtest
                self.container = self.add_containers(oclass)
                self.log.info("Starting MDTEST with %s and %s", api, oclass)
                self.execute_mdtest()
                # save the current container; to be destroyed later
                saved_container.append(self.container)
                # run ior
                self.container = self.add_containers(oclass)
                self.update_ior_cmd_with_pool(False)
                self.log.info("Starting IOR with %s and %s", api, oclass)
                self.run_ior_with_pool(create_pool=False)
                # save the current container
                saved_container.append(self.container)
        # copy saved containers to self.container
        self.container = saved_container

    def test_largefilecount(self):
        """Jira ID: DAOS-3845.

        Test Description:
            Run IOR and MDTEST with 30 clients.
        Use Cases:
            Run IOR for 5 mints with DFS and POSIX and create 60GB file
            Run MDTEST to create 50K files with DFS and POSIX
        :avocado: tags=all,daosio,full_regression
        :avocado: tags=hw,large,dfuse
        :avocado: tags=largefilecount
        """
        self.run_largefile_count()

    def test_largefilecount_rc(self):
        """Jira ID: DAOS-3845.

        Test Description:
            Run IOR and MDTEST with 30 clients with very large file counts.
            This test is not supposed to run as part of pr or weekly runs.
            This should only be run before release as desired.
        Use Cases:
            Run IOR for 5 mints with DFS and POSIX and create 600GB file
            Run MDTEST to create 1M files with DFS and POSIX
        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=daosio,dfuse
        :avocado: tags=rc,largefilecount_rc
        """
        self.run_largefile_count(rc=True)
