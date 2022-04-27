#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase
from avocado.core.exceptions import TestFail


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
class FileCountTestBase(IorTestBase, MdtestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR and MDTEST to create specified number of files.

    :avocado: recursive
    """

    def add_containers(self, oclass=None):
        """Create a list of containers that the various jobs use for storage.

        Args:
            oclass: object class of container


        """
        # Create a container and add it to the overall list of containers
        container = self.get_container(self.pool, create=False)
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

    def run_file_count(self):
        """Run the file count test."""
        saved_containers = []
        results = []
        apis = self.params.get("api", "/run/largefilecount/*")
        object_class = self.params.get("object_class", '/run/largefilecount/*')
        hdf5_plugin_path = self.params.get("plugin_path", '/run/hdf5_vol/*')
        mount_dir = self.params.get("mount_dir", "/run/dfuse/*")
        # create pool
        self.add_pool(connect=False)

        for oclass in object_class:
            self.ior_cmd.dfs_oclass.update(oclass)
            self.mdtest_cmd.dfs_oclass.update(oclass)
            self.ior_cmd.dfs_dir_oclass.update(oclass)
            # oclass_dir can not be EC must be RP based on rf
            rf = get_rf(oclass)
            if rf >= 2:
                self.mdtest_cmd.dfs_dir_oclass.update("RP_3G1")
            elif rf == 1:
                self.mdtest_cmd.dfs_dir_oclass.update("RP_2G1")
            else:
                self.mdtest_cmd.dfs_dir_oclass.update("SX")
            for api in apis:
                self.ior_cmd.api.update(api)
                self.mdtest_cmd.api.update(api)
                # update test_dir for mdtest if api is DFS
                if api == "DFS":
                    self.mdtest_cmd.test_dir.update("/")
                # run mdtest
                if self.mdtest_cmd.api.value in ['DFS', 'POSIX']:
                    self.log.info("=======>>>Starting MDTEST with %s and %s", api, oclass)
                    self.container = self.add_containers(oclass)
                    try:
                        self.execute_mdtest()
                        results.append(["PASS", str(self.mdtest_cmd)])
                    except TestFail:
                        results.append(["FAIL", str(self.mdtest_cmd)])
                # save the current container; to be destroyed later
                if self.container is not None:
                    saved_containers.append(self.container)
                # run ior
                self.log.info("=======>>>Starting IOR with %s and %s", api, oclass)
                self.container = self.add_containers(oclass)
                self.update_ior_cmd_with_pool(False)
                try:
                    if self.ior_cmd.api.value == 'HDF5-VOL':
                        self.ior_cmd.api.update('HDF5')
                        self.run_ior_with_pool(create_pool=False,
                                               plugin_path=hdf5_plugin_path,
                                               mount_dir=mount_dir)
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
