'''
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import avocado
from data_mover_test_base import DataMoverTestBase
from pydaos.raw import DaosApiError
from test_utils_container import get_existing_container


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
        pool1 = self.get_pool()

        # Create cont1
        cont1 = self.get_container(pool1)

        # Create dataset in cont1
        obj_list = self.dataset_gen(
            cont1,
            self.num_objs, self.num_dkeys, self.num_akeys_single,
            self.num_akeys_array, self.akey_sizes, self.akey_extents)

        # Clone cont1 to a new cont2 in pool1
        result = self.run_datamover(
            self.test_id + " (cont1->cont2) (same pool)",
            "DAOS_UUID", None, pool1, cont1,
            "DAOS_UUID", None, pool1, None)
        cont2_label = self.parse_create_cont_label(result.stdout_text)

        # Verify data in cont2
        cont2 = get_existing_container(self, pool1, cont2_label)
        self.dataset_verify(
            obj_list, cont2,
            self.num_objs, self.num_dkeys, self.num_akeys_single,
            self.num_akeys_array, self.akey_sizes, self.akey_extents)

        # Create pool2
        pool2 = self.get_pool()

        # Clone cont1 to a new cont3 in pool2
        result = self.run_datamover(
            self.test_id + " (cont1->cont3) (different pool)",
            "DAOS_UUID", None, pool1, cont1,
            "DAOS_UUID", None, pool2, None)
        cont3_label = self.parse_create_cont_label(result.stdout_text)
        # Verify data in cont3
        cont3 = get_existing_container(self, pool2, cont3_label)
        self.dataset_verify(
            obj_list, cont3,
            self.num_objs, self.num_dkeys, self.num_akeys_single,
            self.num_akeys_array, self.akey_sizes, self.akey_extents)

    @avocado.fail_on(DaosApiError)
    def test_dm_obj_small_dcp(self):
        """
        Test Description:
            DAOS-6858: Verify cloning a small container.
        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_dcp
        :avocado: tags=DmvrObjSmallTest,test_dm_obj_small_dcp
        """
        self.run_dm_obj_small("DCP")

    @avocado.fail_on(DaosApiError)
    def test_dm_obj_small_cont_clone(self):
        """
        Test Description:
            DAOS-6858: Verify cloning a small container.
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=datamover,daos_cont_clone,daos_cmd
        :avocado: tags=DmvrObjSmallTest,test_dm_obj_small_cont_clone
        """
        self.run_dm_obj_small("CONT_CLONE")
