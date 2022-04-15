#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase
from pydaos.raw import DaosApiError
import avocado


class DmvrObjSmallTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Object Data Mover validation for syncing/cloning generic containers
       at the object level.

    Test Class Description:
        Tests the following cases:
            Cloning a small container with dcp.
    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        # Get the dataset parameters
        self.num_objs = self.params.get(
            "num_objs", "/run/dataset/*")
        self.num_dkeys = self.params.get(
            "num_dkeys", "/run/dataset/*")
        self.num_akeys_array = self.params.get(
            "num_akeys_array", "/run/dataset/*")
        self.num_akeys_single = self.params.get(
            "num_akeys_single", "/run/dataset/*")
        self.akey_sizes = self.params.get(
            "akey_sizes", "/run/dataset/*")
        self.akey_extents = self.params.get(
            "akey_extents", "/run/dataset/*")

    def run_dm_obj_small(self, tool):
        """
        Test Description:
            Tests cloning a small container.
        Use Cases:
            Create pool1.
            Create cont1 in pool1.
            Create a small dataset in cont1.
            Clone cont1 to a new cont2 in pool1.
            Create pool2.
            Clone cont1 to a new cont3 in pool2.

        Args:
            tool (str): the tool to use. Must be in DataMoverTestBase.TOOLS
        """
        # Set the tool to use
        self.set_tool(tool)

        # Create pool1
        pool1 = self.create_pool()
        pool1.connect(2)

        # Create cont1
        cont1 = self.create_cont(pool1)

        # Create dataset in cont1
        obj_list = self.dataset_gen(
            cont1,
            self.num_objs, self.num_dkeys, self.num_akeys_single,
            self.num_akeys_array, self.akey_sizes, self.akey_extents)

        # Generate a uuid for cont2
        cont2_uuid = self.gen_uuid()

        # Clone cont1 to a new cont2 in pool1
        self.run_datamover(
            self.test_id + " (cont1->cont2) (same pool)",
            "DAOS_UUID", None, pool1, cont1,
            "DAOS_UUID", None, pool1, cont2_uuid)

        # Verify data in cont2
        cont2 = self.get_cont(pool1, cont2_uuid)
        self.dataset_verify(
            obj_list, cont2,
            self.num_objs, self.num_dkeys, self.num_akeys_single,
            self.num_akeys_array, self.akey_sizes, self.akey_extents)

        # Create pool2
        pool2 = self.create_pool()
        pool2.connect(2)

        # Generate a uuid for cont3
        cont3_uuid = self.gen_uuid()

        # Clone cont1 to a new cont3 in pool2
        self.run_datamover(
            self.test_id + " (cont1->cont3) (different pool)",
            "DAOS_UUID", None, pool1, cont1,
            "DAOS_UUID", None, pool2, cont3_uuid)

        # Verify data in cont3
        cont3 = self.get_cont(pool2, cont3_uuid)
        self.dataset_verify(
            obj_list, cont3,
            self.num_objs, self.num_dkeys, self.num_akeys_single,
            self.num_akeys_array, self.akey_sizes, self.akey_extents)

        # Must destroy before closing pools
        cont1.destroy()
        cont2.destroy()
        cont3.destroy()
        pool1.disconnect()
        pool2.disconnect()

    @avocado.fail_on(DaosApiError)
    def test_dm_obj_small_dcp(self):
        """
        Test Description:
            DAOS-6858: Verify cloning a small container.
        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_dcp
        :avocado: tags=dm_obj_small,dm_obj_small_dcp
        """
        self.run_dm_obj_small("DCP")

    @avocado.fail_on(DaosApiError)
    def test_dm_obj_small_cont_clone(self):
        """
        Test Description:
            DAOS-6858: Verify cloning a small container.
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=datamover,daos_cont_clone
        :avocado: tags=dm_obj_small,dm_obj_small_cont_clone
        """
        self.run_dm_obj_small("CONT_CLONE")
