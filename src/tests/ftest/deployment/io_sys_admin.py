#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time

from file_count_test_base import FileCountTestBase
from data_mover_test_base import DataMoverTestBase
from general_utils import human_to_bytes
from pool_test_base import PoolTestBase
import security_test_base as secTestBase

class IoSysAdmin(DataMoverTestBase, FileCountTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description: Test class for large IO tests and some
                               basic system admin operations.
    :avocado: recursive
    """

    def get_free_space(self):
        """Display pool free space."""
        pool_space = self.pool.get_pool_daos_space()
        self.log.info("Free space [SCM, NVMe]: %s", list(pool_space["s_free"]))
        return pool_space["s_free"]

    def test_io_sys_admin(self):
        """
        Test Description: Performs tests to generate large data sets over
                          various middleware, perform various system admin
                          operations, datamover operations.
        :avocado: tags=all,deployment,full_regression
        :avocado: tags=hw,large
        :avocado: tags=datamover,ior,mdtest
        :avocado: tags=iosysadmin
        """
        # local param
        new_test_user = self.params.get("new_user", "/run/container_acl/*")
        new_test_group = self.params.get("new_group", "/run/container_acl/*")

        dmg = self.get_dmg_command()
        daos = self.get_daos_command()

        secTestBase.add_del_user(
            self.hostlist_clients, "useradd", new_test_user)
        secTestBase.add_del_user(
            self.hostlist_clients, "groupadd", new_test_group)

        for idx in range(1, 4):
            self.add_pool_qty(1, namespace="/run/pool_{}/".format(idx), create=False)
            PoolTestBase.check_pool_creation(self, 60)
            self.pool[-1].connect()
            for cont_idx in range(1, 4):
                # Appends to self.container
                self.get_container(
                    self.pool[-1], namespace="/run/container_{}/".format(cont_idx))
                daos.container_set_owner(self.pool[-1].uuid, self.container[-1].uuid,
                                         new_test_user, new_test_group)

            daos.container_list(self.pool[-1].uuid)
            self.destroy_containers(self.container)
            self.container = None
            self.destroy_pools(self.pool)
            self.pool = None

        # dmg storage scan
        dmg.storage_scan()
        dmg.system_query()
        dmg.system_leader_query()

        # write large data sets
        self.run_file_count()
        # create snapshot
        self.container[-1].create_snap()
        # overwrite the last ior file
        self.ior_cmd.signature.update('456')
        self.run_ior_with_pool(create_pool=False, create_cont=False)

        nvme_free_space_before_snap_destroy = self.get_free_space()[1]
        # delete snapshot
        self.container[-1].destroy_snap(epc=self.container[-1].epoch)
        # Now check if the space is returned back.
        counter = 1
        returned_space = (self.get_free_space()[1] -
                          nvme_free_space_before_snap_destroy)

        data_written = (int(self.ppn) * human_to_bytes(self.ior_cmd.block_size.value))
        while returned_space < int(data_written):
            # try to wait for 4 x 60 secs for aggregation to be completed or
            # else exit the test with a failure.
            if counter > 4:
                self.log.info("Free space before snapshot destroy: %s",
                              nvme_free_space_before_snap_destroy)
                self.log.info("Free space when test terminated: %s",
                              self.get_free_space()[1])
                self.fail("Aggregation did not complete as expected")

            time.sleep(60)
            returned_space = (self.get_free_space()[1] -
                              nvme_free_space_before_snap_destroy)
            counter += 1

        self.log.info("#####Starting FS_COPY Test")
        self.run_dm_activities_with_ior("FS_COPY", pool=self.pool)
        self.log.info("#####Starting DCP Test")
        self.run_dm_activities_with_ior("DCP", pool=self.pool)
        self.log.info("#####Starting DSERIAL Test")
        self.run_dm_activities_with_ior("DSERIAL", pool=self.pool)
        self.log.info("#####Completed all Datamover tests")
        self.container.pop(0)
