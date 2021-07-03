#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase
from apricot import skipForTicket

import write_host_file


# pylint: disable=attribute-defined-outside-init
class LargeFileCount(MdtestBase, IorTestBase):
    """Test class Description: Runs IOR and MDTEST to create large number
                               of files.
    :avocado: recursive
    """

    def single_client(self, api):
        """Use one client only for POSIX api until DAOS-3320
           is resolved.
        Arguments:
            api (str): IOR or MDTEST api to be used
        """
        if api == "POSIX":
            self.hostlist_clients = [self.hostlist_clients[0]]
            self.hostfile_clients = write_host_file.write_host_file(
                self.hostlist_clients, self.workdir,
                self.hostfile_clients_slots)

    def create_cont(self, oclass):
        """Override container create method

          Args:
            oclass(str): Object class type
        """
        # create container
        self.container = self.get_container(self.pool, create=False)
        self.container.oclass.update(oclass)
        self.container.create()

    def test_largefilecount(self):
        """Jira ID: DAOS-3845.
        Test Description:
            Run IOR and MDTEST with 48 clients.
        Use Cases:
            Run IOR for 5 mints with DFS and POSIX
            Run MDTEST to create 25K files with DFS and POSIX
        :avocado: tags=all,daosio,full_regression
        :avocado: tags=hw,large
        :avocado: tags=largefilecount
        """
        apis = self.params.get("api", "/run/largefilecount/*")
        object_class = self.params.get("object_class", '/run/largefilecount/*')

        # create pool
        self.add_pool(connect=False)

        for oclass in object_class:
            self.ior_cmd.dfs_oclass.update(oclass)
            self.mdtest_cmd.dfs_oclass.update(oclass)
            for api in apis:
                self.ior_cmd.api.update(api)
                self.mdtest_cmd.api.update(api)
                # update test_dir for mdtest if api is DFS
                if api == "DFS":
                    self.mdtest_cmd.test_dir.update("/")
                # Until DAOS-3320 is resolved run IOR for POSIX
                # with single client node
                self.single_client(api)

                # create container for mdtest
                self.create_cont(oclass)
                # run mdtest and ior
                self.execute_mdtest()
                # create container for ior
                self.create_cont(oclass)
                self.update_ior_cmd_with_pool(False)
                self.run_ior_with_pool(create_pool=False)
                # container destroy
                self.container.destroy()

    @skipForTicket("DAOS-7287")
    def test_largefilecount_rc(self):
        """Jira ID: DAOS-3845.
        Test Description:
            Run IOR and MDTEST with 48 clients with very large file counts.
            This test is not supposed to run as part of pr or weekly runs.
            This should only be run before release as desired.
        Use Cases:
            Run IOR for 5 mints with DFS and POSIX
            Run MDTEST to create 25K files with DFS and POSIX
        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=daosio
        :avocado: tags=rc,largefilecount_rc
        """
        apis = self.params.get("api", "/run/largefilecount/*")
        num_of_files_dirs_rc = self.params.get("num_of_files_dirs_rc",
                                               "/run/mdtest/*")
        object_class = self.params.get("object_class", '/run/largefilecount/*')

        # update mdtest file count
        self.mdtest_cmd.num_of_files_dirs.update(num_of_files_dirs_rc)

        for oclass in object_class:
            self.ior_cmd.dfs_oclass.update(oclass)
            self.mdtest_cmd.dfs_oclass.update(oclass)
            for api in apis:
                self.ior_cmd.api.update(api)
                self.mdtest_cmd.api.update(api)
                # update test_dir for mdtest if api is DFS
                if api == "DFS":
                    self.mdtest_cmd.test_dir.update("/")
                # Until DAOS-3320 is resolved run IOR for POSIX
                # with single client node
                self.single_client(api)

                # create container for mdtest
                self.create_cont(oclass)
                # run mdtest and ior
                self.execute_mdtest()
                # create container for ior
                self.create_cont(oclass)
                self.update_ior_cmd_with_pool(False)
                self.run_ior_with_pool(create_pool=False)
                # container destroy
                self.container.destroy()
