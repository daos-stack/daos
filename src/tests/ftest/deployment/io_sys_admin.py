#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time

from file_count_test_base import FileCountTestBase
from data_mover_test_base import DataMoverTestBase
from general_utils import human_to_bytes
from pool_test_base import PoolTestBase

class IoSysAdmin(DataMoverTestBase, FileCountTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description: Test class wrapping up tests from four
                               different test classes into one. Intent
                               is to run these tests as a basic checkout
                               for newly installed servers.
    :avocado: recursive
    """

    def get_free_space(self):
        """Display pool free space."""
        free_space = []
        scm_free_space = self.pool.get_pool_free_space("scm")
        free_space.append(scm_free_space)
        nvme_free_space = self.pool.get_pool_free_space("nvme")
        free_space.append(nvme_free_space)
        self.log.info("Free space [SCM, Nvme]: [%s, %s]", scm_free_space, nvme_free_space)

        return free_space

    def test_io_sys_admin(self):
        """
        Test Description: Bundles four tests into one and run in the
                          following sequence - ior_small, mdtest_small,
                          ec_smoke and autotest.
        :avocado: tags=hw,large
        :avocado: tags=deployment,iosysadmin
        """
        # local param
        ec_ior_flags = self.params.get("ec_ior_flags", "/run/ior/*")
        hdf5_plugin_path = self.params.get("plugin_path", '/run/hdf5_vol/*')
        dmg = self.get_dmg_command()
        daos = self.get_daos_command()
#        sys_admin_pool_sizes = self.params.get("pool_sizes", '/run/sys_admin/*')

        for idx in range(1, 4):
            self.add_pool_qty(1, namespace="/run/pool_{}/".format(idx), create=False)
            PoolTestBase.check_pool_creation(self, 60)
#            self.pool[-1].connect()
            self.add_container_qty(1, self.pool[-1], namespace="/run/container_{}/".format(idx), create=False)
            for count, container in enumerate(self.container):
                container.create()
#            daos.container_set_owner(self.pool[-1].uuid, self.container[-1].uuid, 'root', 'daos_server')
            self.destroy_containers(self.container)
            self.container = None
            if idx is not 3:
                self.destroy_pools(self.pool)
                self.pool = None


#        self.add_pool_qty(1, namespace="/run/pool_2/", create=False)
#        PoolTestBase.check_pool_creation(self, 30)
#        self.destroy_pools(self.pool)
#        self.pool = None
#        self.add_pool_qty(1, namespace="/run/pool_3/", create=False)
#        PoolTestBase.check_pool_creation(self, 60)

        dmg.pool_list()
        daos.container_list(self.pool[-1])
        # run tests
#        self.test_dmg_storage_scan_scm()

#        self.test_basic_config()
#        self.test_access_points_single()
#        self.test_access_points_odd()
#        self.test_access_points_invalid()
#        self.test_access_points_same_ap_repeated()
#        self.test_num_engines()
#        self.test_min_ssds()
#        self.test_net_class()


#        self.run_file_count()
        # create snapshot
#        self.container[-1].create_snap()        
        # overwrite the last ior file
#        self.ior_cmd.signature.update('456')
#        self.run_ior_with_pool(create_pool=False, create_cont=False)

#        nvme_free_space_before_snap_destroy = self.get_free_space()[1]
        # delete snapshot
#        self.container[-1].destroy_snap(epc=self.container[-1].epoch)
        # Now check if the space is returned back.
#        counter = 1
#        returned_space = (self.get_free_space()[1] -
#                          nvme_free_space_before_snap_destroy)

#        data_written = (int(self.processes) * human_to_bytes(self.ior_cmd.block_size.value))
#        while returned_space < int(data_written):
            # try to wait for 4 x 60 secs for aggregation to be completed or
            # else exit the test with a failure.
#            if counter > 4:
#                self.log.info("Free space before snapshot destroy: %s",
#                              nvme_free_space_before_snap_destroy)
#                self.log.info("Free space when test terminated: %s",
#                              self.get_free_space()[1])
#                self.fail("Aggregation did not complete as expected")

#            time.sleep(60)
#            returned_space = (self.get_free_space()[1] -
#                              nvme_free_space_before_snap_destroy)
#            counter += 1

#        print("#####Starting FS_COPY Test")
#        self.run_dm_activities_with_ior("FS_COPY", pool=self.pool)
#        print("#####Starting DCP Test")
#        self.run_dm_activities_with_ior("DCP", pool=self.pool)
#        print("#####Starting DSERIAL Test")
#        self.run_dm_activities_with_ior("DSERIAL", pool=self.pool)
#        print("#####Completed all Datamover tests")
#        self.container.pop(0)

#        self.run_dm_large_file_dep("FS_COPY")
#        self.run_dm_large_file_dep("DCP")
##        self.run_dm_large_file_dep("CONT_CLONE")
#        self.run_dm_large_file_dep("DSERIAL")

#        self.test_dm_large_file_dcp()
#        self.test_dm_large_file_fs_copy()
#        self.test_ior_small()
#        self.test_mdtest_small()

#        self.ior_cmd.flags.update(ec_ior_flags)
#        self.test_ec()

#        self.container.destroy()
#        self.pool.destroy()

#        self.test_container_autotest()

