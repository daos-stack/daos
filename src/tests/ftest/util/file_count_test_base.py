"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado.core.exceptions import TestFail
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase
from oclass_utils import extract_redundancy_factor

import os

class FileCountTestBase(IorTestBase, MdtestBase):
    """Test class Description: Runs IOR and MDTEST to create specified number of files.

    :avocado: recursive
    """

    def add_containers(self, file_oclass=None, dir_oclass=None):
        """Create a list of containers that the various jobs use for storage.

        Args:
            file_oclass: file object class of container
            dir_oclass: dir object class of container


        """
        # Create a container and add it to the overall list of containers
        container = self.get_container(self.pool, create=False)
        # don't include oclass in daos cont cmd; include rd_fac based on the class
        if file_oclass:
            properties = container.properties.value
            container.file_oclass.update(file_oclass)
            if dir_oclass:
                container.dir_oclass.update(dir_oclass)
            else:
                redundancy_factor = extract_redundancy_factor(file_oclass)
                rd_fac = 'rd_fac:{}'.format(str(redundancy_factor))
                properties = (",").join(filter(None, [properties, rd_fac]))
                container.properties.update(properties)
        container.create()

        return container

    def get_diroclass(self, rd_fac):
        """
        rd_fac: redundancy factor

        Returns: value for dir_oclass
        """

        if rd_fac >= 2:
            dir_oclass = "RP_3GX"
        elif rd_fac == 1:
            dir_oclass = "RP_2GX"
        else:
            dir_oclass = "SX"

        return dir_oclass

    def run_file_count(self):
        """Run the file count test."""
        saved_containers = []
        results = []
        dir_oclass = None
        apis = self.params.get("api", "/run/largefilecount/*")
        hdf5_plugin_path = self.params.get("plugin_path", '/run/hdf5_vol/*')
        mount_dir = self.params.get("mount_dir", "/run/dfuse/*")
        ior_np = self.params.get("np", '/run/ior/client_processes/*', 1)
        ior_ppn = self.params.get("ppn", '/run/ior/client_processes/*', None)
        mdtest_np = self.params.get("np", '/run/mdtest/client_processes/*', 1)
        mdtest_ppn = self.params.get("ppn", '/run/mdtest/client_processes/*', None)
        intercept = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')
        ior_oclass = self.params.get("ior_oclass", '/run/largefilecount/object_class/*')
        mdtest_oclass = self.params.get("mdtest_oclass", '/run/largefilecount/object_class/*')

        # create pool
        self.add_pool(connect=False)

        # iterating only twice as only two elements in ior_oclass and mdtest_oclass
        for idx in range(2):
            self.ior_cmd.dfs_oclass.update(ior_oclass[idx])
            self.mdtest_cmd.dfs_oclass.update(mdtest_oclass[idx])
            self.ior_cmd.dfs_dir_oclass.update(ior_oclass[idx])
            # oclass_dir can not be EC must be RP based on rd_fac
            dir_oclass = self.get_diroclass(extract_redundancy_factor(mdtest_oclass[idx]))
            self.mdtest_cmd.dfs_dir_oclass.update(dir_oclass)
            for api in apis:
                self.ior_cmd.api.update(api)
                self.mdtest_cmd.api.update(api)
                # update test_dir for mdtest if api is DFS
                if api == "DFS":
                    self.mdtest_cmd.test_dir.update("/")
                # run mdtest
                if self.mdtest_cmd.api.value in ['DFS', 'POSIX']:
                    self.log.info("=======>>>Starting MDTEST with %s and %s", api,
                    mdtest_oclass[idx])
                    self.container = self.add_containers(mdtest_oclass[idx], dir_oclass)
                    try:
                        self.processes = mdtest_np
                        self.ppn = mdtest_ppn
                        if self.mdtest_cmd.api.value == 'POSIX':
                            mdtest_cmd.env.update(LD_PRELOAD=intercept, D_IL_REPORT='1')
                            self.execute_mdtest()
                        else:
                            self.execute_mdtest()
                        results.append(["PASS", str(self.mdtest_cmd)])
                    except TestFail:
                        results.append(["FAIL", str(self.mdtest_cmd)])
                # save the current container; to be destroyed later
                if self.container is not None:
                    saved_containers.append(self.container)
                # run ior
                self.log.info("=======>>>Starting IOR with %s and %s", api, ior_oclass[idx])
                self.container = self.add_containers(ior_oclass[idx])
                self.update_ior_cmd_with_pool(False)
                try:
                    self.processes = ior_np
                    self.ppn = ior_ppn
                    if self.ior_cmd.api.value == 'HDF5-VOL':
                        self.ior_cmd.api.update('HDF5')
                        self.run_ior_with_pool(
                            create_pool=False, plugin_path=hdf5_plugin_path, mount_dir=mount_dir)
                    elif self.ior_cmd.api.value == 'POSIX':
                        self.run_ior_with_pool(create_pool=False, intercept=intercept)
                    else:
                        self.run_ior_with_pool(create_pool=False)
                    results.append(["PASS", str(self.ior_cmd)])
                except TestFail:
                    results.append(["FAIL", str(self.ior_cmd)])
                # save the current container
                if self.container is not None:
                    saved_containers.append(self.container)
        # copy saved containers to self.container
        self.container = saved_containers
        self.log.info("=======>>>Summary of Large File Count test results:")
        errors = False
        for item in results:
            self.log.info("  %s  %s", item[0], item[1])
            if item[0] == "FAIL":
                errors = True
        if errors:
            self.fail("Test FAILED")
